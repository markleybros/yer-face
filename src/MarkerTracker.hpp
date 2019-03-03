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
#include "FrameServer.hpp"
#include "Utilities.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class MarkerPoint {
public:
	Point2d point;
	Point3d point3d;
	double timestamp;
	bool set;
};

class MarkerTrackerWorkingVariables {
public:
	MarkerPoint markerPoint;
	MarkerPoint previouslyReportedMarkerPoint;
};

class FaceMapper;

class MarkerTracker {
public:
	MarkerTracker(json config, MarkerType myMarkerType, FaceMapper *myFaceMapper);
	~MarkerTracker() noexcept(false);
	MarkerType getMarkerType(void);
	void processCurrentFrame(void);
	void advanceWorkingToCompleted(void);
	void renderPreviewHUD(void);
	MarkerPoint getCompletedMarkerPoint(void);
	static vector<MarkerTracker *> getMarkerTrackers(void);
	static MarkerTracker *getMarkerTrackerByType(MarkerType markerType);
private:
	void assignMarkerPoint(void);
	void calculate3dMarkerPoint(void);
	void performMarkerPointValidationAndSmoothing(void);
	
	static vector<MarkerTracker *> markerTrackers;
	static SDL_mutex *myStaticMutex;

	MarkerType markerType;
	FaceMapper *faceMapper;
	double pointSmoothingOverSeconds;
	double pointSmoothingExponent;
	double pointMotionLowRejectionThreshold;
	double pointMotionHighRejectionThreshold;
	double markerRejectionResetAfterSeconds;

	Logger *logger;
	SDL_mutex *myCmpMutex;
	SDLDriver *sdlDriver;
	FrameServer *frameServer;
	FaceTracker *faceTracker;

	list<MarkerPoint> markerPointSmoothingBuffer;

	MarkerTrackerWorkingVariables working, complete;
};

}; //namespace YerFace
