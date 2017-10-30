
#include "EyeTracker.hpp"
#include "Utilities.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

EyeTracker::EyeTracker(WhichEye myWhichEye, string myClassifierFileName, FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, float myMinEyeSizePercentage, float myMaxEyeSizePercentage, int myOpticalTrackStaleFramesInterval) {
	whichEye = myWhichEye;
	classifierFileName = myClassifierFileName;
	opticalTrackStaleFramesInterval = myOpticalTrackStaleFramesInterval;
	trackerState = DETECTING;
	classificationBoxSet = false;
	trackingBoxSet = false;

	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	faceTracker = myFaceTracker;
	if(faceTracker == NULL) {
		throw invalid_argument("faceTracker cannot be NULL");
	}
	minEyeSizePercentage = myMinEyeSizePercentage;
	if(minEyeSizePercentage <= 0.0 || minEyeSizePercentage > 1.0) {
		throw invalid_argument("minEyeSizePercentage is out of range.");
	}
	maxEyeSizePercentage = myMaxEyeSizePercentage;
	if(maxEyeSizePercentage <= 0.0 || maxEyeSizePercentage > 1.0) {
		throw invalid_argument("maxEyeSizePercentage is out of range.");
	}
	opticalTrackStaleFramesInterval = myOpticalTrackStaleFramesInterval;
	if(opticalTrackStaleFramesInterval <= 0) {
		throw invalid_argument("opticalTrackStaleFramesInterval is out of range.");
	}
	if(!cascadeClassifier.load(classifierFileName)) {
		throw invalid_argument("Unable to load specified classifier.");
	}
	fprintf(stderr, "EyeTracker <%s> object constructed and ready to go!\n", EyeTracker::getWhichEyeAsString(whichEye));
}

WhichEye EyeTracker::getWhichEye(void) {
	return whichEye;
}

TrackerState EyeTracker::processCurrentFrame(void) {
	if(trackerState == DETECTING || trackerState == LOST || trackerState == STALE) {
		double classificationScaleFactor = frameDerivatives->getClassificationScaleFactor();
		tuple<Rect2d, bool> faceRectTuple = faceTracker->getFaceRect();
		Rect2d faceRect = get<0>(faceRectTuple);
		bool faceRectSet = get<1>(faceRectTuple);
		if(!faceRectSet) {
			//FIXME -- what about STALE?
			fprintf(stderr, "EyeTracker: Can't track eyes, no face!\n");
			return trackerState;
		}
		Rect2d classificationCrop = Rect(Utilities::scaleRect(faceRect, classificationScaleFactor));
		classificationCrop.width = classificationCrop.width / 2;
		if(whichEye == LeftEye) {
			classificationCrop.x = classificationCrop.x + classificationCrop.width;
		}
		std::vector<Rect> eyes;
		try {
			Mat classificationFrame(frameDerivatives->getClassificationFrame(), classificationCrop);
			double classificationFrameArea = (double)classificationFrame.size().area();
			double minEyeSize = sqrt(classificationFrameArea * minEyeSizePercentage);
			double maxEyeSize = sqrt(classificationFrameArea * maxEyeSizePercentage);
			cascadeClassifier.detectMultiScale(classificationFrame, eyes, 1.1, 3, 0|CASCADE_SCALE_IMAGE, Size(minEyeSize, minEyeSize), Size(maxEyeSize, maxEyeSize));
		} catch(exception &e) {
			fprintf(stderr, "EyeTracker <%s>: WARNING: Failed classification detection. Got exception: %s", EyeTracker::getWhichEyeAsString(whichEye), e.what());
			//FIXME -- what about STALE?
			return trackerState;
		}

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
		}
	}
	return trackerState;
}

void EyeTracker::renderPreviewHUD(void) {
	Mat frame = frameDerivatives->getPreviewFrame();
	if(classificationBoxSet) {
		rectangle(frame, classificationBoxNormalSize, Scalar( 0, 255, 0 ), 1, 0);
	}
}

const char *EyeTracker::getWhichEyeAsString(WhichEye whichEye) {
	switch(whichEye) {
		default:
			return "Unknown!";
		case LeftEye:
			return "LeftEye";
		case RightEye:
			return "RightEye";
	}
}


}; //namespace YerFace
