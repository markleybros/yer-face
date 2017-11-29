#pragma once

#include "opencv2/objdetect.hpp"
#include "opencv2/imgproc.hpp"

#include "Logger.hpp"
#include "SDLDriver.hpp"
#include "FrameDerivatives.hpp"
#include "FaceTracker.hpp"
#include "MarkerType.hpp"
#include "Metrics.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class MarkerSeparated {
public:
	RotatedRect marker;
	MarkerType assignedType;
	bool active;
};

class MarkerSeparator {
public:
	MarkerSeparator(SDLDriver *mySDLDriver, FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, Scalar myHSVRangeMin = Scalar(56, 29, 80), Scalar myHSVRangeMax = Scalar(100, 211, 255), float myFaceSizePercentage = 1.5, float myMinTargetMarkerAreaPercentage = 0.00001, float myMaxTargetMarkerAreaPercentage = 0.01, float myMarkerBoxInflatePixels = 1.5);
	~MarkerSeparator();
	void setHSVRange(Scalar myHSVRangeMin, Scalar myHSVRangeMax);
	void processCurrentFrame(bool debug = false);
	void renderPreviewHUD(bool verbose = true);
	vector<MarkerSeparated> *getMarkerList(void);
private:
	void doPickColor(void);
	
	SDLDriver *sdlDriver;
	FrameDerivatives *frameDerivatives;
	FaceTracker *faceTracker;
	float faceSizePercentage;
	float minTargetMarkerAreaPercentage;
	float maxTargetMarkerAreaPercentage;
	double markerBoxInflatePixels;

	Logger *logger;
	Metrics *metrics;
	Scalar HSVRangeMin;
	Scalar HSVRangeMax;
	Mat searchFrameBGR;
	Mat searchFrameHSV;
	Rect2d markerBoundaryRect;
	vector<MarkerSeparated> markerList;
};

}; //namespace YerFace
