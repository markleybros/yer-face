
#include "FaceMapper.hpp"
#include "Utilities.hpp"
#include "opencv2/calib3d.hpp"
#include "opencv2/highgui.hpp"

#include <cstdlib>

using namespace std;
using namespace cv;

namespace YerFace {

FaceMapper::FaceMapper(SDLDriver *mySDLDriver, FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker) {
	sdlDriver = mySDLDriver;
	if(sdlDriver == NULL) {
		throw invalid_argument("sdlDriver cannot be NULL");
	}
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	faceTracker = myFaceTracker;
	if(faceTracker == NULL) {
		throw invalid_argument("faceTracker cannot be NULL");
	}

	logger = new Logger("FaceMapper");
	metrics = new Metrics("FaceMapper");

	markerSeparator = new MarkerSeparator(sdlDriver, frameDerivatives, faceTracker);

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
	
	working.features.set = false;
	working.leftEye.set = false;
	working.rightEye.set = false;
	complete.features.set = false;
	complete.leftEye.set = false;
	complete.rightEye.set = false;

	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	logger->debug("FaceMapper object constructed and ready to go!");
}

FaceMapper::~FaceMapper() {
	logger->debug("FaceMapper object destructing...");
	vector<MarkerTracker *> markerTrackers = MarkerTracker::getMarkerTrackers();
	for(MarkerTracker *markerTracker : markerTrackers) {
		if(markerTracker != NULL) {
			delete markerTracker;
		}
	}
	SDL_DestroyMutex(myMutex);
	delete markerSeparator;
	delete metrics;
	delete logger;
}

void FaceMapper::processCurrentFrame(void) {
	YerFace_MutexLock(myMutex);

	markerSeparator->processCurrentFrame();

	metrics->startClock();

	working.features = faceTracker->getFacialFeatures();
	calculateEyeRects();

	markerJaw->processCurrentFrame();

	markerEyelidLeftTop->processCurrentFrame();
	markerEyelidLeftBottom->processCurrentFrame();
	markerEyebrowLeftInner->processCurrentFrame();
	markerEyebrowLeftMiddle->processCurrentFrame();
	markerEyebrowLeftOuter->processCurrentFrame();
	markerCheekLeft->processCurrentFrame();
	markerLipsLeftCorner->processCurrentFrame();
	markerLipsLeftTop->processCurrentFrame();
	markerLipsLeftBottom->processCurrentFrame();

	markerEyelidRightTop->processCurrentFrame();
	markerEyelidRightBottom->processCurrentFrame();
	markerEyebrowRightInner->processCurrentFrame();
	markerEyebrowRightMiddle->processCurrentFrame();
	markerEyebrowRightOuter->processCurrentFrame();
	markerCheekRight->processCurrentFrame();
	markerLipsRightCorner->processCurrentFrame();
	markerLipsRightTop->processCurrentFrame();
	markerLipsRightBottom->processCurrentFrame();

	metrics->endClock();

	YerFace_MutexUnlock(myMutex);
}

void FaceMapper::advanceWorkingToCompleted(void) {
	YerFace_MutexLock(myMutex);
	
	complete = working;
	working.features.set = false;
	working.leftEye.set = false;
	working.rightEye.set = false;

	markerSeparator->advanceWorkingToCompleted();
	vector<MarkerTracker *> markerTrackers = MarkerTracker::getMarkerTrackers();
	// for(MarkerTracker *markerTracker : (*markerTrackers)) {
	// 	markerTracker->advanceWorkingToCompleted();
	// }

	YerFace_MutexUnlock(myMutex);
}

void FaceMapper::renderPreviewHUD() {
	Mat frame = frameDerivatives->getPreviewFrame();
	int density = sdlDriver->getPreviewDebugDensity();
	vector<MarkerTracker *> markerTrackers = MarkerTracker::getMarkerTrackers();
	for(MarkerTracker *markerTracker : markerTrackers) {
		markerTracker->renderPreviewHUD();
	}
	markerSeparator->renderPreviewHUD();
	if(density > 0) {
		Size frameSize = frame.size();
		double previewRatio = 1.25, previewWidthPercentage = 0.2, previewCenterHeightPercentage = 0.2; // FIXME - magic numbers
		Rect2d previewRect;
		previewRect.width = frameSize.width * previewWidthPercentage;
		previewRect.height = previewRect.width * previewRatio;
		PreviewPositionInFrame previewPosition = sdlDriver->getPreviewPositionInFrame();
		if(previewPosition == BottomRight || previewPosition == TopRight) {
			previewRect.x = frameSize.width - previewRect.width;
		} else {
			previewRect.x = 0;
		}
		if(previewPosition == BottomLeft || previewPosition == BottomRight) {
			previewRect.y = frameSize.height - previewRect.height;
		} else {
			previewRect.y = 0;
		}
		Point2d previewCenter = Utilities::centerRect(previewRect);
		previewCenter.y -= previewRect.height * previewCenterHeightPercentage;
		double previewPointScale = previewRect.width / 200;
		rectangle(frame, previewRect, Scalar(10, 10, 10), CV_FILLED);
		for(MarkerTracker *markerTracker : markerTrackers) {
			MarkerPoint markerPoint = markerTracker->getMarkerPoint();
			Point2d previewPoint = Point2d(
					(markerPoint.point3d.x * previewPointScale) + previewCenter.x,
					(markerPoint.point3d.y * previewPointScale) + previewCenter.y);
			Utilities::drawX(frame, previewPoint, Scalar(255, 255, 255));
		}
	}
	YerFace_MutexLock(myMutex);
	if(density > 3) {
		if(complete.leftEye.set) {
			rectangle(frame, complete.leftEye.rect, Scalar(0, 0, 255));
		}
		if(complete.rightEye.set) {
			rectangle(frame, complete.rightEye.rect, Scalar(0, 0, 255));
		}
	}
	YerFace_MutexUnlock(myMutex);
}

SDLDriver *FaceMapper::getSDLDriver(void) {
	return sdlDriver;
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
	YerFace_MutexLock(myMutex);
	EyeRect val = working.leftEye;
	YerFace_MutexUnlock(myMutex);
	return val;
}

EyeRect FaceMapper::getRightEyeRect(void) {
	YerFace_MutexLock(myMutex);
	EyeRect val = working.rightEye;
	YerFace_MutexUnlock(myMutex);
	return val;
}

void FaceMapper::calculateEyeRects(void) {
	Point2d pointA, pointB;
	double dist;

	working.leftEye.set = false;
	working.rightEye.set = false;

	if(!working.features.set) {
		return;
	}

	pointA = working.features.eyeLeftInnerCorner;
	pointB = working.features.eyeLeftOuterCorner;
	dist = Utilities::lineDistance(pointA, pointB);
	working.leftEye.rect.x = pointA.x;
	working.leftEye.rect.y = ((pointA.y + pointB.y) / 2.0) - (dist / 2.0);
	working.leftEye.rect.width = dist;
	working.leftEye.rect.height = dist;
	working.leftEye.rect = Utilities::insetBox(working.leftEye.rect, 1.25); // FIXME - magic numbers
	working.leftEye.set = true;

	pointA = working.features.eyeRightOuterCorner;
	pointB = working.features.eyeRightInnerCorner;
	dist = Utilities::lineDistance(pointA, pointB);
	working.rightEye.rect.x = pointA.x;
	working.rightEye.rect.y = ((pointA.y + pointB.y) / 2.0) - (dist / 2.0);
	working.rightEye.rect.width = dist;
	working.rightEye.rect.height = dist;
	working.rightEye.rect = Utilities::insetBox(working.rightEye.rect, 1.25); // FIXME - magic numbers
	working.rightEye.set = true;
}

}; //namespace YerFace
