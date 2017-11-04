
#include "MarkerTracker.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

MarkerTracker::MarkerTracker(WhichMarker myWhichMarker, SeparateMarkers *mySeparateMarkers) {
	whichMarker = myWhichMarker;
	separateMarkers = mySeparateMarkers;
	if(separateMarkers == NULL) {
		throw invalid_argument("separateMarkers cannot be NULL");
	}

	size_t markerTrackersCount = markerTrackers.size();
	for(size_t i = 0; i < markerTrackersCount; i++) {
		if(markerTrackers[i]->getWhichMarker() == whichMarker) {
			fprintf(stderr, "Trying to construct MarkerTracker <%s> object, but one already exists!\n", MarkerTracker::getWhichMarkerAsString(whichMarker));
			throw invalid_argument("WhichMarker collision trying to construct MarkerTracker");
		}
	}
	markerTrackers.push_back(this);

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
	fprintf(stderr, "MarkerTracker <%s> FIXME Stub!\n", MarkerTracker::getWhichMarkerAsString(whichMarker));
	return trackerState;
}

void MarkerTracker::renderPreviewHUD(bool verbose) {
	fprintf(stderr, "MarkerTracker <%s> FIXME Stub!\n", MarkerTracker::getWhichMarkerAsString(whichMarker));
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
