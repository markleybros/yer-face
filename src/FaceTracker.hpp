#pragma once

#include <string>

#include "Logger.hpp"
#include "Status.hpp"
#include "SDLDriver.hpp"
#include "FaceDetector.hpp"
#include "MarkerType.hpp"
#include "FrameServer.hpp"
#include "Metrics.hpp"
#include "Utilities.hpp"
#include "WorkerPool.hpp"

using namespace std;

namespace YerFace {

class DlibPointPointer;

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
	IDX_RIGHTEYEBROW_FAROUTER = 17,
	IDX_RIGHTEYEBROW_NEAROUTER = 18,
	IDX_RIGHTEYEBROW_MIDDLE = 19,
	IDX_RIGHTEYEBROW_NEARINNER = 20,
	IDX_RIGHTEYEBROW_FARINNER = 21,
	IDX_LEFTEYEBROW_FARINNER = 22,
	IDX_LEFTEYEBROW_NEARINNER = 23,
	IDX_LEFTEYEBROW_MIDDLE = 24,
	IDX_LEFTEYEBROW_NEAROUTER = 25,
	IDX_LEFTEYEBROW_FAROUTER = 26,
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
	cv::Mat translationVector, rotationMatrix;
	cv::Mat translationVectorInternal, rotationMatrixInternal;
	cv::Vec3d facialPlaneNormal;
	double timestamp;
	bool set;
};

class FacialPlane {
public:
	cv::Point3d planePoint;
	cv::Vec3d planeNormal;
};

class FacialFeatures {
public:
	std::vector<cv::Point2d> features;
	bool set;
};

class FacialFeaturesInternal {
public:
	std::vector<cv::Point2d> features;
	std::vector<cv::Point3d> features3D;
	FacialFeatures featuresExposed;
	bool set;
};

class FacialCameraModel {
public:
	cv::Mat cameraMatrix, distortionCoefficients;
	bool set;
};

class FaceTrackerWorker;

class FaceTrackerOutput {
public:
	bool set;
	FrameNumber frameNumber;
	FacialFeaturesInternal facialFeatures;
	FacialPose facialPose;
};

class FaceTrackerAssignmentTask {
public:
	FrameNumber frameNumber;
	bool readyForAssignment;
};

class FaceTracker {
public:
	FaceTracker(json config, Status *myStatus, SDLDriver *mySDLDriver, FrameServer *myFrameServer, FaceDetector *myFaceDetector);
	~FaceTracker() noexcept(false);
	void renderPreviewHUD(cv::Mat frame, FrameNumber frameNumber, int density, bool mirrorMode);
	FacialFeatures getFacialFeatures(FrameNumber frameNumber);
	FacialCameraModel getFacialCameraModel(void);
	FacialPose getFacialPose(FrameNumber frameNumber);
	FacialPlane getCalculatedFacialPlaneForWorkingFacialPose(FrameNumber frameNumber, MarkerType markerType);
private:
	void doIdentifyFeatures(WorkerPoolWorker *worker, WorkingFrame *workingFrame, FaceTrackerOutput *output);
	void doInitializeCameraModel(WorkingFrame *workingFrame);
	void doCalculateFacialTransformation(WorkerPoolWorker *worker, WorkingFrame *workingFrame, FaceTrackerOutput *output);
	void doPrecalculateFacialPlaneNormal(WorkerPoolWorker *worker, WorkingFrame *workingFrame, FaceTrackerOutput *output);
	bool doConvertLandmarkPointToImagePoint(DlibPointPointer pointPointer, cv::Point2d *dst, double detectionScaleFactor);
	static void handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps);
	static void predictorWorkerInitializer(WorkerPoolWorker *worker, void *ptr);
	static bool predictorWorkerHandler(WorkerPoolWorker *worker);
	static bool assignmentWorkerHandler(WorkerPoolWorker *worker);

	string featureDetectionModelFileName, faceDetectionModelFileName;
	bool useFullSizedFrameForLandmarkDetection;
	Status *status;
	SDLDriver *sdlDriver;
	FrameServer *frameServer;
	FaceDetector *faceDetector;
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
	cv::Point3d vertexNoseSellion;
	cv::Point3d vertexEyeRightOuterCorner;
	cv::Point3d vertexEyeLeftOuterCorner;
	cv::Point3d vertexRightEar;
	cv::Point3d vertexLeftEar;
	cv::Point3d vertexNoseTip;
	cv::Point3d vertexMenton;
	cv::Point3d vertexStommion;
	double depthSliceA, depthSliceB, depthSliceC, depthSliceD, depthSliceE, depthSliceF, depthSliceG, depthSliceH;

	Logger *logger;
	Metrics *metricsPredictor, *metricsAssignment;

	list<FacialPose> facialPoseSmoothingBuffer;
	FacialPose previouslyReportedFacialPose;
	FacialCameraModel facialCameraModel;

	SDL_mutex *myMutex, *myAssignmentMutex;

	std::list<FrameNumber> pendingPredictionFrameNumbers;
	unordered_map<FrameNumber, FaceTrackerAssignmentTask> pendingAssignmentFrameNumbers;
	unordered_map<FrameNumber, FaceTrackerOutput> outputFrames;

	WorkerPool *predictorWorkerPool, *assignmentWorkerPool;
};

}; //namespace YerFace
