#pragma once

#include "Metrics.hpp"
#include "Logger.hpp"
#include "Status.hpp"
#include "Utilities.hpp"
#include "FFmpegDriver.hpp"
#include "WorkerPool.hpp"

#include <list>

#include "SDL.h"

#include "opencv2/imgproc.hpp"

using namespace std;

namespace YerFace {

#define YERFACE_FRAMESERVER_MAX_QUEUEDEPTH 200

class VideoFrame;
class WorkerPool;
class WorkerPoolWorker;

#define FRAME_STATUS_MAX 9
enum WorkingFrameStatus: unsigned int {
	FRAME_STATUS_NEW = 0, //Frame has just been inserted via insertNewFrame() but no processing has taken place yet.
	FRAME_STATUS_PREPROCESS = 1, //Any tasks that should be done after memory is allocated for this frame, but before everything else.
	FRAME_STATUS_DETECTION = 2, //Primary face rectangle is being identified by FaceDetector
	FRAME_STATUS_TRACKING = 3, //Face raw landmarks and pose are being recovered by FaceTracker
	FRAME_STATUS_MAPPING = 4, //Primary markers position is being recovered by FaceMapper and its children.
	FRAME_STATUS_PREVIEW_DISPLAY = 5, //Frame preview is to be displayed. (NOTE: After this stage, ALL bitmap data associated with this frame is RELEASED!)
	FRAME_STATUS_LATE_PROCESSING = 6, //Frame is eligible for any late-stage processing (like Sphinx data or event logs).
	FRAME_STATUS_DRAINING = 7, //Last call before this frame is gone. (Output frame data.)
	FRAME_STATUS_GONE = 8 //This frame is about to be freed and purged from the frame store. (No checkpoints can be registered for this status!)
};

class WorkingFrame {
public:
	cv::Mat frame; //BGR format, at the native resolution of the input.
	cv::Mat detectionFrame; //BGR, scaled down to DetectionScaleFactor.
	double detectionScaleFactor;
	cv::Mat previewFrame; //BGR, same as the input frame, but possibly with some HUD stuff scribbled onto it.
	SDL_mutex *previewFrameMutex; //IMPORTANT - make sure you lock previewFrameMutex before WRITING TO or READING FROM previewFrame.
	FrameTimestamps frameTimestamps;

	WorkingFrameStatus status;
	unordered_map<string, bool> checkpoints[FRAME_STATUS_MAX + 1];
};

class FrameStatusChangeEventCallback {
public:
	WorkingFrameStatus newStatus;
	void *userdata;
	function<void(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps)> callback;
};

class FrameServerDrainedEventCallback {
public:
	void *userdata;
	function<void(void *userdata)> callback;
};

class FrameServer {
public:
	FrameServer(json config, Status *myStatus, bool myLowLatency);
	~FrameServer() noexcept(false);
	void setDraining(void);
	void setMirrorMode(bool myMirrorMode);
	void onFrameServerDrainedEvent(FrameServerDrainedEventCallback callback);
	void onFrameStatusChangeEvent(FrameStatusChangeEventCallback callback);
	void registerFrameStatusCheckpoint(WorkingFrameStatus status, string checkpointKey);
	void insertNewFrame(VideoFrame *videoFrame);
	WorkingFrame *getWorkingFrame(FrameNumber frameNumber);
	void setWorkingFrameStatusCheckpoint(FrameNumber frameNumber, WorkingFrameStatus status, string checkpointKey);
private:
	bool isDrained(void);
	void destroyFrame(FrameNumber frameNumber);
	void setFrameStatus(FrameTimestamps frameTimestamps, WorkingFrameStatus newStatus);
	void checkStatusValue(WorkingFrameStatus status);
	static bool workerHandler(WorkerPoolWorker *worker);
	static void workerDeinitializer(WorkerPoolWorker *worker, void *usrPtr);

	Status *status;
	bool lowLatency;
	bool draining;
	bool mirrorMode;
	int detectionBoundingBox;
	double detectionScaleFactor;
	Logger *logger;
	SDL_mutex *myMutex;
	Metrics *metrics;
	cv::Size frameSize;
	bool frameSizeSet;

	unordered_map<FrameNumber, WorkingFrame *> frameStore;

	std::vector<FrameStatusChangeEventCallback> onFrameStatusChangeCallbacks[FRAME_STATUS_MAX + 1];
	std::vector<string> statusCheckpoints[FRAME_STATUS_MAX + 1];

	std::vector<FrameServerDrainedEventCallback> onFrameServerDrainedCallbacks;

	WorkerPool *workerPool;
};

}; //namespace YerFace
