#pragma once

#include <string>
#include "opencv2/objdetect.hpp"
#include "opencv2/tracking.hpp"

#include "dlib/opencv.h"
#include "dlib/dnn.h"
#include "dlib/image_processing/frontal_face_detector.h"
#include "dlib/image_processing/render_face_detections.h"
#include "dlib/image_processing.h"

#include "Logger.hpp"
#include "SDLDriver.hpp"
#include "FrameDerivatives.hpp"
#include "TrackerState.hpp"
#include "Metrics.hpp"
#include "Utilities.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

enum DlibFeatureIndexes {
	IDX_JAW_RIGHT_TOP = 0,
	IDX_MENTON = 8, //Chin
	IDX_JAW_LEFT_TOP = 16,
	IDX_NOSE_SELLION = 27, //Bridge of the nose
	IDX_NOSE_TIP = 30,
	IDX_EYE_RIGHT_OUTER_CORNER = 36,
	IDX_EYE_RIGHT_INNER_CORNER = 39,
	IDX_EYE_LEFT_INNER_CORNER = 42,
	IDX_EYE_LEFT_OUTER_CORNER = 45,
	IDX_MOUTH_RIGHT_OUTER_CORNER = 48,
	IDX_MOUTH_LEFT_OUTER_CORNER = 54,
	IDX_MOUTH_CENTER_INNER_TOP = 62,
	IDX_MOUTH_CENTER_INNER_BOTTOM = 66
};

//Unit millimeters. Values roughly taken from https://en.wikipedia.org/wiki/Human_head
#define VERTEX_NOSE_SELLION Point3d(0.0, 0.0, 0.0)
#define VERTEX_EYE_RIGHT_OUTER_CORNER Point3d(-65.5, 5.0, -20.0)
#define VERTEX_EYE_LEFT_OUTER_CORNER Point3d(65.5, 5.0, -20.0)
#define VERTEX_RIGHT_EAR Point3d(-77.5, 6.0, -100.0)
#define VERTEX_LEFT_EAR Point3d(77.5, 6.0, -100.0)
#define VERTEX_NOSE_TIP Point3d(0.0, 48.0, 21.0)
#define VERTEX_STOMMION Point3d(0.0, 75.0, 10.0)
#define VERTEX_MENTON Point3d(0.0, 133.0, 0.0)

class FacialPose {
public:
	Mat translationVector, rotationMatrix;
	Point3d planePoint;
	Vec3d planeNormal;
	double timestamp;
	bool set;
};

class FacialRect {
public:
	Rect2d rect;
	bool set;
};

class FacialFeatures {
public:
	Point2d menton, noseSellion, noseTip, stommion, eyeRightOuterCorner, eyeLeftOuterCorner, eyeRightInnerCorner, eyeLeftInnerCorner, jawRightTop, jawLeftTop;
	bool set;
};

class FacialFeaturesInternal {
public:
	std::vector<Point2d> features;
	std::vector<Point3d> features3D;
	FacialFeatures featuresExposed;
	bool set;
};

class FacialCameraModel {
public:
	Mat cameraMatrix, distortionCoefficients;
	bool set;
};

class FacialClassificationBox {
public:
	Rect2d box;
	Rect2d boxNormalSize; //This is the scaled-up version to fit the native resolution of the frame.
	bool set;
};

class FaceTrackerWorkingVariables {
public:
	FacialClassificationBox classificationBox;
	FacialRect trackingBox;
	FacialRect faceRect;
	FacialFeaturesInternal facialFeatures;
	FacialPose facialPose;
	FacialPose previouslyReportedFacialPose;
};


template <long num_filters, typename SUBNET> using con5d = dlib::con<num_filters,5,5,2,2,SUBNET>;
template <long num_filters, typename SUBNET> using con5  = dlib::con<num_filters,5,5,1,1,SUBNET>;

template <typename SUBNET> using downsampler  = dlib::relu<dlib::affine<con5d<32, dlib::relu<dlib::affine<con5d<32, dlib::relu<dlib::affine<con5d<16,SUBNET>>>>>>>>>;
template <typename SUBNET> using rcon5  = dlib::relu<dlib::affine<con5<45,SUBNET>>>;

using FaceDetectionModel = dlib::loss_mmod<dlib::con<1,9,9,1,1,rcon5<rcon5<rcon5<downsampler<dlib::input_rgb_image_pyramid<dlib::pyramid_down<6>>>>>>>>;

class FaceTracker {
public:
	FaceTracker(json config, SDLDriver *mySDLDriver, FrameDerivatives *myFrameDerivatives);
	~FaceTracker();
	TrackerState processCurrentFrame(void);
	void advanceWorkingToCompleted(void);
	void renderPreviewHUD(void);
	TrackerState getTrackerState(void);
	FacialRect getFacialBoundingBox(void);
	FacialFeatures getFacialFeatures(void);
	FacialCameraModel getFacialCameraModel(void);
	FacialPose getWorkingFacialPose(void);
	FacialPose getCompletedFacialPose(void);
private:
	void performInitializationOfTracker(void);
	bool performTracking(void);
	bool trackerDriftingExcessively(void);
	void doClassifyFace(void);
	void assignFaceRect(void);
	void doIdentifyFeatures(void);
	void doInitializeCameraModel(void);
	void doCalculateFacialTransformation(void);
	void doCalculateFacialPlane(void);
	bool doConvertLandmarkPointToImagePoint(dlib::point *src, Point2d *dst);

	string featureDetectionModelFileName, faceDetectionModelFileName;
	SDLDriver *sdlDriver;
	FrameDerivatives *frameDerivatives;
	bool performOpticalTracking;
	float trackingBoxPercentage;
	float maxTrackerDriftPercentage;
	double poseSmoothingOverSeconds;
	double poseSmoothingExponent;
	double poseRotationLowRejectionThreshold;
	double poseTranslationLowRejectionThreshold;
	double poseRotationHighRejectionThreshold;
	double poseTranslationHighRejectionThreshold;
	double poseRejectionResetAfterSeconds;

	Logger *logger;
	Metrics *metrics;
	TrackerState trackerState;

	double classificationScaleFactor;

	bool usingDNNFaceDetection;

	Ptr<Tracker> tracker;

	FacialCameraModel facialCameraModel;

	list<FacialPose> facialPoseSmoothingBuffer;

	dlib::frontal_face_detector frontalFaceDetector;
	dlib::shape_predictor shapePredictor;
	FaceDetectionModel faceDetectionModel;
	dlib::cv_image<dlib::bgr_pixel> dlibClassificationFrame;

	FaceTrackerWorkingVariables working, complete;

	SDL_mutex *myWrkMutex, *myCmpMutex;
};

}; //namespace YerFace
