
#include "FaceTracker.hpp"
#include <exception>
#include <cmath>
#include <stdio.h>

using namespace std;

namespace YerFace {

FaceTracker::FaceTracker(string myClassifierFileName, FrameDerivatives *myFrameDerivatives, float myTrackingBoxPercentage, float myMaxFaceSizePercentage, int myOpticalTrackStaleFramesInterval) {
	classifierFileName = myClassifierFileName;
	frameDerivatives = myFrameDerivatives;
	trackerState = DETECTING;
	classificationBoxSet = false;
	trackingBoxSet = false;

	trackingBoxPercentage = myTrackingBoxPercentage;
	if(trackingBoxPercentage <= 0.0 || trackingBoxPercentage > 1.0) {
		throw invalid_argument("trackingBoxPercentage is out of range.");
	}
	maxFaceSizePercentage = myMaxFaceSizePercentage;
	if(maxFaceSizePercentage <= 0.0 || maxFaceSizePercentage > 1.0) {
		throw invalid_argument("maxFaceSizePercentage is out of range.");
	}
	opticalTrackStaleFramesInterval = myOpticalTrackStaleFramesInterval;
	if(opticalTrackStaleFramesInterval <= 0) {
		throw invalid_argument("opticalTrackStaleFramesInterval is out of range.");
	}
	if(!cascadeClassifier.load(classifierFileName)) {
		throw invalid_argument("Unable to load specified classifier.");
	}
	fprintf(stderr, "FaceTracker object constructed and ready to go!\n");
}

TrackerState FaceTracker::processCurrentFrame(void) {
	if(trackerState == DETECTING || trackerState == LOST || trackerState == STALE) {
		classificationBoxSet = false;
		std::vector<Rect> faces;
		Mat classificationFrame = frameDerivatives->getClassificationFrame();
		double classificationFrameArea = (double)classificationFrame.size().area();
		double maxFaceSize = sqrt(classificationFrameArea * maxFaceSizePercentage);
		cascadeClassifier.detectMultiScale(classificationFrame, faces, 1.1, 3, 0|CASCADE_SCALE_IMAGE, Size(maxFaceSize, maxFaceSize));

		int largestFace = -1;
		int largestFaceArea = -1;
		for( size_t i = 0; i < faces.size(); i++ ) {
			if(faces[i].area() > largestFaceArea) {
				largestFace = i;
				largestFaceArea = faces[i].area();
			}
		}
		if(largestFace >= 0) {
			classificationBoxSet = true;
			double classificationScaleFactor = frameDerivatives->getClassificationScaleFactor();
			classificationBox = faces[largestFace];
			classificationBoxNormalSize = Rect(
				(double)classificationBox.x / classificationScaleFactor,
				(double)classificationBox.y / classificationScaleFactor,
				(double)classificationBox.width / classificationScaleFactor,
				(double)classificationBox.height / classificationScaleFactor);
			//Switch to TRACKING
			trackerState = TRACKING;
			#if (CV_MINOR_VERSION < 3)
			tracker = Tracker::create("KCF");
			#else
			tracker = TrackerKCF::create();
			#endif
			double trackingBoxWidth = (double)classificationBoxNormalSize.width * trackingBoxPercentage;
			double trackingBoxHeight = (double)classificationBoxNormalSize.height * trackingBoxPercentage;
			Rect trackingBox = Rect(
				(double)classificationBoxNormalSize.x + (((double)classificationBoxNormalSize.width - trackingBoxWidth) / 2.0),
				(double)classificationBoxNormalSize.y + (((double)classificationBoxNormalSize.height - trackingBoxHeight) / 2.0),
				trackingBoxWidth,
				trackingBoxHeight);
			tracker->init(frameDerivatives->getCurrentFrame(), trackingBox);
			staleCounter = opticalTrackStaleFramesInterval;
		}
	}
	if(trackerState == TRACKING || trackerState == STALE) {
		bool trackSuccess = tracker->update(frameDerivatives->getCurrentFrame(), trackingBox);
		if(!trackSuccess) {
			fprintf(stderr, "FaceTracker WARNING! Track lost. Will keep searching...\n");
			trackingBoxSet = false;
			trackerState = LOST;
			//Attempt to re-process this same frame in LOST mode.
			return this->processCurrentFrame();
		} else {
			trackingBoxSet = true;
			staleCounter--;
			if(staleCounter <= 0) {
				trackerState = STALE;
			}
		}
	}
	return trackerState;
}

void FaceTracker::renderPreviewHUD(void) {
	Mat frame = frameDerivatives->getPreviewFrame();
	if(classificationBoxSet) {
		rectangle(frame, classificationBoxNormalSize, Scalar( 0, 255, 0 ), 1, 0);
	}
	if(trackingBoxSet) {
		rectangle(frame, trackingBox, Scalar( 255, 0, 0 ), 1, 0);
	}
}

TrackerState FaceTracker::getTrackerState(void) {
	return trackerState;
}

}; //namespace YerFace
