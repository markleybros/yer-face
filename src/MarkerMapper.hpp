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

class MarkerMapper {
public:
	MarkerMapper(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, EyeTracker *myLeftEyeTracker, EyeTracker *myRightEyeTracker);
	~MarkerMapper();
	void processCurrentFrame(void);
	void renderPreviewHUD(bool verbose = true);
	tuple<Point2d, Point2d, bool> getEyeLine(void);
private:
	void calculateEyeLine(void);
	bool calculateEyeCenter(MarkerTracker *top, MarkerTracker *bottom, Point2d *center);

	FrameDerivatives *frameDerivatives;
	FaceTracker *faceTracker;
	EyeTracker *leftEyeTracker;
	EyeTracker *rightEyeTracker;
	MarkerSeparator *markerSeparator;

	MarkerTracker *markerEyelidLeftTop;
	MarkerTracker *markerEyelidRightTop;
	MarkerTracker *markerEyelidLeftBottom;
	MarkerTracker *markerEyelidRightBottom;

	Point2d eyeLineLeft;
	Point2d eyeLineRight;
	bool eyeLineSet;
};

}; //namespace YerFace
