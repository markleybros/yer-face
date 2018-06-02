
#include "MarkerType.hpp"
#include "MarkerTracker.hpp"
#include "Utilities.hpp"

#include <iostream>
#include <cstdlib>

using namespace std;
using namespace cv;

namespace YerFace {

MarkerTracker::MarkerTracker(json config, MarkerType myMarkerType, FaceMapper *myFaceMapper, bool myPerformOpticalTracking) {
	markerType = MarkerType(myMarkerType);

	if(markerType.type == NoMarkerAssigned) {
		throw invalid_argument("MarkerTracker class cannot be assigned NoMarkerAssigned");
	}

	YerFace_MutexLock(myStaticMutex);
	for(auto markerTracker : markerTrackers) {
		if(markerTracker->getMarkerType().type == markerType.type) {
			throw invalid_argument("MarkerType collision trying to construct MarkerTracker");
		}
	}
	markerTrackers.push_back(this);
	YerFace_MutexUnlock(myStaticMutex);
	
	faceMapper = myFaceMapper;
	if(faceMapper == NULL) {
		throw invalid_argument("faceMapper cannot be NULL");
	}
	performOpticalTracking = myPerformOpticalTracking;
	trackingBoxPercentage = config["YerFace"]["MarkerTracker"]["trackingBoxPercentage"];
	if(trackingBoxPercentage <= 0.0) {
		throw invalid_argument("trackingBoxPercentage cannot be less than or equal to zero");
	}
	maxTrackerDriftPercentage = config["YerFace"]["MarkerTracker"]["maxTrackerDriftPercentage"];
	if(maxTrackerDriftPercentage <= 0.0) {
		throw invalid_argument("maxTrackerDriftPercentage cannot be less than or equal to zero");
	}
	pointSmoothingOverSeconds = config["YerFace"]["MarkerTracker"]["pointSmoothingOverSeconds"];
	if(pointSmoothingOverSeconds <= 0.0) {
		throw invalid_argument("pointSmoothingOverSeconds cannot be less than or equal to zero");
	}
	pointSmoothingExponent = config["YerFace"]["MarkerTracker"]["pointSmoothingExponent"];
	if(pointSmoothingExponent <= 0.0) {
		throw invalid_argument("pointSmoothingExponent cannot be less than or equal to zero");
	}
	pointMotionLowRejectionThreshold = config["YerFace"]["MarkerTracker"]["pointMotionLowRejectionThreshold"];
	if(pointMotionLowRejectionThreshold <= 0.0) {
		throw invalid_argument("pointMotionLowRejectionThreshold cannot be less than or equal to zero");
	}

	sdlDriver = faceMapper->getSDLDriver();
	frameDerivatives = faceMapper->getFrameDerivatives();
	faceTracker = faceMapper->getFaceTracker();
	markerSeparator = faceMapper->getMarkerSeparator();

	trackerState = DETECTING;
	working.markerPoint.set = false;
	working.previouslyReportedMarkerPoint.set = false;
	working.markerDetectedSet = false;
	working.trackingBoxSet = false;
	complete.markerPoint.set = false;
	complete.markerDetectedSet = false;
	complete.trackingBoxSet = false;
	markerList = markerSeparator->getWorkingMarkerList();

	if((myWrkMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((myCmpMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	string loggerName = "MarkerTracker<" + (string)markerType.toString() + ">";
	logger = new Logger(loggerName.c_str());
	logger->debug("MarkerTracker object constructed and ready to go!");
}

MarkerTracker::~MarkerTracker() noexcept(false) {
	logger->debug("MarkerTracker object destructing...");
	YerFace_MutexLock(myStaticMutex);
	for(vector<MarkerTracker *>::iterator iterator = markerTrackers.begin(); iterator != markerTrackers.end(); ++iterator) {
		if(*iterator == this) {
			markerTrackers.erase(iterator);
			return;
		}
	}
	YerFace_MutexUnlock(myStaticMutex);
	SDL_DestroyMutex(myWrkMutex);
	SDL_DestroyMutex(myCmpMutex);
	delete logger;
}

MarkerType MarkerTracker::getMarkerType(void) {
	return markerType;
}

TrackerState MarkerTracker::processCurrentFrame(void) {
	YerFace_MutexLock(myWrkMutex);

	working.markerDetectedSet = false;

	if(performOpticalTracking) {
		performTracking();
		performTrackToSeparatedCorrelation();
	}

	if(!working.markerDetectedSet) {
		performDetection();
	}

	if(performOpticalTracking) {
		if(working.markerDetectedSet) {
			if(!working.trackingBoxSet || trackerDriftingExcessively()) {
				performInitializationOfTracker();
			}
		}
	}
	
	assignMarkerPoint();

	calculate3dMarkerPoint();

	performMarkerPointSmoothing();

	YerFace_MutexUnlock(myWrkMutex);

	return trackerState;
}

void MarkerTracker::advanceWorkingToCompleted(void) {
	YerFace_MutexLock(myWrkMutex);
	YerFace_MutexLock(myCmpMutex);
	complete = working;
	YerFace_MutexUnlock(myCmpMutex);
	working.markerDetectedSet = false;
	working.trackingBoxSet = false;
	working.markerPoint.set = false;
	YerFace_MutexUnlock(myWrkMutex);
}

void MarkerTracker::performTrackToSeparatedCorrelation(void) {
	if(!working.trackingBoxSet) {
		return;
	}
	Point2d trackingBoxCenter = Utilities::centerRect(working.trackingBox);
	list<MarkerCandidate> markerCandidateList;
	markerSeparator->lockWorkingMarkerList();
	generateMarkerCandidateList(&markerCandidateList, trackingBoxCenter, &working.trackingBox);
	if(markerCandidateList.size() <= 0) {
		markerSeparator->unlockWorkingMarkerList();
		return;
	}
	markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
	claimMarkerCandidate(markerCandidateList.front());
	markerSeparator->unlockWorkingMarkerList();
}

void MarkerTracker::performDetection(void) {
	markerSeparator->lockWorkingMarkerList();
	if((*markerList).size() < 1) {
		markerSeparator->unlockWorkingMarkerList();
		return;
	}
	double proposedExclusionRadius = 0.0;
	int xDirection;
	list<MarkerCandidate> markerCandidateList;

	FacialFeatures facialFeatures = faceTracker->getFacialFeatures();
	Size frameSize = frameDerivatives->getWorkingFrameSize();
	Rect2d boundingRect;

	if(markerType.type == EyelidLeftTop || markerType.type == EyelidLeftBottom || markerType.type == EyelidRightTop || markerType.type == EyelidRightBottom) {
		ExclusionRadius exclusionRadius = faceMapper->exclusionRadiusFromPercentageOfFace(0.07);
		EyeRect eyeRect;
		if(markerType.type == EyelidLeftTop || markerType.type == EyelidLeftBottom) {
			eyeRect = faceMapper->getLeftEyeRect();
		} else {
			eyeRect = faceMapper->getRightEyeRect();
		}
		if(!eyeRect.set || !exclusionRadius.set) {
			markerSeparator->unlockWorkingMarkerList();
			return;
		}
		Point2d eyeRectCenter = Utilities::centerRect(eyeRect.rect);
		proposedExclusionRadius = exclusionRadius.exclusionRadius;

		generateMarkerCandidateList(&markerCandidateList, eyeRectCenter, &eyeRect.rect, proposedExclusionRadius, true);
		markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
		
		if(markerCandidateList.size() == 1) {
			if(!claimMarkerCandidate(markerCandidateList.front(), proposedExclusionRadius)) {
				markerSeparator->unlockWorkingMarkerList();
				return;
			}
		} else if(markerCandidateList.size() > 1) {
			list<MarkerCandidate>::iterator markerCandidateIterator = markerCandidateList.begin();
			MarkerCandidate markerCandidateA = *markerCandidateIterator;
			++markerCandidateIterator;
			MarkerCandidate markerCandidateB = *markerCandidateIterator;
			if(markerCandidateB.marker.center.y < markerCandidateA.marker.center.y) {
				if(markerType.type == EyelidLeftTop || markerType.type == EyelidRightTop) {
					if(!claimMarkerCandidate(markerCandidateB, proposedExclusionRadius)) {
						markerSeparator->unlockWorkingMarkerList();
						return;
					}
				} else {
					if(!claimMarkerCandidate(markerCandidateA, proposedExclusionRadius)) {
						markerSeparator->unlockWorkingMarkerList();
						return;
					}
				}
			} else {
				if(markerType.type == EyelidLeftTop || markerType.type == EyelidRightTop) {
					if(!claimMarkerCandidate(markerCandidateA, proposedExclusionRadius)) {
						markerSeparator->unlockWorkingMarkerList();
						return;
					}
				} else {
					if(!claimMarkerCandidate(markerCandidateB, proposedExclusionRadius)) {
						markerSeparator->unlockWorkingMarkerList();
						return;
					}
				}
			}
		}
		markerSeparator->unlockWorkingMarkerList();
		return;
	} else if(markerType.type == EyebrowLeftInner || markerType.type == EyebrowLeftMiddle || markerType.type == EyebrowLeftOuter || markerType.type == EyebrowRightInner || markerType.type == EyebrowRightMiddle || markerType.type == EyebrowRightOuter) {
		ExclusionRadius exclusionRadius = faceMapper->exclusionRadiusFromPercentageOfFace(0.05);
		if(!facialFeatures.set || !exclusionRadius.set) {
			markerSeparator->unlockWorkingMarkerList();
			return;
		}
		proposedExclusionRadius = exclusionRadius.exclusionRadius;

		xDirection = -1;
		if(markerType.type == EyebrowLeftInner || markerType.type == EyebrowLeftMiddle || markerType.type == EyebrowLeftOuter) {
			xDirection = 1;
		}

		boundingRect.y = 0;
		boundingRect.height = facialFeatures.noseSellion.y;
		if(xDirection < 0) {
			boundingRect.x = 0;
			boundingRect.width = facialFeatures.noseSellion.x;
		} else {
			boundingRect.x = facialFeatures.noseSellion.x;
			boundingRect.width = frameSize.width - facialFeatures.noseSellion.x;
		}

		if(markerType.type == EyebrowLeftInner || markerType.type == EyebrowRightInner) {
			generateMarkerCandidateList(&markerCandidateList, facialFeatures.noseSellion, &boundingRect, proposedExclusionRadius);
			if(markerCandidateList.size() < 1) {
				markerSeparator->unlockWorkingMarkerList();
				return;
			}
			markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
		} else {
			MarkerTracker *eyebrowTracker;
			if(xDirection > 0) {
				if(markerType.type == EyebrowLeftMiddle) {
					eyebrowTracker = MarkerTracker::getMarkerTrackerByType(MarkerType(EyebrowLeftInner));
				} else {
					eyebrowTracker = MarkerTracker::getMarkerTrackerByType(MarkerType(EyebrowLeftMiddle));
				}
			} else {
				if(markerType.type == EyebrowRightMiddle) {
					eyebrowTracker = MarkerTracker::getMarkerTrackerByType(MarkerType(EyebrowRightInner));
				} else {
					eyebrowTracker = MarkerTracker::getMarkerTrackerByType(MarkerType(EyebrowRightMiddle));
				}
			}
			if(eyebrowTracker == NULL) {
				markerSeparator->unlockWorkingMarkerList();
				return;
			}

			MarkerPoint eyeBrowPoint = eyebrowTracker->getWorkingMarkerPoint();
			if(!eyeBrowPoint.set) {
				markerSeparator->unlockWorkingMarkerList();
				return;
			}

			if(xDirection < 0) {
				boundingRect.width = eyeBrowPoint.point.x;
			} else {
				boundingRect.x = eyeBrowPoint.point.x;
				boundingRect.width = frameSize.width - eyeBrowPoint.point.x;
			}

			generateMarkerCandidateList(&markerCandidateList, eyeBrowPoint.point, &boundingRect, proposedExclusionRadius);
			if(markerCandidateList.size() < 1) {
				markerSeparator->unlockWorkingMarkerList();
				return;
			}
			markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
		}
	} else if(markerType.type == CheekLeft || markerType.type == CheekRight) {
		ExclusionRadius exclusionRadius = faceMapper->exclusionRadiusFromPercentageOfFace(0.075);
		if(!facialFeatures.set || !exclusionRadius.set) {
			markerSeparator->unlockWorkingMarkerList();
			return;
		}
		proposedExclusionRadius = exclusionRadius.exclusionRadius;

		xDirection = -1;
		if(markerType.type == CheekLeft) {
			xDirection = 1;
		}

		EyeRect eyeRect;
		if(xDirection > 0) {
			eyeRect = faceMapper->getLeftEyeRect();
		} else {
			eyeRect = faceMapper->getRightEyeRect();
		}
		if(!eyeRect.set) {
			markerSeparator->unlockWorkingMarkerList();
			return;
		}
		Point2d cheekPoI = Point2d(
			(eyeRect.rect.x + eyeRect.rect.x + eyeRect.rect.width) / 2.0,
			eyeRect.rect.y + eyeRect.rect.height
		);

		boundingRect.y = cheekPoI.y;
		boundingRect.height = facialFeatures.stommion.y - cheekPoI.y;
		if(xDirection < 0) {
			boundingRect.x = facialFeatures.jawRightTop.x;
			boundingRect.width = facialFeatures.noseSellion.x - facialFeatures.jawRightTop.x;
		} else {
			boundingRect.x = facialFeatures.noseSellion.x;
			boundingRect.width = facialFeatures.jawLeftTop.x - facialFeatures.noseSellion.x;
		}
		generateMarkerCandidateList(&markerCandidateList, cheekPoI, &boundingRect, proposedExclusionRadius);

		if(markerCandidateList.size() < 1) {
			markerSeparator->unlockWorkingMarkerList();
			return;
		}
		markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
	} else if(markerType.type == Jaw) {
		ExclusionRadius exclusionRadius = faceMapper->exclusionRadiusFromPercentageOfFace(0.125);
		if(!facialFeatures.set || !exclusionRadius.set) {
			markerSeparator->unlockWorkingMarkerList();
			return;
		}
		proposedExclusionRadius = exclusionRadius.exclusionRadius;

		boundingRect.x = facialFeatures.eyeRightOuterCorner.x;
		boundingRect.width = facialFeatures.eyeLeftOuterCorner.x - boundingRect.x;
		boundingRect.y = facialFeatures.noseTip.y;
		boundingRect.height = frameSize.height - boundingRect.y;

		generateMarkerCandidateList(&markerCandidateList, facialFeatures.menton, &boundingRect, proposedExclusionRadius);
		if(markerCandidateList.size() < 1) {
			markerSeparator->unlockWorkingMarkerList();
			return;
		}
		markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
	} else if(markerType.type == LipsLeftCorner || markerType.type == LipsRightCorner || markerType.type == LipsLeftTop || markerType.type == LipsRightTop || markerType.type == LipsLeftBottom || markerType.type == LipsRightBottom) {
		ExclusionRadius exclusionRadius = faceMapper->exclusionRadiusFromPercentageOfFace(0.05);
		if(!facialFeatures.set || !exclusionRadius.set) {
			markerSeparator->unlockWorkingMarkerList();
			return;
		}
		proposedExclusionRadius = exclusionRadius.exclusionRadius;

		MarkerTracker *jawTracker;
		jawTracker = MarkerTracker::getMarkerTrackerByType(MarkerType(Jaw));
		if(jawTracker == NULL) {
			markerSeparator->unlockWorkingMarkerList();
			return;
		}
		
		MarkerPoint jawPoint = jawTracker->getWorkingMarkerPoint();
		if(!jawPoint.set) {
			markerSeparator->unlockWorkingMarkerList();
			return;
		}

		MarkerTracker *cheekTracker;
		MarkerPoint cheekPoint;

		double avgX = (facialFeatures.menton.x + facialFeatures.stommion.x + facialFeatures.noseTip.x) / 3.0;

		Point2d lipPointOfInterest;
		if(markerType.type == LipsLeftCorner || markerType.type == LipsLeftTop || markerType.type == LipsLeftBottom) {
			cheekTracker = MarkerTracker::getMarkerTrackerByType(MarkerType(CheekLeft));
			if(cheekTracker == NULL) {
				markerSeparator->unlockWorkingMarkerList();
				return;
			}
			cheekPoint = cheekTracker->getWorkingMarkerPoint();
			if(!cheekPoint.set) {
				markerSeparator->unlockWorkingMarkerList();
				return;
			}

			boundingRect.y = cheekPoint.point.y;
			boundingRect.height = jawPoint.point.y - boundingRect.y;
			boundingRect.x = avgX;
			boundingRect.width = cheekPoint.point.x - boundingRect.x;
		} else {
			cheekTracker = MarkerTracker::getMarkerTrackerByType(MarkerType(CheekRight));
			if(cheekTracker == NULL) {
				markerSeparator->unlockWorkingMarkerList();
				return;
			}
			cheekPoint = cheekTracker->getWorkingMarkerPoint();
			if(!cheekPoint.set) {
				markerSeparator->unlockWorkingMarkerList();
				return;
			}

			boundingRect.y = cheekPoint.point.y;
			boundingRect.height = jawPoint.point.y - boundingRect.y;
			boundingRect.x = cheekPoint.point.x;
			boundingRect.width = avgX - boundingRect.x;
		}

		if(markerType.type == LipsLeftCorner || markerType.type == LipsRightCorner) {
			if(markerType.type == LipsLeftCorner) {
				lipPointOfInterest = Point2d(boundingRect.x + boundingRect.width, boundingRect.y) + boundingRect.br();
			} else {
				lipPointOfInterest = boundingRect.tl() + Point2d(boundingRect.x, boundingRect.y + boundingRect.height);
			}
			lipPointOfInterest.x = lipPointOfInterest.x / 2.0;
			lipPointOfInterest.y = lipPointOfInterest.y / 2.0;
		} else if(markerType.type == LipsLeftTop || markerType.type == LipsRightTop) {
			lipPointOfInterest = facialFeatures.noseTip;
		} else {
			lipPointOfInterest = jawPoint.point;
		}

		generateMarkerCandidateList(&markerCandidateList, lipPointOfInterest, &boundingRect, proposedExclusionRadius);
		if(markerCandidateList.size() < 1) {
			markerSeparator->unlockWorkingMarkerList();
			return;
		}
		markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
	}
	if(markerCandidateList.size() > 0) {
		claimFirstAvailableMarkerCandidate(&markerCandidateList, proposedExclusionRadius);
	}
	markerSeparator->unlockWorkingMarkerList();
}

void MarkerTracker::performInitializationOfTracker(void) {
	if(!working.markerDetectedSet) {
		throw invalid_argument("MarkerTracker::performInitializationOfTracker() called while markerDetectedSet is false");
	}
	trackerState = TRACKING;
	tracker = TrackerKCF::create();
	working.trackingBox = Rect(Utilities::insetBox(working.markerDetected.marker.boundingRect2f(), trackingBoxPercentage));
	working.trackingBoxSet = true;

	tracker->init(frameDerivatives->getWorkingFrame(), working.trackingBox);
}

bool MarkerTracker::performTracking(void) {
	if(trackerState == TRACKING) {
		bool trackSuccess = tracker->update(frameDerivatives->getWorkingFrame(), working.trackingBox);
		if(!trackSuccess) {
			working.trackingBoxSet = false;
			return false;
		}
		working.trackingBoxSet = true;
		return true;
	}
	return false;
}

bool MarkerTracker::trackerDriftingExcessively(void) {
	if(!working.markerDetectedSet || !working.trackingBoxSet) {
		throw invalid_argument("MarkerTracker::trackerDriftingExcessively() called while one or both of markerDetectedSet or trackingBoxSet are false");
	}
	double actualDistance = Utilities::lineDistance(working.markerDetected.marker.center, Utilities::centerRect(working.trackingBox));
	double maxDistance = working.markerDetected.sqrtArea * maxTrackerDriftPercentage;
	if(actualDistance > maxDistance) {
		logger->warn("Optical tracker drifting excessively! Resetting it.");
		return true;
	}
	return false;
}

bool MarkerTracker::claimMarkerCandidate(MarkerCandidate markerCandidate, double setExclusionRadius) {
	size_t markerListCount = (*markerList).size();
	if(markerCandidate.markerListIndex >= markerListCount) {
		throw invalid_argument("MarkerTracker::claimMarkerCandidate() called with a markerCandidate whose index is outside the bounds of markerList");
	}
	MarkerSeparated *markerSeparatedCandidate = &(*markerList)[markerCandidate.markerListIndex];
	if(markerSeparatedCandidate->assignedType.type != NoMarkerAssigned) {
		return false;
	}
	markerSeparatedCandidate->exclusionRadius = setExclusionRadius;
	markerSeparatedCandidate->assignedType.type = markerType.type;
	working.markerDetected = markerCandidate;
	working.markerDetectedSet = true;
	return true;
}

bool MarkerTracker::claimFirstAvailableMarkerCandidate(list<MarkerCandidate> *markerCandidateList, double setExclusionRadius) {
	if(markerCandidateList == NULL) {
		throw invalid_argument("MarkerTracker::claimFirstAvailableMarkerCandidate() called with NULL markerCandidateList");
	}
	for(list<MarkerCandidate>::iterator iterator = markerCandidateList->begin(); iterator != markerCandidateList->end(); ++iterator) {
		if(claimMarkerCandidate(*iterator, setExclusionRadius)) {
			return true;
		}
	}
	return false;
}

void MarkerTracker::assignMarkerPoint(void) {
	working.markerPoint.set = false;
	if(working.markerDetectedSet && working.trackingBoxSet) {
		Point2d detectedPoint = Point(working.markerDetected.marker.center);
		Point2d trackingPoint = Point(Utilities::centerRect(working.trackingBox));
		double actualDistance = Utilities::lineDistance(detectedPoint, trackingPoint);
		double maxDistance = working.markerDetected.sqrtArea * maxTrackerDriftPercentage;
		double detectedPointWeight = actualDistance / maxDistance;
		if(detectedPointWeight < 0.0) {
			detectedPointWeight = 0.0;
		} else if(detectedPointWeight > 1.0) {
			detectedPointWeight = 1.0;
		}
		double trackingPointWeight = 1.0 - detectedPointWeight;
		detectedPoint.x = detectedPoint.x * detectedPointWeight;
		detectedPoint.y = detectedPoint.y * detectedPointWeight;
		trackingPoint.x = trackingPoint.x * trackingPointWeight;
		trackingPoint.y = trackingPoint.y * trackingPointWeight;
		working.markerPoint.point = detectedPoint + trackingPoint;
		working.markerPoint.set = true;
	} else if(working.markerDetectedSet) {
		working.markerPoint.point = working.markerDetected.marker.center;
		working.markerPoint.set = true;
	} else if(working.trackingBoxSet) {
		working.markerPoint.point = Utilities::centerRect(working.trackingBox);
		working.markerPoint.set = true;
	} else {
		if(trackerState == TRACKING) {
			trackerState = LOST;
			logger->warn("Lost marker completely! Will keep searching...");
		}
	}	
}

void MarkerTracker::calculate3dMarkerPoint(void) {
	if(!working.markerPoint.set) {
		return;
	}
	FacialPose facialPose = faceTracker->getWorkingFacialPose();
	FacialCameraModel cameraModel = faceTracker->getFacialCameraModel();
	if(!facialPose.set || !cameraModel.set) {
		return;
	}
	Mat homogeneousPoint = (Mat_<double>(3,1) << working.markerPoint.point.x, working.markerPoint.point.y, 1.0);
	Mat worldPoint = cameraModel.cameraMatrix.inv() * homogeneousPoint;
	Point3d intersection;
	Point3d rayOrigin = Point3d(0,0,0);
	Vec3d rayVector = Vec3d(worldPoint.at<double>(0), worldPoint.at<double>(1), worldPoint.at<double>(2));
	if(!Utilities::rayPlaneIntersection(intersection, rayOrigin, rayVector, facialPose.planePoint, facialPose.planeNormal)) {
		logger->warn("Failed 3d ray/plane intersection with face plane! No update to 3d marker point.");
		return;
	}
	Mat markerMat = (Mat_<double>(3, 1) << intersection.x, intersection.y, intersection.z);
	markerMat = markerMat - facialPose.translationVector;
	markerMat = facialPose.rotationMatrix.inv() * markerMat;
	working.markerPoint.point3d = Point3d(markerMat.at<double>(0), markerMat.at<double>(1), markerMat.at<double>(2));
	// logger->verbose("Recovered approximate 3D position: <%.03f, %.03f, %.03f>", working.markerPoint.point3d.x, working.markerPoint.point3d.y, working.markerPoint.point3d.z);
}

void MarkerTracker::performMarkerPointSmoothing(void) {
	if(!working.markerPoint.set) {
		return;
	}
	FrameTimestamps frameTimestamps = frameDerivatives->getWorkingFrameTimestamps();
	double frameTimestamp = frameTimestamps.startTimestamp;
	working.markerPoint.timestamp = frameTimestamp;
	markerPointSmoothingBuffer.push_back(working.markerPoint);
	while(markerPointSmoothingBuffer.front().timestamp <= (frameTimestamp - pointSmoothingOverSeconds)) {
		markerPointSmoothingBuffer.pop_front();
	}

	MarkerPoint tempPoint = working.markerPoint;
	tempPoint.point3d.x = 0;
	tempPoint.point3d.y = 0;
	tempPoint.point3d.z = 0;

	double combinedWeights = 0.0;
	for(MarkerPoint point : markerPointSmoothingBuffer) {
		double progress = (point.timestamp - (frameTimestamp - pointSmoothingOverSeconds)) / pointSmoothingOverSeconds;
		double weight = std::pow(progress, (double)pointSmoothingExponent) - combinedWeights;
		combinedWeights += weight;
		tempPoint.point3d.x += point.point3d.x * weight;
		tempPoint.point3d.y += point.point3d.y * weight;
		tempPoint.point3d.z += point.point3d.z * weight;
	}

	bool reportNewPoint = true;
	if(working.previouslyReportedMarkerPoint.set) {
		double distance = Utilities::lineDistance(tempPoint.point3d, working.previouslyReportedMarkerPoint.point3d);
		if(distance < pointMotionLowRejectionThreshold) {
			reportNewPoint = false;
		}
	}

	if(reportNewPoint) {
		working.markerPoint = tempPoint;
		working.previouslyReportedMarkerPoint = working.markerPoint;
	} else {
		working.markerPoint = working.previouslyReportedMarkerPoint;
	}
}

void MarkerTracker::generateMarkerCandidateList(list<MarkerCandidate> *markerCandidateList, Point2d pointOfInterest, Rect2d *boundingRect, double proposedExclusionRadius, bool overrideExclusionZone, bool debug) {
	if(markerCandidateList == NULL) {
		throw invalid_argument("MarkerTracker::generateMarkerCandidateList() called with NULL markerCandidateList");
	}
	if(debug) {
		Mat prevFrame = frameDerivatives->getWorkingPreviewFrame();
		Utilities::drawX(prevFrame, pointOfInterest, Scalar(255, 0, 255), 10, 2);
		if(boundingRect != NULL) {
			rectangle(prevFrame, *boundingRect, Scalar(255, 0, 255), 2);
		}
	}
	MarkerCandidate markerCandidate;
	size_t markerListCount = (*markerList).size();
	for(size_t i = 0; i < markerListCount; i++) {
		MarkerSeparated markerSeparated = (*markerList)[i];
		if(!markerSeparated.active) {
			continue;
		}
		if(markerSeparated.assignedType.type != NoMarkerAssigned) {
			continue;
		}
		RotatedRect marker = markerSeparated.marker;
		Rect2d markerRect = Rect(marker.boundingRect2f());
		if(boundingRect == NULL || (markerRect & (*boundingRect)).area() > 0) {
			if(!overrideExclusionZone) {
				bool excluded = false;
				for(MarkerSeparated markerCompare : (*markerList)) {
					if(markerCompare.active && markerCompare.assignedType.type != NoMarkerAssigned && markerCompare.exclusionRadius > 0.0) {
						double markerDistance = Utilities::lineDistance(markerSeparated.marker.center, markerCompare.marker.center);
						if(markerDistance <= (proposedExclusionRadius + markerCompare.exclusionRadius)) {
							excluded = true;
							break;
						}
					}
				}
				if(excluded) {
					continue;
				}
			}

			markerCandidate.marker = marker;
			markerCandidate.markerListIndex = i;
			markerCandidate.distanceFromPointOfInterest = Utilities::lineDistance(pointOfInterest, markerCandidate.marker.center);
			markerCandidate.sqrtArea = std::sqrt((double)(markerCandidate.marker.size.width * markerCandidate.marker.size.height));
			markerCandidateList->push_back(markerCandidate);
		}
	}
}

bool MarkerTracker::sortMarkerCandidatesByDistanceFromPointOfInterest(const MarkerCandidate a, const MarkerCandidate b) {
	return (a.distanceFromPointOfInterest < b.distanceFromPointOfInterest);
}

void MarkerTracker::renderPreviewHUD(void) {
	YerFace_MutexLock(myCmpMutex);
	Scalar color = Scalar(0, 0, 255);
	if(markerType.type == EyelidLeftBottom || markerType.type == EyelidRightBottom || markerType.type == EyelidLeftTop || markerType.type == EyelidRightTop) {
		color = Scalar(0, 255, 255);
		if(markerType.type == EyelidLeftTop || markerType.type == EyelidRightTop) {
			color[1] = 127;
		}
	} else if(markerType.type == EyebrowLeftInner || markerType.type == EyebrowLeftMiddle || markerType.type == EyebrowLeftOuter || markerType.type == EyebrowRightInner || markerType.type == EyebrowRightMiddle || markerType.type == EyebrowRightOuter) {
		color = Scalar(255, 0, 0);
		if(markerType.type == EyebrowLeftMiddle || markerType.type == EyebrowRightMiddle) {
			color[2] = 127;
		} else if(markerType.type == EyebrowLeftOuter || markerType.type == EyebrowRightOuter) {
			color[2] = 255;
		}
	} else if(markerType.type == CheekLeft || markerType.type == CheekRight) {
		color = Scalar(255, 255, 0);
	} else if(markerType.type == Jaw) {
		color = Scalar(0, 255, 0);
	} else if(markerType.type == LipsLeftCorner || markerType.type == LipsRightCorner || markerType.type == LipsLeftTop || markerType.type == LipsRightTop || markerType.type == LipsLeftBottom || markerType.type == LipsRightBottom) {
		color = Scalar(0, 0, 255);
		if(markerType.type == LipsLeftCorner || markerType.type == LipsRightCorner) {
			color[1] = 127;
		} else if(markerType.type == LipsLeftTop || markerType.type == LipsRightTop) {
			color[0] = 127;
		} else {
			color[0] = 127;
			color[1] = 127;
		}
	}
	Mat frame = frameDerivatives->getCompletedPreviewFrame();
	int density = sdlDriver->getPreviewDebugDensity();
	if(density > 0) {
		Utilities::drawX(frame, complete.markerPoint.point, color, 10, 2);
	}
	if(density > 1) {
		if(complete.markerDetectedSet) {
			Utilities::drawRotatedRectOutline(frame, complete.markerDetected.marker, color, 1);
		}
	}
	if(density > 2) {
		if(complete.trackingBoxSet) {
			rectangle(frame, complete.trackingBox, color, 1);
		}
	}
	YerFace_MutexUnlock(myCmpMutex);
}

TrackerState MarkerTracker::getTrackerState(void) {
	YerFace_MutexLock(myWrkMutex);
	TrackerState val = trackerState;
	YerFace_MutexUnlock(myWrkMutex);
	return val;
}

MarkerPoint MarkerTracker::getWorkingMarkerPoint(void) {
	YerFace_MutexLock(myWrkMutex);
	MarkerPoint val = working.markerPoint;
	YerFace_MutexUnlock(myWrkMutex);
	return val;
}

MarkerPoint MarkerTracker::getCompletedMarkerPoint(void) {
	YerFace_MutexLock(myCmpMutex);
	MarkerPoint val = complete.markerPoint;
	YerFace_MutexUnlock(myCmpMutex);
	return val;
}

vector<MarkerTracker *> MarkerTracker::markerTrackers;
SDL_mutex *MarkerTracker::myStaticMutex = SDL_CreateMutex();

vector<MarkerTracker *> MarkerTracker::getMarkerTrackers(void) {
	YerFace_MutexLock(myStaticMutex);
	auto val = markerTrackers;
	YerFace_MutexUnlock(myStaticMutex);
	return val;
}

MarkerTracker *MarkerTracker::getMarkerTrackerByType(MarkerType markerType) {
	YerFace_MutexLock(myStaticMutex);
	for(auto markerTracker : markerTrackers) {
		if(markerTracker->getMarkerType().type == markerType.type) {
			YerFace_MutexUnlock(myStaticMutex);
			return markerTracker;
		}
	}
	YerFace_MutexUnlock(myStaticMutex);
	return NULL;
}

}; //namespace YerFace
