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
	FaceTracker(string myClassifierFileName, FrameDerivatives *myFrameDerivatives);
	TrackerState processCurrentFrame(void);
	void renderPreviewHUD(void);
	TrackerState getTrackerState(void);
	tuple<Rect, bool> getTrackingRect(void); //FIXME - implement this
private:
	string classifierFileName;
	CascadeClassifier cascadeClassifier;
	Ptr<Tracker> tracker;
	FrameDerivatives *frameDerivatives;
	TrackerState trackerState;
	bool classificationBoxSet;
	bool trackingBoxSet;
	Rect classificationBox;
	Rect classificationBoxNormalSize; //This is the scaled-up version to fit the native resolution of the frame.
	Rect2d trackingBox;
	double trackingBoxScaleFactor;
	int staleCounter;
};

}; //namespace YerFace
