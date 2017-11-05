#pragma once

#include "FrameDerivatives.hpp"
#include "FaceTracker.hpp"
#include "EyeTracker.hpp"
#include "MarkerTracker.hpp"
#include "MarkerSeparator.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class MarkerMapper {
public:
	MarkerMapper(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, EyeTracker *myLeftEyeTracker, EyeTracker *myRightEyeTracker);
	~MarkerMapper();
	void processCurrentFrame(void);
	void renderPreviewHUD(bool verbose = true);
private:
	FrameDerivatives *frameDerivatives;
	FaceTracker *faceTracker;
	EyeTracker *leftEyeTracker;
	EyeTracker *rightEyeTracker;
	MarkerSeparator *markerSeparator;

	MarkerTracker *markerEyelidLeftTop;
	MarkerTracker *markerEyelidRightTop;
	MarkerTracker *markerEyelidLeftBottom;
	MarkerTracker *markerEyelidRightBottom;
};

}; //namespace YerFace
