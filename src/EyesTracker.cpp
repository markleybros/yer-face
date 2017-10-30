
#include "EyesTracker.hpp"
#include "Utilities.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

EyesTracker::EyesTracker(string myClassifierFileName, FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, float myMinEyeSizePercentage, int myOpticalTrackStaleFramesInterval) {
	classifierFileName = myClassifierFileName;
	frameDerivatives = myFrameDerivatives;
	faceTracker = myFaceTracker;
	minEyeSizePercentage = myMinEyeSizePercentage;
	opticalTrackStaleFramesInterval = myOpticalTrackStaleFramesInterval;
	trackerState = DETECTING;
	classificationBoxSet = false;

	opticalTrackStaleFramesInterval = myOpticalTrackStaleFramesInterval;
	if(opticalTrackStaleFramesInterval <= 0) {
		throw invalid_argument("opticalTrackStaleFramesInterval is out of range.");
	}
	if(!cascadeClassifier.load(classifierFileName)) {
		throw invalid_argument("Unable to load specified classifier.");
	}
	fprintf(stderr, "EyesTracker object constructed and ready to go!\n");
}

TrackerState EyesTracker::processCurrentFrame(void) {
	if(trackerState == DETECTING || trackerState == LOST || trackerState == STALE) {
		double classificationScaleFactor = frameDerivatives->getClassificationScaleFactor();
		classificationBoxSet = false;
		tuple<Rect2d, bool> faceRectTuple = faceTracker->getFaceRect();
		Rect2d faceRect = get<0>(faceRectTuple);
		bool faceRectSet = get<1>(faceRectTuple);
		if(!faceRectSet) {
			fprintf(stderr, "EyesTracker: Can't track eyes, no face!");
			return trackerState;
		}
		Rect2d classificationCrop = Rect(Utilities::scaleRect(faceRect, classificationScaleFactor));
		Mat classificationFrame(frameDerivatives->getClassificationFrame(), classificationCrop);
		double classificationFrameArea = (double)classificationFrame.size().area();
		double minEyeSize = sqrt(classificationFrameArea * minEyeSizePercentage);
		std::vector<Rect> eyes;
		cascadeClassifier.detectMultiScale(classificationFrame, eyes, 1.1, 3, 0|CASCADE_SCALE_IMAGE, Size(minEyeSize, minEyeSize));

		int largestEye = -1;
		int largestEyeArea = -1;
		for( size_t i = 0; i < eyes.size(); i++ ) {
			if(eyes[i].area() > largestEyeArea) {
				largestEye = i;
				largestEyeArea = eyes[i].area();
			}
		}
		if(largestEye >= 0) {
			classificationBoxSet = true;
			classificationBox = eyes[largestEye];
			classificationBox.x += classificationCrop.x;
			classificationBox.y += classificationCrop.y;
			classificationBoxNormalSize = Rect(Utilities::scaleRect(classificationBox, 1.0 / classificationScaleFactor));

			// //Switch to TRACKING
			// trackerState = TRACKING;
			// transitionedToTrackingThisFrame = true;
			// #if (CV_MINOR_VERSION < 3)
			// tracker = Tracker::create("KCF");
			// #else
			// tracker = TrackerKCF::create();
			// #endif
			// double trackingBoxWidth = (double)classificationBoxNormalSize.width * trackingBoxPercentage;
			// double trackingBoxHeight = (double)classificationBoxNormalSize.height * trackingBoxPercentage;
			// trackingBoxOffset = Point(
			// 	(((double)classificationBoxNormalSize.width - trackingBoxWidth) / 2.0),
			// 	(((double)classificationBoxNormalSize.height - trackingBoxHeight) / 2.0));
			// trackingBox = Rect(
			// 	(double)classificationBoxNormalSize.x + trackingBoxOffset.x,
			// 	(double)classificationBoxNormalSize.y + trackingBoxOffset.y,
			// 	trackingBoxWidth,
			// 	trackingBoxHeight);
			// trackingBoxSet = true;
			//
			// tracker->init(frameDerivatives->getCurrentFrame(), trackingBox);
			// staleCounter = opticalTrackStaleFramesInterval;
		}
	}
	return trackerState;
}

void EyesTracker::renderPreviewHUD(void) {
	Mat frame = frameDerivatives->getPreviewFrame();
	if(classificationBoxSet) {
		rectangle(frame, classificationBoxNormalSize, Scalar( 0, 255, 0 ), 1, 0);
	}
}

}; //namespace YerFace
