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

enum DlibFeatureIndexes {
	IDX_CHIN = 8,
	IDX_NOSE_BRIDGE = 27,
	IDX_NOSE_TIP = 30,
	IDX_EYE_RIGHT_OUTER_CORNER = 36,
	IDX_EYE_RIGHT_INNER_CORNER = 39,
	IDX_EYE_LEFT_INNER_CORNER = 42,
	IDX_EYE_LEFT_OUTER_CORNER = 45,
	IDX_MOUTH_RIGHT_OUTER_CORNER = 48,
	IDX_MOUTH_LEFT_OUTER_CORNER = 54
};

//Centimeters, roughly derived from https://en.wikipedia.org/wiki/File:AvgHeadSizes.png
#define VERTEX_NOSE_TIP Point3d(0.0, 0.0, 0.0)
#define VERTEX_CHIN Point3d(0.0, -6.5, -1.4)
#define VERTEX_EYE_RIGHT_OUTER_CORNER Point3d(-6.1, 3.7, -5.6)
#define VERTEX_EYE_LEFT_OUTER_CORNER Point3d(6.1, 3.7, -5.6)
#define VERTEX_MOUTH_RIGHT_OUTER_CORNER Point3d(-2.9, -2.8, -4.3)
#define VERTEX_MOUTH_LEFT_OUTER_CORNER  Point3d(2.9, -2.8, -4.3)

#define NUM_TRACKED_FEATURES 6

class FaceTracker {
public:
	FaceTracker(string myModelFileName, FrameDerivatives *myFrameDerivatives);
	~FaceTracker();
	TrackerState processCurrentFrame(void);
	void renderPreviewHUD(bool verbose = true);
	TrackerState getTrackerState(void);
private:
	void doClassifyFace(void);
	void doIdentifyFeatures(void);
	void doCalculateFacialTransformation(void);

	string modelFileName;
	FrameDerivatives *frameDerivatives;

	TrackerState trackerState;

	Rect2d classificationBox;
	Rect2d classificationBoxNormalSize; //This is the scaled-up version to fit the native resolution of the frame.
	bool classificationBoxSet;

	std::vector<Point2d> facialFeatures;
	bool facialFeaturesSet;

	dlib::rectangle classificationBoxDlib;
	dlib::frontal_face_detector frontalFaceDetector;
	dlib::shape_predictor shapePredictor;
	dlib::cv_image<dlib::bgr_pixel> dlibClassificationFrame;
};

}; //namespace YerFace
