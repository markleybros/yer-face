#include "MapMarkers.hpp"
#include "Utilities.hpp"
#include "opencv2/highgui.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

MapMarkers::MapMarkers(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, EyeTracker *myLeftEyeTracker, EyeTracker *myRightEyeTracker) {
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	faceTracker = myFaceTracker;
	if(faceTracker == NULL) {
		throw invalid_argument("faceTracker cannot be NULL");
	}
	leftEyeTracker = myLeftEyeTracker;
	if(leftEyeTracker == NULL) {
		throw invalid_argument("leftEyeTracker cannot be NULL");
	}
	rightEyeTracker = myRightEyeTracker;
	if(rightEyeTracker == NULL) {
		throw invalid_argument("rightEyeTracker cannot be NULL");
	}

	separateMarkers = new SeparateMarkers(frameDerivatives, faceTracker);
}

void MapMarkers::processCurrentFrame(void) {
	separateMarkers->processCurrentFrame();
}

void MapMarkers::renderPreviewHUD(bool verbose) {
	if(verbose) {
		separateMarkers->renderPreviewHUD(true);
	}
}

}; //namespace YerFace
