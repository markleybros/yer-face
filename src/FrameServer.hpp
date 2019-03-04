#pragma once

#include "Metrics.hpp"
#include "Logger.hpp"
#include "Utilities.hpp"
#include "FFmpegDriver.hpp"

#include <list>

#include "SDL.h"

#include "opencv2/imgproc.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class VideoFrame;

#define FRAME_STATUS_MAX 5
enum WorkingFrameStatus: unsigned int {
	FRAME_STATUS_NEW = 0, //Frame has just been inserted via insertNewFrame() but no processing has taken place yet.
	FRAME_STATUS_PROCESSING = 1, //Frame is being processed by the pipeline, but not ready for previewing.
	FRAME_STATUS_PREVIEWING = 2, //Frame is being previewed.
	FRAME_STATUS_LATE_PROCESSING = 3, //Frame is eligible for any late-stage processing (like Sphinx data).
	FRAME_STATUS_DRAINING = 4, //Last call before this frame is gone. (Frame data output)
	FRAME_STATUS_GONE = 5 //This frame is about to be freed and purged from the frame store. (No checkpoints can be registered for this status!)
};

class WorkingFrame {
public:
	Mat frame; //BGR format, at the native resolution of the input.
	Mat classificationFrame; //BGR, scaled down to ClassificationScaleFactor.
	Mat previewFrame; //BGR, same as the input frame, but possibly with some HUD stuff scribbled onto it.
	SDL_mutex *previewFrameMutex; //IMPORTANT - make sure you lock previewFrameMutex before WRITING TO or READING FROM previewFrame.
	FrameTimestamps frameTimestamps;

	WorkingFrameStatus status;
	unordered_map<string, bool> checkpoints[FRAME_STATUS_MAX + 1];
};

class ClassificationFrame {
public:
	FrameTimestamps timestamps;
	Mat frame;
	double scaleFactor;
	bool set;
};

class Metrics;

class FrameServer {
public:
	FrameServer(json config, bool myLowLatency);
	~FrameServer() noexcept(false);
	void setDraining(void);
	bool isDrained(void);
	void onFrameStatusChangeEvent(WorkingFrameStatus newStatus, function<void(FrameNumber frameNumber, WorkingFrameStatus newStatus)> callback);
	void registerFrameStatusCheckpoint(WorkingFrameStatus status, string checkpointKey);
	void insertNewFrame(VideoFrame *videoFrame);
	WorkingFrame *getWorkingFrame(FrameNumber frameNumber);
	void setWorkingFrameStatusCheckpoint(FrameNumber frameNumber, WorkingFrameStatus status, string checkpointKey);
	// ClassificationFrame getClassificationFrame(void);
	// Mat getWorkingPreviewFrame(void);
	// Mat getCompletedPreviewFrame(void);
	// void resetCompletedPreviewFrame(void);
	// Size getWorkingFrameSize(void);
	// FrameTimestamps getWorkingFrameTimestamps(void);
	// FrameTimestamps getCompletedFrameTimestamps(void);
	// bool getCompletedFrameSet(void);

private:
	void destroyFrame(FrameNumber frameNumber);
	void setFrameStatus(FrameNumber frameNumber, WorkingFrameStatus newStatus);
	void checkStatusValue(WorkingFrameStatus status);
	static int frameHerderLoop(void *ptr);

	bool lowLatency;
	bool draining;
	int classificationBoundingBox;
	double classificationScaleFactor;
	Logger *logger;
	SDL_mutex *myMutex;
	Metrics *metrics;
	Size frameSize;
	bool frameSizeSet;

	bool herderRunning;
	SDL_Thread *herderThread;

	unordered_map<FrameNumber, WorkingFrame *> frameStore;

	std::vector<function<void(FrameNumber frameNumber, WorkingFrameStatus newStatus)>> onFrameStatusChangeCallbacks[FRAME_STATUS_MAX + 1];
	std::vector<string> statusCheckpoints[FRAME_STATUS_MAX + 1];
};

}; //namespace YerFace
