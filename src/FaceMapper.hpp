#pragma once

#include "FrameDerivatives.hpp"
#include "FaceTracker.hpp"
#include "MarkerTracker.hpp"
#include "MarkerSeparator.hpp"

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
	FaceMapper(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, float myEyelidBottomPointWeight = 0.6, float myFaceAspectRatio = 0.65);
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
	float eyelidBottomPointWeight;
	float faceAspectRatio;

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
