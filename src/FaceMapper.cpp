
#include "FaceMapper.hpp"
#include "Utilities.hpp"
#include "opencv2/calib3d.hpp"
#include "opencv2/highgui.hpp"

#include <cstdlib>

using namespace std;
using namespace cv;

namespace YerFace {

FaceMapper::FaceMapper(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker) {
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	faceTracker = myFaceTracker;
	if(faceTracker == NULL) {
		throw invalid_argument("faceTracker cannot be NULL");
	}

	markerSeparator = new MarkerSeparator(frameDerivatives, faceTracker);

	markerEyelidLeftTop = new MarkerTracker(EyelidLeftTop, this);
	markerEyelidRightTop = new MarkerTracker(EyelidRightTop, this);
	markerEyelidLeftBottom = new MarkerTracker(EyelidLeftBottom, this);
	markerEyelidRightBottom = new MarkerTracker(EyelidRightBottom, this);

	markerEyebrowLeftInner = new MarkerTracker(EyebrowLeftInner, this);
	markerEyebrowRightInner = new MarkerTracker(EyebrowRightInner, this);
	markerEyebrowLeftMiddle = new MarkerTracker(EyebrowLeftMiddle, this);
	markerEyebrowRightMiddle = new MarkerTracker(EyebrowRightMiddle, this);
	markerEyebrowLeftOuter = new MarkerTracker(EyebrowLeftOuter, this);
	markerEyebrowRightOuter = new MarkerTracker(EyebrowRightOuter, this);

	markerCheekLeft = new MarkerTracker(CheekLeft, this);
	markerCheekRight = new MarkerTracker(CheekRight, this);
	
	markerJaw = new MarkerTracker(Jaw, this);

	markerLipsLeftCorner = new MarkerTracker(LipsLeftCorner, this);
	markerLipsRightCorner = new MarkerTracker(LipsRightCorner, this);

	markerLipsLeftTop = new MarkerTracker(LipsLeftTop, this);
	markerLipsRightTop = new MarkerTracker(LipsRightTop, this);

	markerLipsLeftBottom = new MarkerTracker(LipsLeftBottom, this);
	markerLipsRightBottom = new MarkerTracker(LipsRightBottom, this);
	
	fprintf(stderr, "FaceMapper object constructed and ready to go!\n");
}

FaceMapper::~FaceMapper() {
	fprintf(stderr, "FaceMapper object destructing...\n");
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

void FaceMapper::processCurrentFrame(void) {
	facialFeatures = faceTracker->getFacialFeatures();
	calculateEyeRects();

	markerSeparator->processCurrentFrame();

	markerEyelidLeftTop->processCurrentFrame();
	markerEyelidRightTop->processCurrentFrame();
	markerEyelidLeftBottom->processCurrentFrame();
	markerEyelidRightBottom->processCurrentFrame();

	markerEyebrowLeftInner->processCurrentFrame();
	markerEyebrowRightInner->processCurrentFrame();
	markerEyebrowLeftMiddle->processCurrentFrame();
	markerEyebrowRightMiddle->processCurrentFrame();
	markerEyebrowLeftOuter->processCurrentFrame();
	markerEyebrowRightOuter->processCurrentFrame();

	markerCheekLeft->processCurrentFrame();
	markerCheekRight->processCurrentFrame();

	markerJaw->processCurrentFrame();

	markerLipsLeftCorner->processCurrentFrame();
	markerLipsRightCorner->processCurrentFrame();

	markerLipsLeftTop->processCurrentFrame();
	markerLipsRightTop->processCurrentFrame();

	markerLipsLeftBottom->processCurrentFrame();
	markerLipsRightBottom->processCurrentFrame();
}

void FaceMapper::renderPreviewHUD(bool verbose) {
	Mat frame = frameDerivatives->getPreviewFrame();
	if(verbose) {
		markerSeparator->renderPreviewHUD(true);

		if(leftEyeRect.set) {
			rectangle(frame, leftEyeRect.rect, Scalar(0, 0, 255));
		}
		if(rightEyeRect.set) {
			rectangle(frame, rightEyeRect.rect, Scalar(0, 0, 255));
		}
	}
	vector<MarkerTracker *> *markerTrackers = MarkerTracker::getMarkerTrackers();
	size_t markerTrackersCount = (*markerTrackers).size();
	for(size_t i = 0; i < markerTrackersCount; i++) {
		(*markerTrackers)[i]->renderPreviewHUD(verbose);
	}
}

FrameDerivatives *FaceMapper::getFrameDerivatives(void) {
	return frameDerivatives;
}

FaceTracker *FaceMapper::getFaceTracker(void) {
	return faceTracker;
}

MarkerSeparator *FaceMapper::getMarkerSeparator(void) {
	return markerSeparator;
}

EyeRect FaceMapper::getLeftEyeRect(void) {
	return leftEyeRect;
}

EyeRect FaceMapper::getRightEyeRect(void) {
	return rightEyeRect;
}

void FaceMapper::calculateEyeRects(void) {
	Point2d pointA, pointB;
	double dist;

	leftEyeRect.set = false;
	rightEyeRect.set = false;

	if(!facialFeatures.set) {
		return;
	}

	pointA = facialFeatures.eyeLeftInnerCorner;
	pointB = facialFeatures.eyeLeftOuterCorner;
	dist = Utilities::lineDistance(pointA, pointB);
	leftEyeRect.rect.x = pointA.x;
	leftEyeRect.rect.y = ((pointA.y + pointB.y) / 2.0) - (dist / 2.0);
	leftEyeRect.rect.width = dist;
	leftEyeRect.rect.height = dist;
	leftEyeRect.rect = Utilities::insetBox(leftEyeRect.rect, 1.25); // FIXME - magic numbers
	leftEyeRect.set = true;

	pointA = facialFeatures.eyeRightOuterCorner;
	pointB = facialFeatures.eyeRightInnerCorner;
	dist = Utilities::lineDistance(pointA, pointB);
	rightEyeRect.rect.x = pointA.x;
	rightEyeRect.rect.y = ((pointA.y + pointB.y) / 2.0) - (dist / 2.0);
	rightEyeRect.rect.width = dist;
	rightEyeRect.rect.height = dist;
	rightEyeRect.rect = Utilities::insetBox(rightEyeRect.rect, 1.25); // FIXME - magic numbers
	rightEyeRect.set = true;
}

}; //namespace YerFace
