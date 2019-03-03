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

class WorkingFrame {
public:
	Mat frame; //BGR format, at the native resolution of the input.
	bool frameSet;
	Mat classificationFrame; //BGR, scaled down to ClassificationScaleFactor.
	Mat previewFrame; //BGR, same as the input frame, but possibly with some HUD stuff scribbled onto it.
	bool previewFrameSet;
	FrameTimestamps frameTimestamps;
};

class ClassificationFrame {
public:
	FrameTimestamps timestamps;
	Mat frame;
	double scaleFactor;
	bool set;
};

class Metrics;

class FrameDerivatives {
public:
	FrameDerivatives(json config, bool myLowLatency);
	~FrameDerivatives();
	void setWorkingFrame(VideoFrame *videoFrame);
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
	bool lowLatency;
	int classificationBoundingBox;
	double classificationScaleFactor;
	Logger *logger;
	SDL_mutex *myMutex;
	Metrics *metrics;
	Size frameSize;
	bool frameSizeSet;

	unordered_map<int, WorkingFrame> frameStore;
};

}; //namespace YerFace
