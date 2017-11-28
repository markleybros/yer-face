#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"
#include "FaceTracker.hpp"
#include "MarkerTracker.hpp"
#include "MarkerSeparator.hpp"
#include "Metrics.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class MarkerTracker;

class EyeRect {
public:
	Rect2d rect;
	bool set;
};

class FaceMapper {
public:
	FaceMapper(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker);
	~FaceMapper();
	void processCurrentFrame(void);
	void renderPreviewHUD(bool verbose = true);
	FrameDerivatives *getFrameDerivatives(void);
	FaceTracker *getFaceTracker(void);
	MarkerSeparator *getMarkerSeparator(void);
	EyeRect getLeftEyeRect(void);
	EyeRect getRightEyeRect(void);
private:
	void calculateEyeRects(void);
	
	FrameDerivatives *frameDerivatives;
	FaceTracker *faceTracker;

	Logger *logger;
	Metrics *metrics;

	MarkerSeparator *markerSeparator;

	MarkerTracker *markerEyelidLeftTop;
	MarkerTracker *markerEyelidRightTop;
	MarkerTracker *markerEyelidLeftBottom;
	MarkerTracker *markerEyelidRightBottom;

	MarkerTracker *markerEyebrowLeftInner;
	MarkerTracker *markerEyebrowLeftMiddle;
	MarkerTracker *markerEyebrowLeftOuter;
	MarkerTracker *markerEyebrowRightInner;
	MarkerTracker *markerEyebrowRightMiddle;
	MarkerTracker *markerEyebrowRightOuter;

	MarkerTracker *markerCheekLeft;
	MarkerTracker *markerCheekRight;

	MarkerTracker *markerJaw;

	MarkerTracker *markerLipsLeftCorner;
	MarkerTracker *markerLipsRightCorner;

	MarkerTracker *markerLipsLeftTop;
	MarkerTracker *markerLipsRightTop;
	
	MarkerTracker *markerLipsLeftBottom;
	MarkerTracker *markerLipsRightBottom;

	FacialFeatures facialFeatures;
	EyeRect leftEyeRect;
	EyeRect rightEyeRect;
};

}; //namespace YerFace
