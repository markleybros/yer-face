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
	FaceTracker(string myModelFileName, FrameDerivatives *myFrameDerivatives, int myFeatureBufferSize = 4, float myFeatureSmoothingExponent = 2.0);
	~FaceTracker();
	TrackerState processCurrentFrame(void);
	void renderPreviewHUD(bool verbose = true);
	TrackerState getTrackerState(void);
private:
	void doClassifyFace(void);
	void doIdentifyFeatures(void);
	void doCalculatePerspectiveTransformationMatrix(void);

	string modelFileName;
	FrameDerivatives *frameDerivatives;
	int featureBufferSize;
	float featureSmoothingExponent;

	TrackerState trackerState;

	Rect2d classificationBox;
	Rect2d classificationBoxNormalSize; //This is the scaled-up version to fit the native resolution of the frame.
	bool classificationBoxSet;

	std::vector<Point2d> facialFeatures;
	list<std::vector<Point2d>> facialFeaturesBuffer;
	bool facialFeaturesSet;

	std::vector<Point2d> facialFeaturesInitial;
	bool facialFeaturesInitialSet;

	Mat perspectiveTransformationMatrix;
	bool perspectiveTransformationMatrixSet;

	dlib::rectangle classificationBoxDlib;
	dlib::frontal_face_detector frontalFaceDetector;
	dlib::shape_predictor shapePredictor;
	dlib::cv_image<dlib::bgr_pixel> dlibClassificationFrame;
};

}; //namespace YerFace
