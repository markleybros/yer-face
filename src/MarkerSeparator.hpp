#pragma once

#include <tuple>

#include "opencv2/objdetect.hpp"
#include "opencv2/imgproc.hpp"
#include "FrameDerivatives.hpp"
#include "FaceTracker.hpp"
#include "MarkerType.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class MarkerSeparated {
public:
	RotatedRect marker;
	MarkerType assignedType;
};

class MarkerSeparator {
public:
	MarkerSeparator(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, Scalar myHSVRangeMin = Scalar(56, 29, 80), Scalar myHSVRangeMax = Scalar(100, 211, 255), float myFaceSizePercentage = 1.5, float myMinTargetMarkerAreaPercentage = 0.00001, float myMaxTargetMarkerAreaPercentage = 0.01);
	~MarkerSeparator();
	void setHSVRange(Scalar myHSVRangeMin, Scalar myHSVRangeMax);
	void processCurrentFrame(void);
	void renderPreviewHUD(bool verbose = true);
	vector<MarkerSeparated> *getMarkerList(void);
	void doPickColor(void);
private:
	FrameDerivatives *frameDerivatives;
	FaceTracker *faceTracker;
	float faceSizePercentage;
	float minTargetMarkerAreaPercentage;
	float maxTargetMarkerAreaPercentage;
	Scalar HSVRangeMin;
	Scalar HSVRangeMax;
	Mat searchFrameBGR;
	Mat searchFrameHSV;
	Rect2d markerBoundaryRect;
	vector<MarkerSeparated> markerList;
};

}; //namespace YerFace
