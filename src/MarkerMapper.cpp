
#include "MarkerMapper.hpp"
#include "Utilities.hpp"
#include "opencv2/highgui.hpp"

#include <cstdlib>

using namespace std;
using namespace cv;

namespace YerFace {

MarkerMapper::MarkerMapper(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, EyeTracker *myLeftEyeTracker, EyeTracker *myRightEyeTracker, float myEyelidBottomPointWeight, float myEyeLineLengthPercentage) {
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
	eyelidBottomPointWeight = myEyelidBottomPointWeight;
	if(eyelidBottomPointWeight < 0.0 || eyelidBottomPointWeight > 1.0) {
		throw invalid_argument("eyelidBottomPointWeight cannot be less than 0.0 or greater than 1.0");
	}
	eyeLineLengthPercentage = myEyeLineLengthPercentage;
	if(eyeLineLengthPercentage <= 0.0) {
		throw invalid_argument("eyeLineLengthPercentage cannot be less than or equal to 0.0");
	}

	eyeLineSet = false;

	markerSeparator = new MarkerSeparator(frameDerivatives, faceTracker);

	markerEyelidLeftTop = new MarkerTracker(EyelidLeftTop, this, frameDerivatives, markerSeparator, leftEyeTracker);
	markerEyelidRightTop = new MarkerTracker(EyelidRightTop, this, frameDerivatives, markerSeparator, rightEyeTracker);
	markerEyelidLeftBottom = new MarkerTracker(EyelidLeftBottom, this, frameDerivatives, markerSeparator, leftEyeTracker);
	markerEyelidRightBottom = new MarkerTracker(EyelidRightBottom, this, frameDerivatives, markerSeparator, rightEyeTracker);

	markerEyebrowLeftInner = new MarkerTracker(EyebrowLeftInner, this, frameDerivatives, markerSeparator);
	markerEyebrowRightInner = new MarkerTracker(EyebrowRightInner, this, frameDerivatives, markerSeparator);
	markerEyebrowLeftMiddle = new MarkerTracker(EyebrowLeftMiddle, this, frameDerivatives, markerSeparator);
	markerEyebrowRightMiddle = new MarkerTracker(EyebrowRightMiddle, this, frameDerivatives, markerSeparator);
	markerEyebrowLeftOuter = new MarkerTracker(EyebrowLeftOuter, this, frameDerivatives, markerSeparator);
	markerEyebrowRightOuter = new MarkerTracker(EyebrowRightOuter, this, frameDerivatives, markerSeparator);

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

	calculateEyeLine();

	markerEyebrowLeftInner->processCurrentFrame();
	markerEyebrowRightInner->processCurrentFrame();
	markerEyebrowLeftMiddle->processCurrentFrame();
	markerEyebrowRightMiddle->processCurrentFrame();
	markerEyebrowLeftOuter->processCurrentFrame();
	markerEyebrowRightOuter->processCurrentFrame();
}

void MarkerMapper::renderPreviewHUD(bool verbose) {
	if(verbose) {
		markerSeparator->renderPreviewHUD(true);
	}
	vector<MarkerTracker *> *markerTrackers = MarkerTracker::getMarkerTrackers();
	size_t markerTrackersCount = (*markerTrackers).size();
	for(size_t i = 0; i < markerTrackersCount; i++) {
		(*markerTrackers)[i]->renderPreviewHUD(verbose);
	}
	if(eyeLineSet) {
		Mat frame = frameDerivatives->getPreviewFrame();
		line(frame, eyeLineLeft, eyeLineRight, Scalar(0, 255, 0), 2);
	}
}

tuple<Point2d, Point2d, bool> MarkerMapper::getEyeLine(void) {
	return std::make_tuple(eyeLineLeft, eyeLineRight, eyeLineSet);
}

void MarkerMapper::calculateEyeLine(void) {
	eyeLineSet = false;
	if(!calculateEyeCenter(markerEyelidLeftTop, markerEyelidLeftBottom, &eyeLineLeft)) {
		return;
	}
	if(!calculateEyeCenter(markerEyelidRightTop, markerEyelidRightBottom, &eyeLineRight)) {
		return;
	}

	double originalEyeLineLength = Utilities::distance(eyeLineLeft, eyeLineRight);
	double desiredEyeLineLength = originalEyeLineLength * eyeLineLengthPercentage;
	Point2d eyeLineCenter = (eyeLineLeft + eyeLineRight);
	eyeLineCenter.x = eyeLineCenter.x / 2.0;
	eyeLineCenter.y = eyeLineCenter.y / 2.0;

	eyeLineLeft = Utilities::adjustLineDistance(eyeLineCenter, eyeLineLeft, (desiredEyeLineLength / 2.0));
	eyeLineRight = Utilities::adjustLineDistance(eyeLineCenter, eyeLineRight, (desiredEyeLineLength / 2.0));
	eyeLineSet = true;
}

bool MarkerMapper::calculateEyeCenter(MarkerTracker *top, MarkerTracker *bottom, Point2d *center) {
	Point2d topPoint;
	bool topPointSet;
	std::tie(topPoint, topPointSet) = top->getMarkerPoint();
	if(!topPointSet) {
		return false;
	}
	Point2d bottomPoint;
	bool bottomPointSet;
	std::tie(bottomPoint, bottomPointSet) = bottom->getMarkerPoint();
	if(!bottomPointSet) {
		return false;
	}
	double bottomPointWeight = eyelidBottomPointWeight;
	bottomPoint.x = bottomPoint.x * bottomPointWeight;
	bottomPoint.y = bottomPoint.y * bottomPointWeight;
	topPoint.x = topPoint.x * (1.0 - bottomPointWeight);
	topPoint.y = topPoint.y * (1.0 - bottomPointWeight);
	*center = bottomPoint + topPoint;
	return true;
}

}; //namespace YerFace
