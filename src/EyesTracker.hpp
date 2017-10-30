#pragma once

#include <string>
#include <tuple>
#include "opencv2/objdetect.hpp"
#include "opencv2/tracking.hpp"

#include "FrameDerivatives.hpp"
#include "TrackerState.hpp"
#include "FaceTracker.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class EyesTracker {
public:
	EyesTracker(string myClassifierFileName, FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, float myMinEyeSizePercentage = 0.05, int myOpticalTrackStaleFramesInterval = 10);
	TrackerState processCurrentFrame(void);
	void renderPreviewHUD(void);
	// TrackerState getTrackerState(void);
	// tuple<Rect2d, bool> getLeftEyeRect(void);
	// tuple<Rect2d, bool> getRightEyeRect(void);
private:
	string classifierFileName;
	// float trackingBoxPercentage;
	float minEyeSizePercentage;
	int opticalTrackStaleFramesInterval;
	CascadeClassifier cascadeClassifier;
	// Ptr<Tracker> tracker;
	FrameDerivatives *frameDerivatives;
	FaceTracker *faceTracker;
	TrackerState trackerState;
	bool classificationBoxSet;
	// bool trackingBoxSet;
	// bool faceRectSet;
	Rect classificationBox;
	Rect classificationBoxNormalSize; //This is the scaled-up version to fit the native resolution of the frame.
	// Rect2d trackingBox;
	// Rect2d faceRect;
	// Point2d trackingBoxOffset;
	// int staleCounter;
};

}; //namespace YerFace
