
#include "FaceTracker.hpp"
#include "Utilities.hpp"
#include <exception>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace std;

namespace YerFace {

FaceTracker::FaceTracker(string myClassifierFileName, FrameDerivatives *myFrameDerivatives, float myTrackingBoxPercentage, float myMinFaceSizePercentage, int myOpticalTrackStaleFramesInterval) {
	classifierFileName = myClassifierFileName;
	trackerState = DETECTING;
	classificationBoxSet = false;
	trackingBoxSet = false;

	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	trackingBoxPercentage = myTrackingBoxPercentage;
	if(trackingBoxPercentage <= 0.0 || trackingBoxPercentage > 1.0) {
		throw invalid_argument("trackingBoxPercentage is out of range.");
	}
	minFaceSizePercentage = myMinFaceSizePercentage;
	if(minFaceSizePercentage <= 0.0 || minFaceSizePercentage > 1.0) {
		throw invalid_argument("minFaceSizePercentage is out of range.");
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

FaceTracker::~FaceTracker() {
	fprintf(stderr, "FaceTracker object destructing...\n");
}

TrackerState FaceTracker::processCurrentFrame(void) {
	double classificationScaleFactor = frameDerivatives->getClassificationScaleFactor();
	bool transitionedToTrackingThisFrame = false;
	if(trackerState == DETECTING || trackerState == LOST || trackerState == STALE) {
		classificationBoxSet = false;
		std::vector<Rect> faces;
		Mat classificationFrame = frameDerivatives->getClassificationFrame();
		double classificationFrameArea = (double)classificationFrame.size().area();
		double minFaceSize = sqrt(classificationFrameArea * minFaceSizePercentage);
		cascadeClassifier.detectMultiScale(classificationFrame, faces, 1.1, 3, 0|CASCADE_SCALE_IMAGE, Size(minFaceSize, minFaceSize));

		int largestFace = -1;
		int largestFaceArea = -1;
		Rect2d scaledTrackingBox;
		if(trackerState == STALE && trackingBoxSet) {
			scaledTrackingBox = Rect(Utilities::scaleRect(trackingBox, classificationScaleFactor));
		}
		for(size_t i = 0; i < faces.size(); i++) {
			if(faces[i].area() > largestFaceArea) {
				if(trackerState == STALE && trackingBoxSet) {
					//This face is only a candidate if it overlaps (at least a bit) with the face we have been tracking.
					Rect2d candidateFace = Rect2d(faces[i]);
					if((scaledTrackingBox & candidateFace).area() <= 0) {
						continue;
					}
				}
				largestFace = i;
				largestFaceArea = faces[i].area();
			}
		}
		if(largestFace >= 0) {
			classificationBoxSet = true;
			classificationBox = faces[largestFace];
			classificationBoxNormalSize = Utilities::scaleRect(classificationBox, 1.0 / classificationScaleFactor);
			//Switch to TRACKING
			trackerState = TRACKING;
			transitionedToTrackingThisFrame = true;
			#if (CV_MINOR_VERSION < 3)
			tracker = Tracker::create("KCF");
			#else
			tracker = TrackerKCF::create();
			#endif
			trackingBox = Rect(Utilities::insetBox(classificationBoxNormalSize, trackingBoxPercentage));
			trackingBoxSet = true;

			tracker->init(frameDerivatives->getCurrentFrame(), trackingBox);
			staleCounter = opticalTrackStaleFramesInterval;
		}
	}
	if((trackerState == TRACKING && !transitionedToTrackingThisFrame) || trackerState == STALE) {
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
	faceRectSet = false;
	if(trackingBoxSet) {
		faceRect = Rect(Utilities::insetBox(trackingBox, 1.0 / trackingBoxPercentage));
		faceRectSet = true;
	}
	return trackerState;
}

void FaceTracker::renderPreviewHUD(bool verbose) {
	Mat frame = frameDerivatives->getPreviewFrame();
	if(faceRectSet) {
		rectangle(frame, faceRect, Scalar( 0, 0, 255 ), 2);
	}
	if(verbose) {
		if(classificationBoxSet) {
			rectangle(frame, classificationBoxNormalSize, Scalar( 0, 255, 0 ), 1);
		}
		if(trackingBoxSet) {
			rectangle(frame, trackingBox, Scalar( 255, 0, 0 ), 1);
		}
	}
}

TrackerState FaceTracker::getTrackerState(void) {
	return trackerState;
}

tuple<Rect2d, bool> FaceTracker::getFaceRect(void) {
	return make_tuple(faceRect, faceRectSet);
}

}; //namespace YerFace
