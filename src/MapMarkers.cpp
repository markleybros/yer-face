
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

	markerTrackers = MarkerTracker::getMarkerTrackers();

	new MarkerTracker(EyelidLeftTop, separateMarkers);
	new MarkerTracker(EyelidRightTop, separateMarkers);

	fprintf(stderr, "MapMarkers object constructed and ready to go!\n");
}

MapMarkers::~MapMarkers() {
	fprintf(stderr, "MapMarkers object destructing...\n");
	//Make a COPY of the vector, because otherwise it will change size out from under us while we are iterating.
	vector<MarkerTracker *> markerTrackersSnapshot = vector<MarkerTracker *>(*markerTrackers);
	size_t markerTrackersCount = markerTrackersSnapshot.size();
	for(size_t i = 0; i < markerTrackersCount; i++) {
		if(markerTrackersSnapshot[i] != NULL) {
			delete markerTrackersSnapshot[i];
		}
	}
	delete separateMarkers;
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
