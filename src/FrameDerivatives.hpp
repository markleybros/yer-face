#pragma once

#include "opencv2/imgproc.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class FrameDerivatives {
public:
	FrameDerivatives(double myClassificationScaleFactor = 0.5);
	~FrameDerivatives();
	void setCurrentFrame(Mat newFrame); //Expected to be in BGR format, at the native resolution of the input.
	Mat getCurrentFrame(void);
	Mat getClassificationFrame(void);
	Mat getPreviewFrame(void);
	double getClassificationScaleFactor(void);

private:
	double classificationScaleFactor;
	Mat currentFrame; //BGR format, at the native resolution of the input.
	Mat classificationFrame; //Grayscale, scaled down to ClassificationScaleFactor.
	Mat previewFrame; //BGR, same as the input frame, but possibly with some HUD stuff scribbled onto it.
	bool previewFrameCloned;
};

}; //namespace YerFace
