#pragma once

#include <string>
#include <tuple>
#include "opencv2/objdetect.hpp"
#include "opencv2/tracking.hpp"

// #include "FrameDerivatives.hpp"
#include "TrackerState.hpp"
#include "SeparateMarkers.hpp"
#include "EyeTracker.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

enum WhichMarker {
	EyelidLeftTop,
	EyelidLeftBottom,
	EyelidRightTop,
	EyelidRightBottom,
	EyebrowLeftInner,
	EyebrowLeftMiddle,
	EyebrowLeftOuter,
	EyebrowRightInner,
	EyebrowRightMiddle,
	EyebrowRightOuter,
	CheekLeft,
	CheekRight,
	LipsLeftCorner,
	LipsLeftTop,
	LipsLeftBottom,
	LipsRightCorner,
	LipsRightTop,
	LipsRightBottom,
	Jaw
};

class MarkerTracker {
public:
	MarkerTracker(WhichMarker myWhichMarker, SeparateMarkers *mySeparateMarkers);
	~MarkerTracker();
	WhichMarker getWhichMarker(void);
	TrackerState processCurrentFrame(void);
	void renderPreviewHUD(bool verbose = true);
	TrackerState getTrackerState(void);
	tuple<Point2d, bool> getMarkerPoint(void);
	static const char *getWhichMarkerAsString(WhichMarker whichMarker);
	static vector<MarkerTracker *> *getMarkerTrackers(void);
private:
	static vector<MarkerTracker *> markerTrackers;
	WhichMarker whichMarker;
	SeparateMarkers *separateMarkers;

	TrackerState trackerState;
	Point2d markerPoint;
	bool markerPointSet;
};

}; //namespace YerFace
