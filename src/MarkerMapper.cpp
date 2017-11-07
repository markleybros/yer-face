
#include "MarkerMapper.hpp"
#include "Utilities.hpp"
#include "opencv2/highgui.hpp"

#include <cstdlib>

using namespace std;
using namespace cv;

namespace YerFace {

MarkerMapper::MarkerMapper(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, EyeTracker *myLeftEyeTracker, EyeTracker *myRightEyeTracker) {
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

	markerSeparator = new MarkerSeparator(frameDerivatives, faceTracker);

	markerEyelidLeftTop = new MarkerTracker(EyelidLeftTop, frameDerivatives, markerSeparator, leftEyeTracker);
	markerEyelidRightTop = new MarkerTracker(EyelidRightTop, frameDerivatives, markerSeparator, rightEyeTracker);
	markerEyelidLeftBottom = new MarkerTracker(EyelidLeftBottom, frameDerivatives, markerSeparator, leftEyeTracker);
	markerEyelidRightBottom = new MarkerTracker(EyelidRightBottom, frameDerivatives, markerSeparator, rightEyeTracker);

	fprintf(stderr, "MarkerMapper object constructed and ready to go!\n");
}

MarkerMapper::~MarkerMapper() {
	fprintf(stderr, "MarkerMapper object destructing...\n");
	//Make a COPY of the vector, because otherwise it will change size out from under us while we are iterating.
	vector<MarkerTracker *> markerTrackersSnapshot = vector<MarkerTracker *>(*MarkerTracker::getMarkerTrackers());
	size_t markerTrackersCount = markerTrackersSnapshot.size();
	for(size_t i = 0; i < markerTrackersCount; i++) {
		if(markerTrackersSnapshot[i] != NULL) {
			delete markerTrackersSnapshot[i];
		}
	}
	delete markerSeparator;
}

void MarkerMapper::processCurrentFrame(void) {
	markerSeparator->processCurrentFrame();

	markerEyelidLeftTop->processCurrentFrame();
	markerEyelidRightTop->processCurrentFrame();
	markerEyelidLeftBottom->processCurrentFrame();
	markerEyelidRightBottom->processCurrentFrame();
}

void MarkerMapper::renderPreviewHUD(bool verbose) {
	if(verbose) {
		markerSeparator->renderPreviewHUD(true);
	}
	vector<MarkerTracker *> *markerTrackers = MarkerTracker::getMarkerTrackers();
	size_t markerTrackersCount = (*markerTrackers).size();
	for(size_t i = 0; i < markerTrackersCount; i++) {
		(*markerTrackers)[i]->renderPreviewHUD();
	}
}

}; //namespace YerFace
