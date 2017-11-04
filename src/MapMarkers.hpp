#pragma once

#include "FrameDerivatives.hpp"
#include "FaceTracker.hpp"
#include "EyeTracker.hpp"
#include "MarkerTracker.hpp"
#include "SeparateMarkers.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class MapMarkers {
public:
	MapMarkers(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, EyeTracker *myLeftEyeTracker, EyeTracker *myRightEyeTracker);
	void processCurrentFrame(void);
	void renderPreviewHUD(bool verbose = true);
private:
	FrameDerivatives *frameDerivatives;
	FaceTracker *faceTracker;
	EyeTracker *leftEyeTracker;
	EyeTracker *rightEyeTracker;
	SeparateMarkers *separateMarkers;
	vector<MarkerTracker *> markerTrackers;
};

}; //namespace YerFace
