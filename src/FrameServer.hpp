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
	FRAME_STATUS_GONE = 5 //This frame is about to be freed and purged from the frame store.
};

class WorkingFrame {
public:
	Mat frame; //BGR format, at the native resolution of the input.
	bool frameSet;
	Mat classificationFrame; //BGR, scaled down to ClassificationScaleFactor.
	Mat previewFrame; //BGR, same as the input frame, but possibly with some HUD stuff scribbled onto it.
	bool previewFrameSet;
	FrameTimestamps frameTimestamps;

	WorkingFrameStatus status;
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
	~FrameServer();
	void onFrameStatusChangeEvent(WorkingFrameStatus newStatus, function<void(signed long frameNumber)> callback);
	void insertNewFrame(VideoFrame *videoFrame);
	// Mat getWorkingFrame(void);
	// Mat getCompletedFrame(void);
	// void advanceWorkingFrameToCompleted(void);
	// ClassificationFrame getClassificationFrame(void);
	// Mat getWorkingPreviewFrame(void);
	// Mat getCompletedPreviewFrame(void);
	// void resetCompletedPreviewFrame(void);
	// Size getWorkingFrameSize(void);
	// FrameTimestamps getWorkingFrameTimestamps(void);
	// FrameTimestamps getCompletedFrameTimestamps(void);
	// bool getCompletedFrameSet(void);

private:
	void setFrameStatus(signed long frameNumber, WorkingFrameStatus newStatus);

	bool lowLatency;
	int classificationBoundingBox;
	double classificationScaleFactor;
	Logger *logger;
	SDL_mutex *myMutex;
	Metrics *metrics;
	Size frameSize;
	bool frameSizeSet;

	unordered_map<signed long, WorkingFrame> frameStore;

	std::vector<function<void(signed long frameNumber)>> onFrameStatusChangeCallbacks[FRAME_STATUS_MAX + 1];
};

}; //namespace YerFace
