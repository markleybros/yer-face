#pragma once

#include "FrameDerivatives.hpp"
#include "FaceTracker.hpp"
#include "EyeTracker.hpp"
#include "MarkerTracker.hpp"
#include "MarkerSeparator.hpp"

#include <tuple>

using namespace std;
using namespace cv;

namespace YerFace {

class MarkerTracker;

class MarkerMapper {
public:
	MarkerMapper(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, EyeTracker *myLeftEyeTracker, EyeTracker *myRightEyeTracker, float myEyelidBottomPointWeight = 0.6, float myEyeLineLengthPercentage = 2.25);
	~MarkerMapper();
	void processCurrentFrame(void);
	void renderPreviewHUD(bool verbose = true);
	tuple<Point2d, Point2d, Point2d, bool> getEyeLine(void);
private:
	void calculateEyeLine(void);
	bool calculateEyeCenter(MarkerTracker *top, MarkerTracker *bottom, Point2d *center);
	void calculateEyebrowLine(void);
	void calculateMidLine(void);
	
	FrameDerivatives *frameDerivatives;
	FaceTracker *faceTracker;
	EyeTracker *leftEyeTracker;
	EyeTracker *rightEyeTracker;
	float eyelidBottomPointWeight;
	float eyeLineLengthPercentage;

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

	Point2d eyeLineLeft;
	Point2d eyeLineRight;
	Point2d eyeLineCenter;
	bool eyeLineSet;
	Point2d eyebrowLineLeft;
	Point2d eyebrowLineRight;
	Point2d eyebrowLineCenter;
	bool eyebrowLineSet;
	Point2d midLineLeft;
	Point2d midLineRight;
	Point2d midLineCenter;
	bool midLineSet;
};

}; //namespace YerFace
