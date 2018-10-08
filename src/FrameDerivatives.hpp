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

class FrameTimestamps {
public:
	double startTimestamp;
	double estimatedEndTimestamp;
	signed long frameNumber;
	bool set;
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
	FrameDerivatives(json config);
	~FrameDerivatives();
	void setWorkingFrame(VideoFrame *videoFrame);
	Mat getWorkingFrame(void);
	Mat getCompletedFrame(void);
	void advanceWorkingFrameToCompleted(void);
	ClassificationFrame getClassificationFrame(void);
	Mat getWorkingPreviewFrame(void);
	Mat getCompletedPreviewFrame(void);
	void resetCompletedPreviewFrame(void);
	Size getWorkingFrameSize(void);
	FrameTimestamps getWorkingFrameTimestamps(void);
	FrameTimestamps getCompletedFrameTimestamps(void);
	bool getCompletedFrameSet(void);

private:
	int classificationBoundingBox;
	double classificationScaleFactor;
	Logger *logger;
	SDL_mutex *myMutex;
	Metrics *metrics;
	Mat workingFrame, completedFrame; //BGR format, at the native resolution of the input.
	bool workingFrameSet, completedFrameSet;
	Mat classificationFrame; //BGR, scaled down to ClassificationScaleFactor.
	Mat workingPreviewFrame, completedPreviewFrameSource, completedPreviewFrame; //BGR, same as the input frame, but possibly with some HUD stuff scribbled onto it.
	bool workingPreviewFrameSet, completedPreviewFrameSet;
	Size workingFrameSize;
	bool workingFrameSizeSet;
	FrameTimestamps workingFrameTimestamps;
	FrameTimestamps completedFrameTimestamps;
};

}; //namespace YerFace
