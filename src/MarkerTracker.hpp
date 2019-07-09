#pragma once

#include <string>
#include <cstdlib>
#include <list>

#include "Logger.hpp"
#include "SDLDriver.hpp"
#include "MarkerType.hpp"
#include "FaceMapper.hpp"
#include "FaceTracker.hpp"
#include "FrameServer.hpp"
#include "Utilities.hpp"

using namespace std;

namespace YerFace {

class MarkerPoint {
public:
	cv::Point2d point;
	cv::Point3d point3d;
	FrameTimestamps timestamp;
	bool set;
};

class FaceMapper;

class MarkerTracker {
public:
	MarkerTracker(json config, MarkerType myMarkerType, FaceMapper *myFaceMapper);
	~MarkerTracker() noexcept(false);
	MarkerType getMarkerType(void);
	void processFrame(FrameNumber frameNumber);
	void renderPreviewHUD(cv::Mat frame, FrameNumber frameNumber, int density, bool mirrorMode);
	void frameStatusNew(FrameNumber frameNumber);
	void frameStatusGone(FrameNumber frameNumber);
	MarkerPoint getMarkerPoint(FrameNumber frameNumber);
	static vector<MarkerTracker *> getMarkerTrackers(void);
	static MarkerTracker *getMarkerTrackerByType(MarkerType markerType);
private:
	void assignMarkerPoint(FrameNumber frameNumber, MarkerPoint *markerPoint);
	void calculate3dMarkerPoint(FrameNumber frameNumber, MarkerPoint *markerPoint);
	void performMarkerPointValidationAndSmoothing(WorkingFrame *workingFrame, FrameNumber frameNumber, MarkerPoint *markerPoint);
	
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
	FrameServer *frameServer;
	FaceTracker *faceTracker;

	SDL_mutex *myMutex;
	list<MarkerPoint> markerPointSmoothingBuffer;
	MarkerPoint previouslyReportedMarkerPoint;
	unordered_map<FrameNumber, MarkerPoint> markerPoints;
};

}; //namespace YerFace
