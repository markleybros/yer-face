#pragma once

#include "FrameDerivatives.hpp"
#include "FaceTracker.hpp"
#include "MarkerTracker.hpp"
#include "MarkerSeparator.hpp"

#include <tuple>

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
	tuple<Point2d, Point2d, Point2d, bool> getEyebrowLine(void);
	tuple<Point2d, Point2d, Point2d, bool> getMidLine(void);
	tuple<Point2d, Point2d, Point2d, bool> getSmileLine(void);
	tuple<Point2d, Point2d, double, double, bool, bool> getCenterLine(void);
private:
	void calculateEyeRects(void);
	bool calculateEyeCenter(MarkerTracker *top, MarkerTracker *bottom, Point2d *center);
	void calculateEyebrowLine(void);
	void calculateMidLine(void);
	void calculateSmileLine(void);
	void calculateCenterLine(bool intermediate);
	
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

	Point2d eyebrowLineLeft;
	Point2d eyebrowLineRight;
	Point2d eyebrowLineCenter;
	double eyebrowLineSlope, eyebrowLineIntercept;
	bool eyebrowLineSet;
	Point2d midLineLeft;
	Point2d midLineRight;
	Point2d midLineCenter;
	double midLineSlope, midLineIntercept;
	bool midLineSet;
	Point2d smileLineLeft;
	Point2d smileLineRight;
	Point2d smileLineCenter;
	double smileLineSlope, smileLineIntercept;
	bool smileLineSet;
	Point2d centerLineTop;
	Point2d centerLineBottom;
	double centerLineSlope, centerLineIntercept;
	bool centerLineIsIntermediate;
	bool centerLineSet;

	vector<Point2d> faceTransformationBaselinePoints, faceTransformationCurrentPoints;
	bool faceTransformationBaselinePointsSet;
	bool faceTransformationSet;
};

}; //namespace YerFace
