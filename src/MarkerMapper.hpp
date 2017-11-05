#pragma once

#include "FrameDerivatives.hpp"
#include "FaceTracker.hpp"
#include "EyeTracker.hpp"
#include "MarkerTracker.hpp"
#include "MarkerSeparator.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class MapMarkers {
public:
	MapMarkers(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, EyeTracker *myLeftEyeTracker, EyeTracker *myRightEyeTracker);
	~MapMarkers();
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
