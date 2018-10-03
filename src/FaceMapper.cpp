
#include "FaceMapper.hpp"
#include "Utilities.hpp"
#include "opencv2/calib3d.hpp"
#include "opencv2/highgui.hpp"

#include <cstdlib>

using namespace std;
using namespace cv;

namespace YerFace {

FaceMapper::FaceMapper(json config, SDLDriver *mySDLDriver, FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker) {
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
	metrics = new Metrics(config, "FaceMapper", frameDerivatives);

	markerEyelidLeftTop = new MarkerTracker(config, EyelidLeftTop, this);
	markerEyelidRightTop = new MarkerTracker(config, EyelidRightTop, this);
	markerEyelidLeftBottom = new MarkerTracker(config, EyelidLeftBottom, this);
	markerEyelidRightBottom = new MarkerTracker(config, EyelidRightBottom, this);

	markerEyebrowLeftInner = new MarkerTracker(config, EyebrowLeftInner, this);
	markerEyebrowRightInner = new MarkerTracker(config, EyebrowRightInner, this);
	markerEyebrowLeftMiddle = new MarkerTracker(config, EyebrowLeftMiddle, this);
	markerEyebrowRightMiddle = new MarkerTracker(config, EyebrowRightMiddle, this);
	markerEyebrowLeftOuter = new MarkerTracker(config, EyebrowLeftOuter, this);
	markerEyebrowRightOuter = new MarkerTracker(config, EyebrowRightOuter, this);

	markerJaw = new MarkerTracker(config, Jaw, this);

	markerLipsLeftCorner = new MarkerTracker(config, LipsLeftCorner, this);
	markerLipsRightCorner = new MarkerTracker(config, LipsRightCorner, this);

	markerLipsLeftTop = new MarkerTracker(config, LipsLeftTop, this);
	markerLipsRightTop = new MarkerTracker(config, LipsRightTop, this);

	markerLipsLeftBottom = new MarkerTracker(config, LipsLeftBottom, this);
	markerLipsRightBottom = new MarkerTracker(config, LipsRightBottom, this);
	
	if((myCmpMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	trackers = {
		markerEyelidLeftTop,
		markerEyelidLeftBottom,
		markerEyelidRightTop,
		markerEyelidRightBottom,
		markerEyebrowLeftInner,
		markerEyebrowLeftMiddle,
		markerEyebrowLeftOuter,
		markerEyebrowRightInner,
		markerEyebrowRightMiddle,
		markerEyebrowRightOuter,
		markerJaw,
		markerLipsLeftCorner,
		markerLipsRightCorner,
		markerLipsLeftTop,
		markerLipsRightTop,
		markerLipsLeftBottom,
		markerLipsRightBottom
	};

	logger->debug("FaceMapper object constructed and ready to go!");
}

FaceMapper::~FaceMapper() {
	logger->debug("FaceMapper object destructing...");
	for(MarkerTracker *markerTracker : trackers) {
		if(markerTracker != NULL) {
			delete markerTracker;
		}
	}
	SDL_DestroyMutex(myCmpMutex);
	delete metrics;
	delete logger;
}

void FaceMapper::processCurrentFrame(void) {

	metrics->startClock();

	for(MarkerTracker *tracker : trackers) {
		tracker->processCurrentFrame();
	}

	metrics->endClock();
}

void FaceMapper::advanceWorkingToCompleted(void) {
	for(MarkerTracker *markerTracker : trackers) {
		markerTracker->advanceWorkingToCompleted();
	}
}

void FaceMapper::renderPreviewHUD() {
	YerFace_MutexLock(myCmpMutex);
	Mat frame = frameDerivatives->getCompletedPreviewFrame();
	int density = sdlDriver->getPreviewDebugDensity();
	for(MarkerTracker *markerTracker : trackers) {
		markerTracker->renderPreviewHUD();
	}
	if(density > 0) {
		Size frameSize = frame.size();
		double previewRatio = 1.25, previewWidthPercentage = 0.2, previewCenterHeightPercentage = 0.2; // FIXME - magic numbers
		int gridIncrement = 15; //FIXME - more magic numbers
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
		rectangle(frame, previewRect, Scalar(10, 10, 10), FILLED);
		if(density > 4) {
			for(int x = previewRect.x; x < previewRect.x + previewRect.width; x = x + gridIncrement) {
				cv::line(frame, Point2d(x, previewRect.y), Point2d(x, previewRect.y + previewRect.height), Scalar(75, 75, 75));
			}
			for(int y = previewRect.y; y < previewRect.y + previewRect.height; y = y + gridIncrement) {
				cv::line(frame, Point2d(previewRect.x, y), Point2d(previewRect.x + previewRect.width, y), Scalar(75, 75, 75));
			}
		}
		for(MarkerTracker *markerTracker : trackers) {
			MarkerPoint markerPoint = markerTracker->getCompletedMarkerPoint();
			if(markerPoint.set) {
				Point2d previewPoint = Point2d(
						(markerPoint.point3d.x * previewPointScale) + previewCenter.x,
						(markerPoint.point3d.y * previewPointScale) + previewCenter.y);
				Utilities::drawX(frame, previewPoint, Scalar(255, 255, 255));
			}
		}
	}
	YerFace_MutexUnlock(myCmpMutex);
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

}; //namespace YerFace
