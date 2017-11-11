
#include "MarkerType.hpp"
#include "MarkerTracker.hpp"
#include "Utilities.hpp"

#include <iostream>
#include <cstdlib>

using namespace std;
using namespace cv;

namespace YerFace {

MarkerTracker::MarkerTracker(MarkerType myMarkerType, MarkerMapper *myMarkerMapper, FrameDerivatives *myFrameDerivatives, MarkerSeparator *myMarkerSeparator, EyeTracker *myEyeTracker, float myTrackingBoxPercentage, float myMaxTrackerDriftPercentage) {
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

	markerMapper = myMarkerMapper;
	if(markerMapper == NULL) {
		throw invalid_argument("markerMapper cannot be NULL");
	}
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	markerSeparator = myMarkerSeparator;
	if(markerSeparator == NULL) {
		throw invalid_argument("markerSeparator cannot be NULL");
	}
	eyeTracker = myEyeTracker;
	if(eyeTracker == NULL) {
		if(markerType.type == EyelidLeftTop || markerType.type == EyelidLeftBottom || markerType.type == EyelidRightTop || markerType.type == EyelidRightBottom) {
			throw invalid_argument("eyeTracker cannot be NULL if markerType is one of the Eyelids");
		}
	} else {
		if(markerType.type == EyelidLeftTop || markerType.type == EyelidLeftBottom) {
			if(eyeTracker->getWhichEye() != LeftEye) {
				throw invalid_argument("eyeTracker must be a LeftEye if markerType is one of the Left Eyelids");
			}
		} else if(markerType.type == EyelidRightTop || markerType.type == EyelidRightBottom) {
			if(eyeTracker->getWhichEye() != RightEye) {
				throw invalid_argument("eyeTracker must be a RightEye if markerType is one of the Right Eyelids");
			}
		} else {
			throw invalid_argument("eyeTracker should be NULL if markerType is not one of the Eyelids");
		}
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
	markerPointSet = false;
	trackingBoxSet = false;
	markerList = NULL;

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
	
	//performTrackToSeparatedCorrelation();

	if(!markerDetectedSet) {
		performDetection();
	}

	if(markerDetectedSet) {
		if(!trackingBoxSet || trackerDriftingExcessively()) {
			performInitializationOfTracker();
		}
	}
	
	assignMarkerPoint();

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
	bool eyeLineSet;
	Point2d eyeLineLeft, eyeLineRight, eyeLineCenter;
	std::tie(eyeLineLeft, eyeLineRight, eyeLineCenter, eyeLineSet) = markerMapper->getEyeLine();
	bool midLineSet;
	Point2d midLineLeft, midLineRight, midLineCenter;
	std::tie(midLineLeft, midLineRight, midLineCenter, midLineSet) = markerMapper->getMidLine();
	bool centerLineSet, centerLineIsIntermediate;
	Point2d centerLineTop, centerLineBottom;
	double centerLineSlope, centerLineIntercept;
	std::tie(centerLineTop, centerLineBottom, centerLineSlope, centerLineIntercept, centerLineIsIntermediate, centerLineSet) = markerMapper->getCenterLine();
	Size frameSize = frameDerivatives->getCurrentFrame().size();
	Rect2d boundingRect;

	if(markerType.type == EyelidLeftTop || markerType.type == EyelidLeftBottom || markerType.type == EyelidRightTop || markerType.type == EyelidRightBottom) {
		Rect2d eyeRect;
		bool eyeRectSet;
		std::tie(eyeRect, eyeRectSet) = eyeTracker->getEyeRect();
		if(!eyeRectSet) {
			return;
		}
		Point2d eyeRectCenter = Utilities::centerRect(eyeRect);

		generateMarkerCandidateList(&markerCandidateList, eyeRectCenter, &eyeRect);
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
		} else {
			return;
		}
	} else if(markerType.type == EyebrowLeftInner || markerType.type == EyebrowLeftMiddle || markerType.type == EyebrowLeftOuter || markerType.type == EyebrowRightInner || markerType.type == EyebrowRightMiddle || markerType.type == EyebrowRightOuter) {
		if(!eyeLineSet) {
			return;
		}
		xDirection = -1;
		if(markerType.type == EyebrowLeftInner || markerType.type == EyebrowLeftMiddle || markerType.type == EyebrowLeftOuter) {
			xDirection = 1;
		}

		boundingRect.y = 0;
		boundingRect.height = eyeLineCenter.y;
		if(xDirection < 0) {
			boundingRect.x = 0;
			boundingRect.width = eyeLineCenter.x;
		} else {
			boundingRect.x = eyeLineCenter.x;
			boundingRect.width = frameSize.width - eyeLineCenter.x;
		}

		if(markerType.type == EyebrowLeftInner || markerType.type == EyebrowRightInner) {
			generateMarkerCandidateList(&markerCandidateList, eyeLineCenter, &boundingRect);
			if(markerCandidateList.size() < 1) {
				return;
			}
			markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
			
			if(!claimFirstAvailableMarkerCandidate(&markerCandidateList)) {
				return;
			}
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

			Point2d eyeBrowPoint;
			bool eyeBrowPointSet;
			std::tie(eyeBrowPoint, eyeBrowPointSet) = eyebrowTracker->getMarkerPoint();
			if(!eyeBrowPointSet) {
				return;
			}

			if(xDirection < 0) {
				boundingRect.width = eyeBrowPoint.x;
			} else {
				boundingRect.x = eyeBrowPoint.x;
				boundingRect.width = frameSize.width - eyeBrowPoint.x;
			}

			generateMarkerCandidateList(&markerCandidateList, eyeBrowPoint, &boundingRect);
			if(markerCandidateList.size() < 1) {
				return;
			}
			markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
			
			if(!claimFirstAvailableMarkerCandidate(&markerCandidateList)) {
				return;
			}
		}
	} else if(markerType.type == CheekLeft || markerType.type == CheekRight) {
		if(!eyeLineSet) {
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

		Point2d eyelidPoint;
		bool eyelidPointSet;
		std::tie(eyelidPoint, eyelidPointSet) = eyelidTracker->getMarkerPoint();
		if(!eyelidPointSet) {
			return;
		}

		boundingRect.y = eyelidPoint.y;
		boundingRect.height = frameSize.height - eyelidPoint.y;
		if(xDirection < 0) {
			boundingRect.x = eyeLineRight.x;
			boundingRect.width = eyeLineCenter.x - eyeLineRight.x;
		} else {
			boundingRect.x = eyeLineCenter.x;
			boundingRect.width = eyeLineLeft.x - eyeLineCenter.x;
		}

		generateMarkerCandidateList(&markerCandidateList, eyelidPoint, &boundingRect);
		if(markerCandidateList.size() < 1) {
			return;
		}
		markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
		
		if(!claimFirstAvailableMarkerCandidate(&markerCandidateList)) {
			return;
		}
	} else if(markerType.type == Jaw) {
		if(!centerLineSet || !midLineSet) {
			return;
		}
		Point2d jawCloseTo;
		jawCloseTo.y = frameSize.height;
		jawCloseTo.x = (jawCloseTo.y - centerLineIntercept) / centerLineSlope;

		boundingRect.x = midLineRight.x;
		boundingRect.width = midLineLeft.x - midLineRight.x;
		boundingRect.y = midLineRight.y;
		boundingRect.height = jawCloseTo.y - midLineRight.y;

		generateMarkerCandidateList(&markerCandidateList, jawCloseTo, &boundingRect);
		if(markerCandidateList.size() < 1) {
			return;
		}
		markerCandidateList.sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
		
		if(!claimFirstAvailableMarkerCandidate(&markerCandidateList)) {
			return;
		}
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
	markerPointSet = false;
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
		markerPoint = detectedPoint + trackingPoint;
		markerPointSet = true;
	} else if(markerDetectedSet) {
		markerPoint = markerDetected.marker.center;
		markerPointSet = true;
	} else if(trackingBoxSet) {
		markerPoint = Utilities::centerRect(trackingBox);
		markerPointSet = true;
	} else {
		if(trackerState == TRACKING) {
			trackerState = LOST;
			fprintf(stderr, "MarkerTracker <%s> Lost marker completely! Will keep searching...\n", markerType.toString());
		}
	}	
}

void MarkerTracker::generateMarkerCandidateList(list<MarkerCandidate> *markerCandidateList, Point2d pointOfInterest, Rect2d *boundingRect) {
	if(markerList == NULL) {
		throw invalid_argument("MarkerTracker::generateMarkerCandidateList() called while markerList is NULL");
	}
	if(markerCandidateList == NULL) {
		throw invalid_argument("MarkerTracker::generateMarkerCandidateList() called with NULL markerCandidateList");
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
			markerCandidate.angleFromPointOfInterest = Utilities::lineAngle(pointOfInterest, markerCandidate.marker.center);
			markerCandidate.sqrtArea = std::sqrt((double)(markerCandidate.marker.size.width * markerCandidate.marker.size.height));
			markerCandidateList->push_back(markerCandidate);
		}
	}
}

bool MarkerTracker::sortMarkerCandidatesByDistanceFromPointOfInterest(const MarkerCandidate a, const MarkerCandidate b) {
	return (a.distanceFromPointOfInterest < b.distanceFromPointOfInterest);
}

bool MarkerTracker::sortMarkerCandidatesByAngleFromPointOfInterest(const MarkerCandidate a, const MarkerCandidate b) {
	return (a.angleFromPointOfInterest < b.angleFromPointOfInterest);
}

bool MarkerTracker::sortMarkerCandidatesByAngleFromPointOfInterestInverted(const MarkerCandidate a, const MarkerCandidate b) {
	return (a.angleFromPointOfInterest > b.angleFromPointOfInterest);
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
	if(markerPointSet) {
		Utilities::drawX(frame, markerPoint, color, 10, 2);
	}
}

TrackerState MarkerTracker::getTrackerState(void) {
	return trackerState;
}

tuple<Point2d, bool> MarkerTracker::getMarkerPoint(void) {
	return make_tuple(markerPoint, markerPointSet);
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
