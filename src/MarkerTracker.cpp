
#include "MarkerType.hpp"
#include "MarkerTracker.hpp"
#include "Utilities.hpp"

#include <iostream>
#include <cstdlib>

using namespace std;
using namespace cv;

namespace YerFace {

MarkerTracker::MarkerTracker(MarkerType myMarkerType, FaceMapper *myFaceMapper, float myTrackingBoxPercentage, float myMaxTrackerDriftPercentage) {
	markerType = MarkerType(myMarkerType);

	if(markerType.type == NoMarkerAssigned) {
		throw invalid_argument("MarkerTracker class cannot be assigned NoMarkerAssigned");
	}
	size_t markerTrackersCount = markerTrackers.size();
	for(size_t i = 0; i < markerTrackersCount; i++) {
		if(markerTrackers[i]->getMarkerType().type == markerType.type) {
			fprintf(stderr, "Trying to construct MarkerTracker <%s> object, but one already exists!\n", markerType.toString());
			throw invalid_argument("MarkerType collision trying to construct MarkerTracker");
		}
	}
	markerTrackers.push_back(this);

	faceMapper = myFaceMapper;
	if(faceMapper == NULL) {
		throw invalid_argument("faceMapper cannot be NULL");
	}
	trackingBoxPercentage = myTrackingBoxPercentage;
	if(trackingBoxPercentage <= 0.0) {
		throw invalid_argument("trackingBoxPercentage cannot be less than or equal to zero");
	}
	maxTrackerDriftPercentage = myMaxTrackerDriftPercentage;
	if(maxTrackerDriftPercentage <= 0.0) {
		throw invalid_argument("maxTrackerDriftPercentage cannot be less than or equal to zero");
	}

	trackerState = DETECTING;
	markerDetectedSet = false;
	markerPoint.set = false;
	trackingBoxSet = false;
	markerList = NULL;

	frameDerivatives = faceMapper->getFrameDerivatives();
	faceTracker = faceMapper->getFaceTracker();
	markerSeparator = faceMapper->getMarkerSeparator();

	fprintf(stderr, "MarkerTracker <%s> object constructed and ready to go!\n", markerType.toString());
}

MarkerTracker::~MarkerTracker() {
	fprintf(stderr, "MarkerTracker <%s> object destructing...\n", markerType.toString());
	for(vector<MarkerTracker *>::iterator iterator = markerTrackers.begin(); iterator != markerTrackers.end(); ++iterator) {
		if(*iterator == this) {
			markerTrackers.erase(iterator);
			return;
		}
	}
}

MarkerType MarkerTracker::getMarkerType(void) {
	return markerType;
}

TrackerState MarkerTracker::processCurrentFrame(void) {
	performTracking();

	markerDetectedSet = false;
	markerList = markerSeparator->getMarkerList();
	
	performTrackToSeparatedCorrelation();

	if(!markerDetectedSet) {
		performDetection();
	}

	if(markerDetectedSet) {
		if(!trackingBoxSet || trackerDriftingExcessively()) {
			performInitializationOfTracker();
		}
	}
	
	assignMarkerPoint();

	calculate3dMarkerPoint();

	return trackerState;
}

void MarkerTracker::performTrackToSeparatedCorrelation(void) {
	if(markerList == NULL) {
		throw invalid_argument("MarkerTracker::performTrackToSeparatedCorrelation() called while markerList is NULL");
	}
	if((*markerList).size() < 1) {
		return;
	}
	if(!trackingBoxSet) {
		return;
	}
	Point2d trackingBoxCenter = Utilities::centerRect(trackingBox);
	list<MarkerCandidate> markerCandidateList;
	generateMarkerCandidateList(&markerCandidateList, trackingBoxCenter, &trackingBox);
	if(markerCandidateList.size() <= 0) {
		return;
	}
	markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
	if(!claimMarkerCandidate(markerCandidateList.front())) {
		return;
	}

}

void MarkerTracker::performDetection(void) {
	if(markerList == NULL) {
		throw invalid_argument("MarkerTracker::performDetection() called while markerList is NULL");
	}
	if((*markerList).size() < 1) {
		return;
	}
	int xDirection;
	list<MarkerCandidate> markerCandidateList;

	FacialFeatures facialFeatures = faceTracker->getFacialFeatures();
	Size frameSize = frameDerivatives->getCurrentFrame().size();
	Rect2d boundingRect;

	if(markerType.type == EyelidLeftTop || markerType.type == EyelidLeftBottom || markerType.type == EyelidRightTop || markerType.type == EyelidRightBottom) {
		EyeRect eyeRect;
		if(markerType.type == EyelidLeftTop || markerType.type == EyelidLeftBottom) {
			eyeRect = faceMapper->getLeftEyeRect();
		} else {
			eyeRect = faceMapper->getRightEyeRect();
		}
		if(!eyeRect.set) {
			return;
		}
		Point2d eyeRectCenter = Utilities::centerRect(eyeRect.rect);

		generateMarkerCandidateList(&markerCandidateList, eyeRectCenter, &eyeRect.rect);
		markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
		
		if(markerCandidateList.size() == 1) {
			if(markerType.type == EyelidLeftBottom || markerType.type == EyelidRightBottom) {
				return;
			}
			if(!claimMarkerCandidate(markerCandidateList.front())) {
				return;
			}
		} else if(markerCandidateList.size() > 1) {
			list<MarkerCandidate>::iterator markerCandidateIterator = markerCandidateList.begin();
			MarkerCandidate markerCandidateA = *markerCandidateIterator;
			++markerCandidateIterator;
			MarkerCandidate markerCandidateB = *markerCandidateIterator;
			if(markerCandidateB.marker.center.y < markerCandidateA.marker.center.y) {
				if(markerType.type == EyelidLeftTop || markerType.type == EyelidRightTop) {
					if(!claimMarkerCandidate(markerCandidateB)) {
						return;
					}
				} else {
					if(!claimMarkerCandidate(markerCandidateA)) {
						return;
					}
				}
			} else {
				if(markerType.type == EyelidLeftTop || markerType.type == EyelidRightTop) {
					if(!claimMarkerCandidate(markerCandidateA)) {
						return;
					}
				} else {
					if(!claimMarkerCandidate(markerCandidateB)) {
						return;
					}
				}
			}
		}
		return;
	} else if(markerType.type == EyebrowLeftInner || markerType.type == EyebrowLeftMiddle || markerType.type == EyebrowLeftOuter || markerType.type == EyebrowRightInner || markerType.type == EyebrowRightMiddle || markerType.type == EyebrowRightOuter) {
		if(!facialFeatures.set) {
			return;
		}
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
			generateMarkerCandidateList(&markerCandidateList, facialFeatures.noseSellion, &boundingRect);
			if(markerCandidateList.size() < 1) {
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
				return;
			}

			MarkerPoint eyeBrowPoint = eyebrowTracker->getMarkerPoint();
			if(!eyeBrowPoint.set) {
				return;
			}

			if(xDirection < 0) {
				boundingRect.width = eyeBrowPoint.point.x;
			} else {
				boundingRect.x = eyeBrowPoint.point.x;
				boundingRect.width = frameSize.width - eyeBrowPoint.point.x;
			}

			generateMarkerCandidateList(&markerCandidateList, eyeBrowPoint.point, &boundingRect);
			if(markerCandidateList.size() < 1) {
				return;
			}
			markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
		}
	} else if(markerType.type == CheekLeft || markerType.type == CheekRight) {
		if(!facialFeatures.set) {
			return;
		}

		xDirection = -1;
		if(markerType.type == CheekLeft) {
			xDirection = 1;
		}

		MarkerTracker *eyelidTracker;
		if(xDirection < 0) {
			eyelidTracker = MarkerTracker::getMarkerTrackerByType(MarkerType(EyelidRightBottom));
		} else {
			eyelidTracker = MarkerTracker::getMarkerTrackerByType(MarkerType(EyelidLeftBottom));
		}
		if(eyelidTracker == NULL) {
			return;
		}

		MarkerPoint eyelidPoint = eyelidTracker->getMarkerPoint();
		if(!eyelidPoint.set) {
			return;
		}

		boundingRect.y = eyelidPoint.point.y;
		boundingRect.height = facialFeatures.stommion.y - eyelidPoint.point.y;
		if(xDirection < 0) {
			boundingRect.x = facialFeatures.jawRightTop.x;
			boundingRect.width = facialFeatures.noseSellion.x - facialFeatures.jawRightTop.x;
		} else {
			boundingRect.x = facialFeatures.noseSellion.x;
			boundingRect.width = facialFeatures.jawLeftTop.x - facialFeatures.noseSellion.x;
		}
		generateMarkerCandidateList(&markerCandidateList, eyelidPoint.point, &boundingRect);

		if(markerCandidateList.size() < 1) {
			return;
		}
		markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
	} else if(markerType.type == Jaw) {
		if(!facialFeatures.set) {
			return;
		}
		boundingRect.x = facialFeatures.eyeRightOuterCorner.x;
		boundingRect.width = facialFeatures.eyeLeftOuterCorner.x - boundingRect.x;
		boundingRect.y = facialFeatures.noseTip.y;
		boundingRect.height = frameSize.height - boundingRect.y;

		generateMarkerCandidateList(&markerCandidateList, facialFeatures.menton, &boundingRect);
		if(markerCandidateList.size() < 1) {
			return;
		}
		markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
	} else if(markerType.type == LipsLeftCorner || markerType.type == LipsRightCorner || markerType.type == LipsLeftTop || markerType.type == LipsRightTop || markerType.type == LipsLeftBottom || markerType.type == LipsRightBottom) {
		if(!facialFeatures.set) {
			return;
		}

		MarkerTracker *jawTracker;
		jawTracker = MarkerTracker::getMarkerTrackerByType(MarkerType(Jaw));
		if(jawTracker == NULL) {
			return;
		}
		
		MarkerPoint jawPoint = jawTracker->getMarkerPoint();
		if(!jawPoint.set) {
			return;
		}

		MarkerTracker *cheekTracker;
		MarkerPoint cheekPoint;

		double avgX = (facialFeatures.menton.x + facialFeatures.stommion.x + facialFeatures.noseTip.x) / 3.0;

		Point2d lipPointOfInterest;
		if(markerType.type == LipsLeftCorner || markerType.type == LipsLeftTop || markerType.type == LipsLeftBottom) {
			cheekTracker = MarkerTracker::getMarkerTrackerByType(MarkerType(CheekLeft));
			if(cheekTracker == NULL) {
				return;
			}
			cheekPoint = cheekTracker->getMarkerPoint();
			if(!cheekPoint.set) {
				return;
			}

			boundingRect.y = cheekPoint.point.y;
			boundingRect.height = jawPoint.point.y - boundingRect.y;
			boundingRect.x = avgX;
			boundingRect.width = cheekPoint.point.x - boundingRect.x;
		} else {
			cheekTracker = MarkerTracker::getMarkerTrackerByType(MarkerType(CheekRight));
			if(cheekTracker == NULL) {
				return;
			}
			cheekPoint = cheekTracker->getMarkerPoint();
			if(!cheekPoint.set) {
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

		generateMarkerCandidateList(&markerCandidateList, lipPointOfInterest, &boundingRect);
		if(markerCandidateList.size() < 1) {
			return;
		}
		markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
	}
	if(markerCandidateList.size() > 0) {
		claimFirstAvailableMarkerCandidate(&markerCandidateList);
	}
}

void MarkerTracker::performInitializationOfTracker(void) {
	if(!markerDetectedSet) {
		throw invalid_argument("MarkerTracker::performInitializationOfTracker() called while markerDetectedSet is false");
	}
	trackerState = TRACKING;
	#if (CV_MINOR_VERSION < 3)
	tracker = Tracker::create("KCF");
	#else
	tracker = TrackerKCF::create();
	#endif
	trackingBox = Rect(Utilities::insetBox(markerDetected.marker.boundingRect2f(), trackingBoxPercentage));
	trackingBoxSet = true;

	tracker->init(frameDerivatives->getCurrentFrame(), trackingBox);
}

bool MarkerTracker::performTracking(void) {
	if(trackerState == TRACKING) {
		bool trackSuccess = tracker->update(frameDerivatives->getCurrentFrame(), trackingBox);
		if(!trackSuccess) {
			trackingBoxSet = false;
			return false;
		}
		trackingBoxSet = true;
		return true;
	}
	return false;
}

bool MarkerTracker::trackerDriftingExcessively(void) {
	if(!markerDetectedSet || !trackingBoxSet) {
		throw invalid_argument("MarkerTracker::trackerDriftingExcessively() called while one or both of markerDetectedSet or trackingBoxSet are false");
	}
	double actualDistance = Utilities::lineDistance(markerDetected.marker.center, Utilities::centerRect(trackingBox));
	double maxDistance = markerDetected.sqrtArea * maxTrackerDriftPercentage;
	if(actualDistance > maxDistance) {
		fprintf(stderr, "MarkerTracker <%s>: WARNING: Optical tracker drifting excessively! Resetting it.\n", markerType.toString());
		return true;
	}
	return false;
}

bool MarkerTracker::claimMarkerCandidate(MarkerCandidate markerCandidate) {
	if(markerList == NULL) {
		throw invalid_argument("MarkerTracker::claimMarkerCandidate() called while markerList is NULL");
	}
	size_t markerListCount = (*markerList).size();
	if(markerCandidate.markerListIndex >= markerListCount) {
		throw invalid_argument("MarkerTracker::claimMarkerCandidate() called with a markerCandidate whose index is outside the bounds of markerList");
	}
	MarkerSeparated *markerSeparatedCandidate = &(*markerList)[markerCandidate.markerListIndex];
	if(markerSeparatedCandidate->assignedType.type != NoMarkerAssigned) {
		// fprintf(stderr, "MarkerTracker <%s>: WARNING: Attempted to claim marker %u but it was already assigned type <%s>.\n", markerType.toString(), markerCandidate.markerListIndex, markerSeparatedCandidate->assignedType.toString());
		return false;
	}
	markerSeparatedCandidate->assignedType.type = markerType.type;
	markerDetected = markerCandidate;
	markerDetectedSet = true;
	return true;
}

bool MarkerTracker::claimFirstAvailableMarkerCandidate(list<MarkerCandidate> *markerCandidateList) {
	if(markerCandidateList == NULL) {
		throw invalid_argument("MarkerTracker::claimFirstAvailableMarkerCandidate() called with NULL markerCandidateList");
	}
	for(list<MarkerCandidate>::iterator iterator = markerCandidateList->begin(); iterator != markerCandidateList->end(); ++iterator) {
		if(claimMarkerCandidate(*iterator)) {
			return true;
		}
	}
	return false;
}

void MarkerTracker::assignMarkerPoint(void) {
	markerPoint.set = false;
	if(markerDetectedSet && trackingBoxSet) {
		Point2d detectedPoint = Point(markerDetected.marker.center);
		Point2d trackingPoint = Point(Utilities::centerRect(trackingBox));
		double actualDistance = Utilities::lineDistance(detectedPoint, trackingPoint);
		double maxDistance = markerDetected.sqrtArea * maxTrackerDriftPercentage;
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
		markerPoint.point = detectedPoint + trackingPoint;
		markerPoint.set = true;
	} else if(markerDetectedSet) {
		markerPoint.point = markerDetected.marker.center;
		markerPoint.set = true;
	} else if(trackingBoxSet) {
		markerPoint.point = Utilities::centerRect(trackingBox);
		markerPoint.set = true;
	} else {
		if(trackerState == TRACKING) {
			trackerState = LOST;
			fprintf(stderr, "MarkerTracker <%s> Lost marker completely! Will keep searching...\n", markerType.toString());
		}
	}	
}

void MarkerTracker::calculate3dMarkerPoint(void) {
	if(!markerPoint.set) {
		return;
	}
	FacialPose facialPose = faceTracker->getFacialPose();
	FacialCameraModel cameraModel = faceTracker->getFacialCameraModel();
	if(!facialPose.set || !cameraModel.set) {
		return;
	}
	Mat homogeneousPoint = (Mat_<double>(3,1) << markerPoint.point.x, markerPoint.point.y, 1.0);
	Mat worldPoint = cameraModel.cameraMatrix.inv() * homogeneousPoint;
	Point3d intersection;
	Point3d rayOrigin = Point3d(0,0,0);
	Vec3d rayVector = Vec3d(worldPoint.at<double>(0), worldPoint.at<double>(1), worldPoint.at<double>(2));
	if(!Utilities::rayPlaneIntersection(intersection, rayOrigin, rayVector, facialPose.planePoint, facialPose.planeNormal)) {
		fprintf(stderr, "MarkerTracker <%s> Failed 3d ray/plane intersection with face plane! No update to 3d marker point.\n", markerType.toString());
		return;
	}
	Mat markerMat = (Mat_<double>(3, 1) << intersection.x, intersection.y, intersection.z);
	markerMat = markerMat - facialPose.translationVector;
	markerMat = facialPose.rotationMatrix.inv() * markerMat;
	markerPoint.point3d = Point3d(markerMat.at<double>(0), markerMat.at<double>(1), markerMat.at<double>(2));
	fprintf(stderr, "MarkerTracker <%s> Recovered approximate 3D position: <%.02f, %.02f, %.02f>\n", markerType.toString(), markerPoint.point3d.x, markerPoint.point3d.y, markerPoint.point3d.z);
}

void MarkerTracker::generateMarkerCandidateList(list<MarkerCandidate> *markerCandidateList, Point2d pointOfInterest, Rect2d *boundingRect, bool debug) {
	if(markerList == NULL) {
		throw invalid_argument("MarkerTracker::generateMarkerCandidateList() called while markerList is NULL");
	}
	if(markerCandidateList == NULL) {
		throw invalid_argument("MarkerTracker::generateMarkerCandidateList() called with NULL markerCandidateList");
	}
	if(debug) {
		Mat prevFrame = frameDerivatives->getPreviewFrame();
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
		RotatedRect marker = markerSeparated.marker;
		Rect2d markerRect = Rect(marker.boundingRect2f());
		if(boundingRect == NULL || (markerRect & (*boundingRect)).area() > 0) {
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

void MarkerTracker::renderPreviewHUD(bool verbose) {
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
	Mat frame = frameDerivatives->getPreviewFrame();
	if(verbose) {
		if(trackingBoxSet) {
			rectangle(frame, trackingBox, color, 1);
		}
		if(markerDetectedSet) {
			Utilities::drawRotatedRectOutline(frame, markerDetected.marker, color, 1);
		}
	}
	if(markerPoint.set) {
		Utilities::drawX(frame, markerPoint.point, color, 10, 2);
	}
}

TrackerState MarkerTracker::getTrackerState(void) {
	return trackerState;
}

MarkerPoint MarkerTracker::getMarkerPoint(void) {
	return markerPoint;
}

vector<MarkerTracker *> MarkerTracker::markerTrackers;

vector<MarkerTracker *> *MarkerTracker::getMarkerTrackers(void) {
	return &markerTrackers;
}

MarkerTracker *MarkerTracker::getMarkerTrackerByType(MarkerType markerType) {
	size_t markerTrackersCount = markerTrackers.size();
	for(size_t i = 0; i < markerTrackersCount; i++) {
		if(markerTrackers[i]->getMarkerType().type == markerType.type) {
			return markerTrackers[i];
		}
	}
	return NULL;
}

}; //namespace YerFace
