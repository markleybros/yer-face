#pragma once

#include <string>
#include <cstdlib>
#include <list>

#include "opencv2/objdetect.hpp"
#include "opencv2/tracking.hpp"

#include "MarkerType.hpp"
#include "FaceMapper.hpp"
#include "FaceTracker.hpp"
#include "FrameDerivatives.hpp"
#include "TrackerState.hpp"
#include "MarkerSeparator.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class MarkerCandidate {
public:
	RotatedRect marker;
	unsigned int markerListIndex;
	double distanceFromPointOfInterest;
	double sqrtArea;
};

class MarkerPoint {
public:
	Point2d point;
	Point3d point3d;
	bool set;
};

class FaceMapper;

class MarkerTracker {
public:
	MarkerTracker(MarkerType myMarkerType, FaceMapper *myFaceMapper, float myTrackingBoxPercentage = 1.5, float myMaxTrackerDriftPercentage = 0.75);
	~MarkerTracker();
	MarkerType getMarkerType(void);
	TrackerState processCurrentFrame(void);
	void renderPreviewHUD(bool verbose = true);
	TrackerState getTrackerState(void);
	MarkerPoint getMarkerPoint(void);
	static vector<MarkerTracker *> *getMarkerTrackers(void);
	static MarkerTracker *getMarkerTrackerByType(MarkerType markerType);
	static bool sortMarkerCandidatesByDistanceFromPointOfInterest(const MarkerCandidate a, const MarkerCandidate b);
private:
	void performTrackToSeparatedCorrelation(void);
	void performDetection(void);
	void performInitializationOfTracker(void);
	bool performTracking(void);
	bool trackerDriftingExcessively(void);
	bool claimMarkerCandidate(MarkerCandidate markerCandidate);
	bool claimFirstAvailableMarkerCandidate(list<MarkerCandidate> *markerCandidateList);
	void assignMarkerPoint(void);
	void calculate3dMarkerPoint(void);
	void generateMarkerCandidateList(list<MarkerCandidate> *markerCandidateList, Point2d pointOfInterest, Rect2d *boundingRect = NULL, bool debug = false);
	
	static vector<MarkerTracker *> markerTrackers;

	MarkerType markerType;
	FaceMapper *faceMapper;
	float trackingBoxPercentage;
	float maxTrackerDriftPercentage;

	FrameDerivatives *frameDerivatives;
	MarkerSeparator *markerSeparator;
	FaceTracker *faceTracker;

	Ptr<Tracker> tracker;
	vector<MarkerSeparated> *markerList;
	TrackerState trackerState;
	MarkerCandidate markerDetected;
	bool markerDetectedSet;
	Rect2d trackingBox;
	bool trackingBoxSet;
	MarkerPoint markerPoint;
};

}; //namespace YerFace
