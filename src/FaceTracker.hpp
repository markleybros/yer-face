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
#include "MarkerType.hpp"
#include "FrameDerivatives.hpp"
#include "Metrics.hpp"
#include "Utilities.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

enum DlibFeatureIndexes {
	IDX_JAWLINE_0 = 0, //Uppermost right side.
	IDX_JAWLINE_1 = 1,
	IDX_JAWLINE_2 = 2,
	IDX_JAWLINE_3 = 3,
	IDX_JAWLINE_4 = 4,
	IDX_JAWLINE_5 = 5,
	IDX_JAWLINE_6 = 6,
	IDX_JAWLINE_7 = 7,
	IDX_JAWLINE_8 = 8, //Chin
	IDX_JAWLINE_9 = 9,
	IDX_JAWLINE_10 = 10,
	IDX_JAWLINE_11 = 11,
	IDX_JAWLINE_12 = 12,
	IDX_JAWLINE_13 = 13,
	IDX_JAWLINE_14 = 14,
	IDX_JAWLINE_15 = 15,
	IDX_JAWLINE_16 = 16, //Uppermost left side.
	IDX_LEFTEYEBROW_FAROUTER = 17,
	IDX_LEFTEYEBROW_NEAROUTER = 18,
	IDX_LEFTEYEBROW_MIDDLE = 19,
	IDX_LEFTEYEBROW_NEARINNER = 20,
	IDX_LEFTEYEBROW_FARINNER = 21,
	IDX_RIGHTEYEBROW_FARINNER = 22,
	IDX_RIGHTEYEBROW_NEARINNER = 23,
	IDX_RIGHTEYEBROW_MIDDLE = 24,
	IDX_RIGHTEYEBROW_NEAROUTER = 25,
	IDX_RIGHTEYEBROW_FAROUTER = 26,
	IDX_NOSE_SELLION = 27, //Bridge of the nose
	IDX_NOSE_TIP = 30,
	IDX_RIGHTEYE_OUTER_CORNER = 36,
	IDX_RIGHTEYE_UPPERLID_RIGHT = 37,
	IDX_RIGHTEYE_UPPERLID_LEFT = 38,
	IDX_RIGHTEYE_INNER_CORNER = 39,
	IDX_RIGHTEYE_LOWERLID_LEFT = 40,
	IDX_RIGHTEYE_LOWERLID_RIGHT = 41,
	IDX_LEFTEYE_INNER_CORNER = 42,
	IDX_LEFTEYE_UPPERLID_RIGHT = 43,
	IDX_LEFTEYE_UPPERLID_LEFT = 44,
	IDX_LEFTEYE_OUTER_CORNER = 45,
	IDX_LEFTEYE_LOWERLID_LEFT = 46,
	IDX_LEFTEYE_LOWERLID_RIGHT = 47,
	IDX_MOUTHOUT_RIGHT_CORNER = 48,
	IDX_MOUTHOUT_RIGHT_FAROUTER_TOP = 49,
	IDX_MOUTHOUT_RIGHT_NEAROUTER_TOP = 50,
	IDX_MOUTHOUT_CENTER_TOP = 51,
	IDX_MOUTHOUT_LEFT_NEAROUTER_TOP = 52,
	IDX_MOUTHOUT_LEFT_FAROUTER_TOP = 53,
	IDX_MOUTHOUT_LEFT_CORNER = 54,
	IDX_MOUTHOUT_LEFT_FAROUTER_BOTTOM = 55,
	IDX_MOUTHOUT_LEFT_NEAROUTER_BOTTOM = 56,
	IDX_MOUTHOUT_CENTER_BOTTOM = 57,
	IDX_MOUTHOUT_RIGHT_NEAROUTER_BOTTOM = 58,
	IDX_MOUTHOUT_RIGHT_FAROUTER_BOTTOM = 59,
	IDX_MOUTHIN_RIGHT_CORNER = 60,
	IDX_MOUTHIN_RIGHT_TOP = 61,
	IDX_MOUTHIN_CENTER_TOP = 62,
	IDX_MOUTHIN_LEFT_TOP = 63,
	IDX_MOUTHIN_LEFT_CORNER = 64,
	IDX_MOUTHIN_LEFT_BOTTOM = 65,
	IDX_MOUTHIN_CENTER_BOTTOM = 66,
	IDX_MOUTHIN_RIGHT_BOTTOM = 67
};

class FacialPose {
public:
	Mat translationVector, rotationMatrix;
	Mat translationVectorInternal, rotationMatrixInternal;
	Vec3d facialPlaneNormal;
	double timestamp;
	bool set;
};

class FacialPlane {
public:
	Point3d planePoint;
	Vec3d planeNormal;
};

class FacialRect {
public:
	Rect2d rect;
	bool set;
};

class FacialFeatures {
public:
	std::vector<Point2d> features;
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
	signed long frameNumber; //The frame number when the classification was run.
	bool run; //Did the classifier run?
	bool set; //Is the box valid?
};

class FaceTrackerWorkingVariables {
public:
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
	FaceTracker(json config, SDLDriver *mySDLDriver, FrameDerivatives *myFrameDerivatives, bool myLowLatency);
	~FaceTracker() noexcept(false);
	void processCurrentFrame(void);
	void advanceWorkingToCompleted(void);
	void renderPreviewHUD(void);
	FacialRect getFacialBoundingBox(void);
	FacialFeatures getFacialFeatures(void);
	FacialCameraModel getFacialCameraModel(void);
	FacialPose getWorkingFacialPose(void);
	FacialPose getCompletedFacialPose(void);
	FacialPlane getCalculatedFacialPlaneForWorkingFacialPose(MarkerType markerType);
private:
	void doClassifyFace(ClassificationFrame classificationFrame);
	void assignFaceRect(void);
	void doIdentifyFeatures(ClassificationFrame classificationFrame);
	void doInitializeCameraModel(void);
	void doCalculateFacialTransformation(void);
	void doPrecalculateFacialPlaneNormal(void);
	bool doConvertLandmarkPointToImagePoint(dlib::point *src, Point2d *dst, double classificationScaleFactor);
	static int runClassificationLoop(void *ptr);

	string featureDetectionModelFileName, faceDetectionModelFileName;
	SDLDriver *sdlDriver;
	FrameDerivatives *frameDerivatives;
	bool lowLatency;
	float trackingBoxPercentage;
	float maxTrackerDriftPercentage;
	double poseSmoothingOverSeconds;
	double poseSmoothingExponent;
	double poseRotationLowRejectionThreshold;
	double poseTranslationLowRejectionThreshold;
	double poseRotationLowRejectionThresholdInternal;
	double poseTranslationLowRejectionThresholdInternal;
	double poseRotationHighRejectionThreshold;
	double poseTranslationHighRejectionThreshold;
	double poseRejectionResetAfterSeconds;
	double poseTranslationMaxX;
	double poseTranslationMinX;
	double poseTranslationMaxY;
	double poseTranslationMinY;
	double poseTranslationMaxZ;
	double poseTranslationMinZ;
	double poseRotationPlusMinusX;
	double poseRotationPlusMinusY;
	double poseRotationPlusMinusZ;
	Point3d vertexNoseSellion;
	Point3d vertexEyeRightOuterCorner;
	Point3d vertexEyeLeftOuterCorner;
	Point3d vertexRightEar;
	Point3d vertexLeftEar;
	Point3d vertexNoseTip;
	Point3d vertexMenton;
	Point3d vertexStommion;
	double depthSliceA, depthSliceB, depthSliceC, depthSliceD, depthSliceE, depthSliceF, depthSliceG, depthSliceH;

	Logger *logger;
	Metrics *metrics;

	bool usingDNNFaceDetection;

	FacialCameraModel facialCameraModel;

	list<FacialPose> facialPoseSmoothingBuffer;

	dlib::frontal_face_detector frontalFaceDetector;
	dlib::shape_predictor shapePredictor;
	FaceDetectionModel faceDetectionModel;

	FacialClassificationBox newestClassificationBox;

	FaceTrackerWorkingVariables working, complete;

	SDL_mutex *myCmpMutex, *myClassificationMutex;
	SDL_Thread *classifierThread;
	bool classifierRunning;
};

}; //namespace YerFace
