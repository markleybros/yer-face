
#include "MarkerType.hpp"
#include "MarkerTracker.hpp"
#include "Utilities.hpp"

#include <list>

using namespace std;
using namespace cv;

namespace YerFace {

MarkerTracker::MarkerTracker(MarkerType myMarkerType, FrameDerivatives *myFrameDerivatives, SeparateMarkers *mySeparateMarkers, EyeTracker *myEyeTracker) {
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

	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	separateMarkers = mySeparateMarkers;
	if(separateMarkers == NULL) {
		throw invalid_argument("separateMarkers cannot be NULL");
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

	trackerState = DETECTING;
	markerDetectedSet = false;
	markerPointSet = false;

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
	transitionedToTrackingThisFrame = false;
	//Perform detection every single frame, regardless of state.
	this->performDetection();

	if((trackerState == TRACKING && !transitionedToTrackingThisFrame)) {
		this->performTracking();
		if(trackerState == LOST) {
			this->performInitializationOfTracker();
		}
	}
	return trackerState;
}

void MarkerTracker::performDetection(void) {
	markerDetectedSet = false;
	tuple<vector<RotatedRect> *, bool> separatedMarkersTuple = separateMarkers->getMarkerList();
	vector<RotatedRect> *markerList = get<0>(separatedMarkersTuple);
	bool markerListValid = get<1>(separatedMarkersTuple);
	if(!markerListValid) {
		return;
	}

	if(markerType.type == EyelidLeftTop || markerType.type == EyelidLeftBottom || markerType.type == EyelidRightTop || markerType.type == EyelidRightBottom) {
		tuple<Rect2d, bool> eyeRectTuple = eyeTracker->getEyeRect();
		Rect2d eyeRect = get<0>(eyeRectTuple);
		bool eyeRectSet = get<1>(eyeRectTuple);
		if(!eyeRectSet) {
			return;
		}
		Point2d eyeRectCenter = Utilities::centerRect(eyeRect);

		MarkerCandidate markerCandidate;
		Mat frame = frameDerivatives->getPreviewFrame();
		list<MarkerCandidate> markerCandidateList;
		size_t markerListCount = (*markerList).size();
		for(size_t i = 0; i < markerListCount; i++) {
			RotatedRect marker = (*markerList)[i];
			Rect2d markerRect = Rect(marker.boundingRect2f());
			if((markerRect & eyeRect).area() > 0) {
				markerCandidate.marker = marker;
				markerCandidate.distance = Utilities::distance(eyeRectCenter, markerCandidate.marker.center);
				markerCandidateList.push_back(markerCandidate);
			}
		}
		markerCandidateList.sort(sortMarkerCandidatesByDistance);

		if(markerCandidateList.size() == 1) {
			if(markerType.type == EyelidLeftBottom || markerType.type == EyelidRightBottom) {
				return;
			}
			markerDetected = markerCandidateList.front().marker;
			markerDetectedSet = true;
		} else if(markerCandidateList.size() > 1) {
			list<MarkerCandidate>::iterator markerCandidateIterator = markerCandidateList.begin();
			MarkerCandidate markerCandidateA = *markerCandidateIterator;
			++markerCandidateIterator;
			MarkerCandidate markerCandidateB = *markerCandidateIterator;
			if(markerCandidateB.marker.center.y < markerCandidateA.marker.center.y) {
				if(markerType.type == EyelidLeftTop || markerType.type == EyelidRightTop) {
					markerDetected = markerCandidateB.marker;
				} else {
					markerDetected = markerCandidateA.marker;
				}
			} else {
				if(markerType.type == EyelidLeftTop || markerType.type == EyelidRightTop) {
					markerDetected = markerCandidateA.marker;
				} else {
					markerDetected = markerCandidateB.marker;
				}
			}
			markerDetectedSet = true;
		} else {
			return;
		}
	}
	if(markerDetectedSet) {
		if(trackerState != TRACKING) { //FIXME -- or trackerDriftingExcessively() ?
			performInitializationOfTracker();
		}
	}
}

void MarkerTracker::performInitializationOfTracker(void) {
	if(markerDetectedSet) {
		trackerState = TRACKING;
		transitionedToTrackingThisFrame = true;
		#if (CV_MINOR_VERSION < 3)
		tracker = Tracker::create("KCF");
		#else
		tracker = TrackerKCF::create();
		#endif
		trackingBox = Rect(markerDetected.boundingRect2f());
		trackingBoxSet = true;

		tracker->init(frameDerivatives->getCurrentFrame(), trackingBox);
	}
}

void MarkerTracker::performTracking(void) {
	bool trackSuccess = tracker->update(frameDerivatives->getCurrentFrame(), trackingBox);
	if(!trackSuccess) {
		fprintf(stderr, "MarkerTracker <%s>: WARNING! Track lost. Will keep searching...\n", markerType.toString());
		trackingBoxSet = false;
		trackerState = LOST;
	} else {
		fprintf(stderr, "MarkerTracker <%s>: INFO: Track okay!\n", markerType.toString());
		trackingBoxSet = true;
	}
}

bool MarkerTracker::sortMarkerCandidatesByDistance(const MarkerCandidate a, const MarkerCandidate b) {
	return (a.distance < b.distance);
}

void MarkerTracker::renderPreviewHUD(bool verbose) {
	Scalar color = Scalar(0, 0, 255);
	if(markerType.type == EyelidLeftBottom || markerType.type == EyelidRightBottom) {
		color = Scalar(0, 255, 255);
	}
	if(markerType.type == EyelidLeftTop || markerType.type == EyelidRightTop) {
		color = Scalar(0, 127, 255);
	}
	Mat frame = frameDerivatives->getPreviewFrame();
	if(verbose) {
		if(trackingBoxSet) {
			Scalar tcolor(color[2], color[1], color[0]);
			rectangle(frame, trackingBox, tcolor, 2);
		}
		if(markerDetectedSet) {
			Utilities::drawRotatedRectOutline(frame, markerDetected, color, 1);
		}
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

}; //namespace YerFace
