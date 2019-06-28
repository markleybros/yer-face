#pragma once

#include "Logger.hpp"
#include "Utilities.hpp"
#include "FrameServer.hpp"
#include "Metrics.hpp"
#include "WorkerPool.hpp"

#include <list>

using namespace std;

namespace YerFace {

class FaceDetectionTask {
public:
	FrameNumber myFrameNumber;
	FrameTimestamps myFrameTimestamps;
	double myDetectionScaleFactor;
	cv::Mat detectionFrame;
};

class FaceDetectorWorker;

class FacialDetectionBox {
public:
	cv::Rect2d box;
	cv::Rect2d boxNormalSize; //This is the scaled-up version to fit the native resolution of the frame.
	FrameTimestamps timestamps; //The timestamp (including frame number) to which this detection belongs.
	bool run; //Did the detector run?
	bool set; //Is the box valid?
};

class FaceDetectorAssignmentTask {
public:
	FrameNumber frameNumber;
	bool readyForAssignment;
};

class FaceDetector {
public:
	FaceDetector(json config, Status *myStatus, FrameServer *myFrameServer);
	~FaceDetector() noexcept(false);
	FacialDetectionBox getFacialDetection(FrameNumber frameNumber);
	void renderPreviewHUD(cv::Mat previewFrame, FrameNumber frameNumber, int density);
private:
	void doDetectFace(WorkerPoolWorker *worker, FaceDetectionTask task);
	static void handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps);
	static void detectionWorkerInitializer(WorkerPoolWorker *worker, void *ptr);
	static bool detectionWorkerHandler(WorkerPoolWorker *worker);
	static bool assignmentWorkerHandler(WorkerPoolWorker *worker);

	string faceDetectionModelFileName;
	double resultGoodForSeconds, faceBoxSizeAdjustment;

	bool usingDNNFaceDetection;

	Status *status;
	FrameServer *frameServer;

	Metrics *metrics, *assignmentMetrics;

	string outputPrefix;

	Logger *logger;
	
	SDL_mutex *myMutex;
	list<FaceDetectionTask> detectionTasks;

	SDL_mutex *detectionsMutex;
	unordered_map<FrameNumber, FacialDetectionBox> detections;
	FacialDetectionBox latestDetection;
	bool latestDetectionLostWarning;

	SDL_mutex *myAssignmentMutex;
	unordered_map<FrameNumber, FaceDetectorAssignmentTask> assignmentFrameNumbers;

	WorkerPool *detectionWorkerPool, *assignmentWorkerPool;
};

}; //namespace YerFace
