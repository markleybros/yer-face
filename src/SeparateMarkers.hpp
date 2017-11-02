#pragma once

#include "opencv2/objdetect.hpp"
#include "opencv2/imgproc.hpp"
#include "FrameDerivatives.hpp"
#include "FaceTracker.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class SeparateMarkers {
public:
	SeparateMarkers(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, float myFaceSizePercentage = 1.5);
	void setHSVThreshold(Scalar myHSVThreshold, Scalar myHSVThresholdTolerance);
	void processCurrentFrame(void);
	void doPickColor(void);
private:
	FrameDerivatives *frameDerivatives;
	FaceTracker *faceTracker;
	float faceSizePercentage;
	Scalar HSVThreshold;
	Scalar HSVThresholdTolerance;
	Mat searchFrameBGR;
	Mat searchFrameHSV;
};

}; //namespace YerFace
