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
	void setCurrentFrame(Mat newFrame); //Expected to be in BGR format, at the native resolution of the input.
	Mat getCurrentFrame(void);
	Mat getClassificationFrame(void);
	Mat getPreviewFrame(void);
	void resetPreviewFrame(void);
	double getClassificationScaleFactor(void);
	Size getCurrentFrameSize(void);

private:
	int classificationBoundingBox;
	double classificationScaleFactor;
	Logger *logger;
	SDL_mutex *myMutex;
	Metrics *metrics;
	Mat currentFrame; //BGR format, at the native resolution of the input.
	Mat classificationFrame; //Grayscale, scaled down to ClassificationScaleFactor.
	Mat previewFrame; //BGR, same as the input frame, but possibly with some HUD stuff scribbled onto it.
	bool previewFrameCloned;
};

}; //namespace YerFace
