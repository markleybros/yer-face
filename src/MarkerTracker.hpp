#pragma once

#include <string>
#include <tuple>
#include "opencv2/objdetect.hpp"
#include "opencv2/tracking.hpp"

#include "FrameDerivatives.hpp"
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

class MarkerCandidate {
public:
	RotatedRect marker;
	double distance;
};

class MarkerTracker {
public:
	MarkerTracker(WhichMarker myWhichMarker, FrameDerivatives *myFrameDerivatives, SeparateMarkers *mySeparateMarkers, EyeTracker *myEyeTracker = NULL);
	~MarkerTracker();
	WhichMarker getWhichMarker(void);
	TrackerState processCurrentFrame(void);
	void renderPreviewHUD(bool verbose = true);
	TrackerState getTrackerState(void);
	tuple<Point2d, bool> getMarkerPoint(void);
	static const char *getWhichMarkerAsString(WhichMarker whichMarker);
	static vector<MarkerTracker *> *getMarkerTrackers(void);
	static bool sortMarkerCandidatesByDistance(const MarkerCandidate a, const MarkerCandidate b);
private:
	void performDetection(void);
	void performInitializationOfTracker(void);
	void performTracking(void);

	static vector<MarkerTracker *> markerTrackers;
	WhichMarker whichMarker;
	FrameDerivatives *frameDerivatives;
	SeparateMarkers *separateMarkers;
	EyeTracker *eyeTracker;
	Ptr<Tracker> tracker;

	bool transitionedToTrackingThisFrame;
	TrackerState trackerState;
	RotatedRect markerDetected;
	bool markerDetectedSet;
	Rect2d trackingBox;
	bool trackingBoxSet;
	Point2d markerPoint;
	bool markerPointSet;
};

}; //namespace YerFace
