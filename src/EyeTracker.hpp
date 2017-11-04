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

enum WhichEye {
	LeftEye, // Represents the Left Eye
	RightEye // Represents the Right Eye
};

class EyeTracker {
public:
	EyeTracker(WhichEye myWhichEye, string myClassifierFileName, FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, float myMinEyeSizePercentage = 0.05, float myMaxEyeSizePercentage = 0.3, int myOpticalTrackStaleFramesInterval = 10);
	WhichEye getWhichEye(void);
	TrackerState processCurrentFrame(void);
	void renderPreviewHUD(bool verbose = true);
	TrackerState getTrackerState(void);
	tuple<Rect2d, bool> getEyeRect(void);
	static const char *getWhichEyeAsString(WhichEye whichEye);
private:
	void performDetection(void);
	void performTracking(void);

	WhichEye whichEye;
	string classifierFileName;
	float minEyeSizePercentage;
	float maxEyeSizePercentage;
	int opticalTrackStaleFramesInterval;
	CascadeClassifier cascadeClassifier;
	Ptr<Tracker> tracker;
	FrameDerivatives *frameDerivatives;
	FaceTracker *faceTracker;
	TrackerState trackerState;
	bool classificationBoxSet;
	bool trackingBoxSet;
	Rect classificationBox;
	Rect classificationBoxNormalSize; //This is the scaled-up version to fit the native resolution of the frame.
	bool transitionedToTrackingThisFrame;
	Rect2d trackingBox;
	int staleCounter;
};

}; //namespace YerFace
