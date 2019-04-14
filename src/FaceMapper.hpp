#pragma once

#include "Logger.hpp"
#include "SDLDriver.hpp"
#include "FrameServer.hpp"
#include "FaceTracker.hpp"
#include "MarkerTracker.hpp"
#include "Metrics.hpp"
#include "Status.hpp"
#include "PreviewHUD.hpp"

using namespace std;

namespace YerFace {

class MarkerTracker;

class FaceMapperPendingFrame {
public:
	FrameNumber frameNumber;
	bool hasEnteredMapping;
	bool hasCompletedMapping;
};

class FaceMapper {
public:
	FaceMapper(json config, Status *myStatus, FrameServer *myFrameServer, FaceTracker *myFaceTracker, PreviewHUD *myPreviewHUD);
	~FaceMapper() noexcept(false);
	void renderPreviewHUD(cv::Mat frame, FrameNumber frameNumber, int density);
	FrameServer *getFrameServer(void);
	FaceTracker *getFaceTracker(void);
private:
	static void handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps);
	static bool workerHandler(WorkerPoolWorker *worker);

	Status *status;
	FrameServer *frameServer;
	FaceTracker *faceTracker;
	PreviewHUD *previewHUD;

	Logger *logger;
	Metrics *metrics;

	MarkerTracker *markerEyelidLeftTop;
	MarkerTracker *markerEyelidRightTop;
	MarkerTracker *markerEyelidLeftBottom;
	MarkerTracker *markerEyelidRightBottom;

	MarkerTracker *markerEyebrowLeftInner;
	MarkerTracker *markerEyebrowLeftMiddle;
	MarkerTracker *markerEyebrowLeftOuter;
	MarkerTracker *markerEyebrowRightInner;
	MarkerTracker *markerEyebrowRightMiddle;
	MarkerTracker *markerEyebrowRightOuter;

	MarkerTracker *markerJaw;

	MarkerTracker *markerLipsLeftCorner;
	MarkerTracker *markerLipsRightCorner;

	MarkerTracker *markerLipsLeftTop;
	MarkerTracker *markerLipsRightTop;
	
	MarkerTracker *markerLipsLeftBottom;
	MarkerTracker *markerLipsRightBottom;

	std::vector<MarkerTracker *> trackers;

	SDL_mutex *myMutex;
	std::unordered_map<FrameNumber, FaceMapperPendingFrame> pendingFrames;
	WorkerPool *workerPool;
};

}; //namespace YerFace
