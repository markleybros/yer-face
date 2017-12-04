#pragma once

#include "Metrics.hpp"
#include "Logger.hpp"

#include "SDL.h"

#include "opencv2/imgproc.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class FrameDerivatives {
public:
	FrameDerivatives(int myClassificationBoundingBox = 320, double myClassificationScaleFactor = 0.0);
	~FrameDerivatives();
	void setWorkingFrame(Mat newFrame); //Expected to be in BGR format, at the native resolution of the input.
	Mat getWorkingFrame(void);
	void advanceWorkingFrameToCompleted(void);
	Mat getClassificationFrame(void);
	Mat getWorkingPreviewFrame(void);
	Mat getCompletedPreviewFrame(void);
	void resetCompletedPreviewFrame(void);
	double getClassificationScaleFactor(void);
	Size getWorkingFrameSize(void);
	bool getCompletedFrameSet(void);

private:
	int classificationBoundingBox;
	double classificationScaleFactor;
	Logger *logger;
	SDL_mutex *myMutex;
	Metrics *metrics;
	Mat workingFrame, completedFrame; //BGR format, at the native resolution of the input.
	bool workingFrameSet, completedFrameSet;
	Mat classificationFrame; //Grayscale, scaled down to ClassificationScaleFactor.
	Mat workingPreviewFrame, completedPreviewFrameSource, completedPreviewFrame; //BGR, same as the input frame, but possibly with some HUD stuff scribbled onto it.
	bool workingPreviewFrameSet, completedPreviewFrameSet;
	Size workingFrameSize;
	bool workingFrameSizeSet;
};

}; //namespace YerFace
