#pragma once

#include <string>
#include <tuple>
#include "opencv2/objdetect.hpp"
#include "opencv2/tracking.hpp"

#include "dlib/opencv.h"
#include "dlib/image_processing/frontal_face_detector.h"
#include "dlib/image_processing/render_face_detections.h"
#include "dlib/image_processing.h"

#include "FrameDerivatives.hpp"
#include "TrackerState.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class FaceTracker {
public:
	FaceTracker(string myModelFileName, FrameDerivatives *myFrameDerivatives, float myMinFaceSizePercentage = 0.1);
	~FaceTracker();
	TrackerState processCurrentFrame(void);
	void renderPreviewHUD(bool verbose = true);
	TrackerState getTrackerState(void);
	tuple<Rect2d, bool> getFaceRect(void);
private:
	void doClassifyFace(void);

	string modelFileName;
	float minFaceSizePercentage;
	FrameDerivatives *frameDerivatives;
	TrackerState trackerState;
	bool classificationBoxSet;
	bool faceRectSet;
	dlib::rectangle classificationBoxDlib;
	Rect2d classificationBox;
	Rect2d classificationBoxNormalSize; //This is the scaled-up version to fit the native resolution of the frame.
	Rect2d faceRect;
	dlib::frontal_face_detector frontalFaceDetector;
	dlib::shape_predictor shapePredictor;
	dlib::cv_image<dlib::bgr_pixel> dlibClassificationFrame;
};

}; //namespace YerFace
