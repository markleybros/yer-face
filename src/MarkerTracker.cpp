
#include "MarkerTracker.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

MarkerTracker::MarkerTracker(WhichMarker myWhichMarker, SeparateMarkers *mySeparateMarkers, EyeTracker *myEyeTracker) {
	whichMarker = myWhichMarker;

	size_t markerTrackersCount = markerTrackers.size();
	for(size_t i = 0; i < markerTrackersCount; i++) {
		if(markerTrackers[i]->getWhichMarker() == whichMarker) {
			fprintf(stderr, "Trying to construct MarkerTracker <%s> object, but one already exists!\n", MarkerTracker::getWhichMarkerAsString(whichMarker));
			throw invalid_argument("WhichMarker collision trying to construct MarkerTracker");
		}
	}
	markerTrackers.push_back(this);

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
	fprintf(stderr, "MarkerTracker <%s> processCurrentFrame() FIXME Stub!\n", MarkerTracker::getWhichMarkerAsString(whichMarker));
	return trackerState;
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
