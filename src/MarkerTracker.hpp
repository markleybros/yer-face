#pragma once

#include <string>
#include <cstdlib>
#include <list>

#include "opencv2/objdetect.hpp"
#include "opencv2/tracking.hpp"

#include "Logger.hpp"
#include "SDLDriver.hpp"
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
	double timestamp;
	bool set;
};

class MarkerTrackerWorkingVariables {
public:
	MarkerCandidate markerDetected;
	bool markerDetectedSet;
	Rect2d trackingBox;
	bool trackingBoxSet;
	MarkerPoint markerPoint;
	MarkerPoint previouslyReportedMarkerPoint;
};

class FaceMapper;

class MarkerTracker {
public:
	MarkerTracker(MarkerType myMarkerType, FaceMapper *myFaceMapper, bool myPerformOpticalTracking = true, double myTrackingBoxPercentage = 1.5, double myMaxTrackerDriftPercentage = 0.75, double myPointSmoothingOverSeconds = 0.1, double myPointSmoothingExponent = 3);
	~MarkerTracker() noexcept(false);
	MarkerType getMarkerType(void);
	TrackerState processCurrentFrame(void);
	void advanceWorkingToCompleted(void);
	void renderPreviewHUD(void);
	TrackerState getTrackerState(void);
	MarkerPoint getWorkingMarkerPoint(void);
	MarkerPoint getCompletedMarkerPoint(void);
	static vector<MarkerTracker *> getMarkerTrackers(void);
	static MarkerTracker *getMarkerTrackerByType(MarkerType markerType);
	static bool sortMarkerCandidatesByDistanceFromPointOfInterest(const MarkerCandidate a, const MarkerCandidate b);
private:
	void performTrackToSeparatedCorrelation(void);
	void performDetection(void);
	void performInitializationOfTracker(void);
	bool performTracking(void);
	bool trackerDriftingExcessively(void);
	bool claimMarkerCandidate(MarkerCandidate markerCandidate, double setExclusionRadius = 0.0);
	bool claimFirstAvailableMarkerCandidate(list<MarkerCandidate> *markerCandidateList, double setExclusionRadius = 0.0);
	void assignMarkerPoint(void);
	void calculate3dMarkerPoint(void);
	void performMarkerPointSmoothing(void);
	void generateMarkerCandidateList(list<MarkerCandidate> *markerCandidateList, Point2d pointOfInterest, Rect2d *boundingRect = NULL, double proposedExclusionRadius = 0.0, bool overrideExclusionZone = false, bool debug = false);
	
	static vector<MarkerTracker *> markerTrackers;
	static SDL_mutex *myStaticMutex;

	MarkerType markerType;
	FaceMapper *faceMapper;
	bool performOpticalTracking;
	double trackingBoxPercentage;
	double maxTrackerDriftPercentage;
	double pointSmoothingOverSeconds;
	double pointSmoothingExponent;

	Logger *logger;
	SDL_mutex *myWrkMutex, *myCmpMutex;
	SDLDriver *sdlDriver;
	FrameDerivatives *frameDerivatives;
	MarkerSeparator *markerSeparator;
	FaceTracker *faceTracker;

	list<MarkerPoint> markerPointSmoothingBuffer;

	Ptr<Tracker> tracker;
	vector<MarkerSeparated> *markerList;
	TrackerState trackerState;

	MarkerTrackerWorkingVariables working, complete;
};

}; //namespace YerFace
