#pragma once

#include <string>
#include <tuple>
#include "opencv2/objdetect.hpp"
#include "opencv2/tracking.hpp"

#include "FrameDerivatives.hpp"
#include "TrackerState.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class FaceTracker {
public:
	FaceTracker(string myClassifierFileName, FrameDerivatives *myFrameDerivatives, float myTrackingBoxPercentage = 0.75, float myMaxFaceSizePercentage = 0.1, int myOpticalTrackStaleFramesInterval = 15);
	TrackerState processCurrentFrame(void);
	void renderPreviewHUD(bool verbose = true);
	TrackerState getTrackerState(void);
	tuple<Rect2d, bool> getFaceRect(void);
private:
	string classifierFileName;
	float trackingBoxPercentage;
	float maxFaceSizePercentage;
	int opticalTrackStaleFramesInterval;
	CascadeClassifier cascadeClassifier;
	Ptr<Tracker> tracker;
	FrameDerivatives *frameDerivatives;
	TrackerState trackerState;
	bool classificationBoxSet;
	bool trackingBoxSet;
	bool faceRectSet;
	Rect classificationBox;
	Rect classificationBoxNormalSize; //This is the scaled-up version to fit the native resolution of the frame.
	Rect2d trackingBox;
	Rect2d faceRect;
	Point2d trackingBoxOffset;
	int staleCounter;
};

}; //namespace YerFace
