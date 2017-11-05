
#include "MarkerTracker.hpp"
#include "Utilities.hpp"

#include <list>

using namespace std;
using namespace cv;

namespace YerFace {

MarkerTracker::MarkerTracker(WhichMarker myWhichMarker, FrameDerivatives *myFrameDerivatives, SeparateMarkers *mySeparateMarkers, EyeTracker *myEyeTracker) {
	whichMarker = myWhichMarker;

	size_t markerTrackersCount = markerTrackers.size();
	for(size_t i = 0; i < markerTrackersCount; i++) {
		if(markerTrackers[i]->getWhichMarker() == whichMarker) {
			fprintf(stderr, "Trying to construct MarkerTracker <%s> object, but one already exists!\n", MarkerTracker::getWhichMarkerAsString(whichMarker));
			throw invalid_argument("WhichMarker collision trying to construct MarkerTracker");
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
		if(whichMarker == EyelidLeftTop || whichMarker == EyelidLeftBottom || whichMarker == EyelidRightTop || whichMarker == EyelidRightBottom) {
			throw invalid_argument("eyeTracker cannot be NULL if whichMarker is one of the Eyelids");
		}
	} else {
		if(whichMarker == EyelidLeftTop || whichMarker == EyelidLeftBottom) {
			if(eyeTracker->getWhichEye() != LeftEye) {
				throw invalid_argument("eyeTracker must be a LeftEye if whichMarker is one of the Left Eyelids");
			}
		} else if(whichMarker == EyelidRightTop || whichMarker == EyelidRightBottom) {
			if(eyeTracker->getWhichEye() != RightEye) {
				throw invalid_argument("eyeTracker must be a RightEye if whichMarker is one of the Right Eyelids");
			}
		} else {
			throw invalid_argument("eyeTracker should be NULL if whichMarker is not one of the Eyelids");
		}
	}

	trackerState = DETECTING;
	markerPointSet = false;

	fprintf(stderr, "MarkerTracker <%s> object constructed and ready to go!\n", MarkerTracker::getWhichMarkerAsString(whichMarker));
}

MarkerTracker::~MarkerTracker() {
	fprintf(stderr, "MarkerTracker <%s> object destructing...\n", MarkerTracker::getWhichMarkerAsString(whichMarker));
	for(vector<MarkerTracker *>::iterator iterator = markerTrackers.begin(); iterator != markerTrackers.end(); ++iterator) {
		if(*iterator == this) {
			markerTrackers.erase(iterator);
			return;
		}
	}
}

WhichMarker MarkerTracker::getWhichMarker(void) {
	return whichMarker;
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
	tuple<vector<RotatedRect> *, bool> separatedMarkersTuple = separateMarkers->getMarkerList();
	vector<RotatedRect> *markerList = get<0>(separatedMarkersTuple);
	bool markerListValid = get<1>(separatedMarkersTuple);
	if(!markerListValid) {
		return;
	}

	if(whichMarker == EyelidLeftTop || whichMarker == EyelidLeftBottom || whichMarker == EyelidRightTop || whichMarker == EyelidRightBottom) {
		tuple<Rect2d, bool> eyeRectTuple = eyeTracker->getEyeRect();
		Rect2d eyeRect = get<0>(eyeRectTuple);
		bool eyeRectSet = get<1>(eyeRectTuple);
		if(!eyeRectSet) {
			return;
		}
		Point2d eyeRectCenter = Utilities::centerRect(eyeRect);

		//LOOP THROUGH THE separatedMarkers LOOKING FOR MARKERS THAT ARE WITHIN eyeRect
		Mat frame = frameDerivatives->getPreviewFrame();
		list<MarkerCandidate> markerCandidateList;
		size_t markerListCount = (*markerList).size();
		for(size_t i = 0; i < markerListCount; i++) {
			RotatedRect marker = (*markerList)[i];
			Rect2d markerRect = Rect(marker.boundingRect2f());
			if((markerRect & eyeRect).area() > 0) {
				MarkerCandidate markerCandidate;
				markerCandidate.marker = marker;
				markerCandidate.distance = Utilities::distance(eyeRectCenter, markerCandidate.marker.center);
				markerCandidateList.push_back(markerCandidate);
				Utilities::drawRotatedRectOutline(frame, markerCandidate.marker);
			}
		}

		if(markerCandidateList.size() < 1) {
			return;
		}

		markerCandidateList.sort(sortMarkerCandidatesByDistance);

		line(frame, eyeRectCenter, markerCandidateList.front().marker.center, Scalar(0, 0, 255), 1);
		fprintf(stderr, "MarkerTracker <%s> distance to candidate %d: %.02f\n", MarkerTracker::getWhichMarkerAsString(whichMarker), 0, markerCandidateList.front().distance);
	}
}

void MarkerTracker::performInitializationOfTracker(void) {

}

void MarkerTracker::performTracking(void) {

}

bool MarkerTracker::sortMarkerCandidatesByDistance(const MarkerCandidate a, const MarkerCandidate b) {
	return (a.distance < b.distance);
}

void MarkerTracker::renderPreviewHUD(bool verbose) {
	fprintf(stderr, "MarkerTracker <%s> renderPreviewHUD() FIXME Stub!\n", MarkerTracker::getWhichMarkerAsString(whichMarker));
	return;
}

TrackerState MarkerTracker::getTrackerState(void) {
	return trackerState;
}

tuple<Point2d, bool> MarkerTracker::getMarkerPoint(void) {
	return make_tuple(markerPoint, markerPointSet);
}

const char *MarkerTracker::getWhichMarkerAsString(WhichMarker whichMarker) {
	switch(whichMarker) {
		default:
			return "Unknown!";
		case EyelidLeftTop:
			return "EyelidLeftTop";
		case EyelidLeftBottom:
			return "EyelidLeftBottom";
		case EyelidRightTop:
			return "EyelidRightTop";
		case EyelidRightBottom:
			return "EyelidRightBottom";
		case EyebrowLeftInner:
			return "EyebrowLeftInner";
		case EyebrowLeftMiddle:
			return "EyebrowLeftMiddle";
		case EyebrowLeftOuter:
			return "EyebrowLeftOuter";
		case EyebrowRightInner:
			return "EyebrowRightInner";
		case EyebrowRightMiddle:
			return "EyebrowRightMiddle";
		case EyebrowRightOuter:
			return "EyebrowRightOuter";
		case CheekLeft:
			return "CheekLeft";
		case CheekRight:
			return "CheekRight";
		case LipsLeftCorner:
			return "LipsLeftCorner";
		case LipsLeftTop:
			return "LipsLeftTop";
		case LipsLeftBottom:
			return "LipsLeftBottom";
		case LipsRightCorner:
			return "LipsRightCorner";
		case LipsRightTop:
			return "LipsRightTop";
		case LipsRightBottom:
			return "LipsRightBottom";
		case Jaw:
			return "Jaw";
	}
}

vector<MarkerTracker *> MarkerTracker::markerTrackers;

vector<MarkerTracker *> *MarkerTracker::getMarkerTrackers(void) {
	return &markerTrackers;
}

}; //namespace YerFace
