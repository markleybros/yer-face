
#include "FaceTracker.hpp"
#include "Utilities.hpp"

#include "opencv2/calib3d.hpp"
#include "opencv2/highgui.hpp"

#include <exception>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace std;
using namespace dlib;

namespace YerFace {

// Pose recovery approach largely informed by the following sources:
//  - https://www.learnopencv.com/head-pose-estimation-using-opencv-and-dlib/
//  - https://github.com/severin-lemaignan/gazr/
FaceTracker::FaceTracker(json config, Status *myStatus, SDLDriver *mySDLDriver, FrameServer *myFrameServer, FaceDetector *myFaceDetector) {
	featureDetectionModelFileName = config["YerFace"]["FaceTracker"]["dlibFaceLandmarks"];
	previouslyReportedFacialPose.set = false;
	facialCameraModel.set = false;

	status = myStatus;
	if(status == NULL) {
		throw invalid_argument("status cannot be NULL");
	}
	sdlDriver = mySDLDriver;
	if(sdlDriver == NULL) {
		throw invalid_argument("sdlDriver cannot be NULL");
	}
	frameServer = myFrameServer;
	if(frameServer == NULL) {
		throw invalid_argument("frameServer cannot be NULL");
	}
	faceDetector = myFaceDetector;
	if(faceDetector == NULL) {
		throw invalid_argument("faceDetector cannot be NULL");
	}
	poseSmoothingOverSeconds = config["YerFace"]["FaceTracker"]["poseSmoothingOverSeconds"];
	if(poseSmoothingOverSeconds <= 0.0) {
		throw invalid_argument("poseSmoothingOverSeconds cannot be less than or equal to zero.");
	}
	poseSmoothingExponent = config["YerFace"]["FaceTracker"]["poseSmoothingExponent"];
	if(poseSmoothingExponent <= 0.0) {
		throw invalid_argument("poseSmoothingExponent cannot be less than or equal to zero.");
	}
	poseRotationLowRejectionThreshold = config["YerFace"]["FaceTracker"]["poseRotationLowRejectionThreshold"];
	if(poseRotationLowRejectionThreshold <= 0.0) {
		throw invalid_argument("poseRotationLowRejectionThreshold cannot be less than or equal to zero.");
	}
	poseTranslationLowRejectionThreshold = config["YerFace"]["FaceTracker"]["poseTranslationLowRejectionThreshold"];
	if(poseTranslationLowRejectionThreshold <= 0.0) {
		throw invalid_argument("poseRotationLowRejectionThreshold cannot be less than or equal to zero.");
	}
	poseRotationLowRejectionThresholdInternal = config["YerFace"]["FaceTracker"]["poseRotationLowRejectionThresholdInternal"];
	if(poseRotationLowRejectionThresholdInternal <= 0.0) {
		throw invalid_argument("poseRotationLowRejectionThresholdInternal cannot be less than or equal to zero.");
	}
	poseTranslationLowRejectionThresholdInternal = config["YerFace"]["FaceTracker"]["poseTranslationLowRejectionThresholdInternal"];
	if(poseTranslationLowRejectionThresholdInternal <= 0.0) {
		throw invalid_argument("poseRotationLowRejectionThresholdInternal cannot be less than or equal to zero.");
	}
	poseRotationHighRejectionThreshold = config["YerFace"]["FaceTracker"]["poseRotationHighRejectionThreshold"];
	if(poseRotationHighRejectionThreshold <= 0.0) {
		throw invalid_argument("poseRotationHighRejectionThreshold cannot be less than or equal to zero.");
	}
	poseTranslationHighRejectionThreshold = config["YerFace"]["FaceTracker"]["poseTranslationHighRejectionThreshold"];
	if(poseTranslationHighRejectionThreshold <= 0.0) {
		throw invalid_argument("poseRotationHighRejectionThreshold cannot be less than or equal to zero.");
	}
	poseRejectionResetAfterSeconds = config["YerFace"]["FaceTracker"]["poseRejectionResetAfterSeconds"];
	if(poseRejectionResetAfterSeconds <= 0.0) {
		throw invalid_argument("poseRejectionResetAfterSeconds cannot be less than or equal to zero.");
	}
	poseTranslationMaxX = config["YerFace"]["FaceTracker"]["poseTranslationMaxX"];
	poseTranslationMinX = config["YerFace"]["FaceTracker"]["poseTranslationMinX"];
	poseTranslationMaxY = config["YerFace"]["FaceTracker"]["poseTranslationMaxY"];
	poseTranslationMinY = config["YerFace"]["FaceTracker"]["poseTranslationMinY"];
	poseTranslationMaxZ = config["YerFace"]["FaceTracker"]["poseTranslationMaxZ"];
	poseTranslationMinZ = config["YerFace"]["FaceTracker"]["poseTranslationMinZ"];
	poseRotationPlusMinusX = config["YerFace"]["FaceTracker"]["poseRotationPlusMinusX"];
	poseRotationPlusMinusY = config["YerFace"]["FaceTracker"]["poseRotationPlusMinusY"];
	poseRotationPlusMinusZ = config["YerFace"]["FaceTracker"]["poseRotationPlusMinusZ"];
	vertexNoseSellion = Utilities::Point3dFromJSONArray((json)config["YerFace"]["FaceTracker"]["solvePnPVertices"]["vertexNoseSellion"]);
	vertexEyeRightOuterCorner = Utilities::Point3dFromJSONArray((json)config["YerFace"]["FaceTracker"]["solvePnPVertices"]["vertexEyeRightOuterCorner"]);
	vertexEyeLeftOuterCorner = Utilities::Point3dFromJSONArray((json)config["YerFace"]["FaceTracker"]["solvePnPVertices"]["vertexEyeLeftOuterCorner"]);
	vertexRightEar = Utilities::Point3dFromJSONArray((json)config["YerFace"]["FaceTracker"]["solvePnPVertices"]["vertexRightEar"]);
	vertexLeftEar = Utilities::Point3dFromJSONArray((json)config["YerFace"]["FaceTracker"]["solvePnPVertices"]["vertexLeftEar"]);
	vertexNoseTip = Utilities::Point3dFromJSONArray((json)config["YerFace"]["FaceTracker"]["solvePnPVertices"]["vertexNoseTip"]);
	vertexStommion = Utilities::Point3dFromJSONArray((json)config["YerFace"]["FaceTracker"]["solvePnPVertices"]["vertexStommion"]);
	vertexMenton = Utilities::Point3dFromJSONArray((json)config["YerFace"]["FaceTracker"]["solvePnPVertices"]["vertexMenton"]);
	depthSliceA = config["YerFace"]["FaceTracker"]["depthSlices"]["A"];
	depthSliceB = config["YerFace"]["FaceTracker"]["depthSlices"]["B"];
	depthSliceC = config["YerFace"]["FaceTracker"]["depthSlices"]["C"];
	depthSliceD = config["YerFace"]["FaceTracker"]["depthSlices"]["D"];
	depthSliceE = config["YerFace"]["FaceTracker"]["depthSlices"]["E"];
	depthSliceF = config["YerFace"]["FaceTracker"]["depthSlices"]["F"];
	depthSliceG = config["YerFace"]["FaceTracker"]["depthSlices"]["G"];
	depthSliceH = config["YerFace"]["FaceTracker"]["depthSlices"]["H"];

	logger = new Logger("FaceTracker");
	metricsPredictor = new Metrics(config, "FaceTracker.Predictor");
	metricsAssignment = new Metrics(config, "FaceTracker.Assignment");

	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((myAssignmentMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	//We want to know when any frame has entered various statuses.
	FrameStatusChangeEventCallback frameStatusChangeCallback;
	frameStatusChangeCallback.userdata = (void *)this;
	frameStatusChangeCallback.callback = handleFrameStatusChange;
	frameStatusChangeCallback.newStatus = FRAME_STATUS_NEW;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_TRACKING;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_GONE;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);

	//We also want to introduce a checkpoint so that frames cannot TRANSITION AWAY from FRAME_STATUS_TRACKING without our blessing.
	frameServer->registerFrameStatusCheckpoint(FRAME_STATUS_TRACKING, "faceTracker.ran");

	WorkerPoolParameters workerPoolParameters;
	workerPoolParameters.name = "FaceTracker.Predictor";
	workerPoolParameters.numWorkers = config["YerFace"]["FaceTracker"]["numWorkers"];
	workerPoolParameters.numWorkersPerCPU = config["YerFace"]["FaceTracker"]["numWorkersPerCPU"];
	workerPoolParameters.initializer = predictorWorkerInitializer;
	workerPoolParameters.deinitializer = NULL;
	workerPoolParameters.usrPtr = (void *)this;
	workerPoolParameters.handler = predictorWorkerHandler;
	predictorWorkerPool = new WorkerPool(config, status, frameServer, workerPoolParameters);

	workerPoolParameters.name = "FaceTracker.Assignment";
	workerPoolParameters.numWorkers = 1;
	workerPoolParameters.numWorkersPerCPU = 0.0;
	workerPoolParameters.initializer = NULL;
	workerPoolParameters.deinitializer = NULL;
	workerPoolParameters.usrPtr = (void *)this;
	workerPoolParameters.handler = assignmentWorkerHandler;
	assignmentWorkerPool = new WorkerPool(config, status, frameServer, workerPoolParameters);

	logger->debug("FaceTracker object constructed and ready to go!");
}

FaceTracker::~FaceTracker() noexcept(false) {
	logger->debug("FaceTracker object destructing...");

	delete predictorWorkerPool;

	YerFace_MutexLock(myMutex);
	if(pendingPredictionFrameNumbers.size() > 0) {
		logger->error("Frames are still pending! Woe is me!");
	}
	if(outputFrames.size() > 0) {
		logger->error("Outputs are still pending! Woe is me!");
	}
	YerFace_MutexUnlock(myMutex);

	YerFace_MutexLock(myAssignmentMutex);
	if(pendingAssignmentFrameNumbers.size() > 0) {
		logger->error("Assignment Frames are still pending! Woe is me!");
	}
	YerFace_MutexUnlock(myAssignmentMutex);

	SDL_DestroyMutex(myMutex);
	SDL_DestroyMutex(myAssignmentMutex);
	delete metricsPredictor;
	delete logger;
}

void FaceTracker::doIdentifyFeatures(WorkerPoolWorker *worker, WorkingFrame *workingFrame, FaceTrackerOutput *output) {
	FaceTrackerWorker *innerWorker = (FaceTrackerWorker *)worker->ptr;
	FacialDetectionBox facialDetection = faceDetector->getFacialDetection(output->frameNumber);
	if(!facialDetection.set) {
		return;
	}

	// FIXME - Currently we're using the full sized frame as input to the Shape Predictor.
	// This seems fine, and the performance is quite reasonable. However, workingFrame->detectionFrame
	// is usually smaller, and Shape Predictor definitely runs faster on it, albiet at a
	// noticeable reduction in quality. Ideally we should expose this choice to the user,
	// although I'm not sure yet how.
	Mat searchFrame = workingFrame->frame;
	double searchFrameScaleFactor = 1.0; //workingFrame->detectionScaleFactor (see above)
	Rect2d searchRect = facialDetection.boxNormalSize;

	dlib::cv_image<dlib::bgr_pixel> dlibSearchFrame = cv_image<bgr_pixel>(searchFrame);
	dlib::rectangle dlibSearchBox = dlib::rectangle(
		searchRect.x * searchFrameScaleFactor,
		searchRect.y * searchFrameScaleFactor,
		(searchRect.width + searchRect.x) * searchFrameScaleFactor,
		(searchRect.height + searchRect.y) * searchFrameScaleFactor);

	full_object_detection result = innerWorker->shapePredictor(dlibSearchFrame, dlibSearchBox);

	output->facialFeatures.featuresExposed.features.clear();
	output->facialFeatures.featuresExposed.features.resize(result.num_parts());

	output->facialFeatures.features.clear();
	output->facialFeatures.features3D.clear();
	dlib::point part;
	Point2d partPoint;
	for(unsigned long featureIndex = 0; featureIndex < result.num_parts(); featureIndex++) {
		part = result.part(featureIndex);
		if(!doConvertLandmarkPointToImagePoint(&part, &partPoint, searchFrameScaleFactor)) {
			return;
		}

		output->facialFeatures.featuresExposed.features[featureIndex] = partPoint;

		bool pushCorrelationPoint = false;
		switch(featureIndex) {
			case IDX_NOSE_SELLION:
				output->facialFeatures.features3D.push_back(vertexNoseSellion);
				pushCorrelationPoint = true;
				break;
			case IDX_RIGHTEYE_OUTER_CORNER:
				output->facialFeatures.features3D.push_back(vertexEyeRightOuterCorner);
				pushCorrelationPoint = true;
				break;
			case IDX_LEFTEYE_OUTER_CORNER:
				output->facialFeatures.features3D.push_back(vertexEyeLeftOuterCorner);
				pushCorrelationPoint = true;
				break;
			case IDX_JAWLINE_0:
				output->facialFeatures.features3D.push_back(vertexRightEar);
				pushCorrelationPoint = true;
				break;
			case IDX_JAWLINE_16:
				output->facialFeatures.features3D.push_back(vertexLeftEar);
				pushCorrelationPoint = true;
				break;
			case IDX_NOSE_TIP:
				output->facialFeatures.features3D.push_back(vertexNoseTip);
				pushCorrelationPoint = true;
				break;
			case IDX_JAWLINE_8:
				output->facialFeatures.features3D.push_back(vertexMenton);
				pushCorrelationPoint = true;
				break;
		}
		if(pushCorrelationPoint) {
			output->facialFeatures.features.push_back(partPoint);
		}
	}

	//Stommion needs a little extra help.
	part = result.part(IDX_MOUTHIN_CENTER_TOP);
	Point2d mouthTop;
	if(!doConvertLandmarkPointToImagePoint(&part, &mouthTop, searchFrameScaleFactor)) {
		return;
	}
	part = result.part(IDX_MOUTHIN_CENTER_BOTTOM);
	Point2d mouthBottom;
	if(!doConvertLandmarkPointToImagePoint(&part, &mouthBottom, searchFrameScaleFactor)) {
		return;
	}
	partPoint = (mouthTop + mouthTop + mouthBottom) / 3.0;
	output->facialFeatures.features.push_back(partPoint);
	output->facialFeatures.features3D.push_back(vertexStommion);
	output->facialFeatures.set = true;
	output->facialFeatures.featuresExposed.set = true;
}

void FaceTracker::doInitializeCameraModel(WorkingFrame *workingFrame) {
	//Totally fake, idealized camera.
	Size frameSize = workingFrame->frame.size();
	double focalLength = frameSize.width;
	Point2d center = Point2d(frameSize.width / 2, frameSize.height / 2);

	YerFace_MutexLock(myAssignmentMutex);
	facialCameraModel.cameraMatrix = Utilities::generateFakeCameraMatrix(focalLength, center);
	facialCameraModel.distortionCoefficients = Mat::zeros(4, 1, DataType<double>::type);
	facialCameraModel.set = true;
	YerFace_MutexUnlock(myAssignmentMutex);
}

void FaceTracker::doCalculateFacialTransformation(WorkerPoolWorker *worker, WorkingFrame *workingFrame, FaceTrackerOutput *output) {
	if(!output->facialFeatures.set) {
		previouslyReportedFacialPose.set = false;
		return;
	}

	FacialCameraModel camera = facialCameraModel;

	FrameTimestamps frameTimestamps = workingFrame->frameTimestamps;
	double frameTimestamp = frameTimestamps.startTimestamp;
	FacialPose tempPose;
	tempPose.timestamp = frameTimestamp;
	tempPose.set = false;
	Mat tempRotationVector;

	//// DO FACIAL POSE SOLUTION ////

	solvePnP(output->facialFeatures.features3D, output->facialFeatures.features, camera.cameraMatrix, camera.distortionCoefficients, tempRotationVector, tempPose.translationVector);
	tempRotationVector.at<double>(0) = tempRotationVector.at<double>(0) * -1.0;
	tempRotationVector.at<double>(1) = tempRotationVector.at<double>(1) * -1.0;
	Rodrigues(tempRotationVector, tempPose.rotationMatrix);

	//// REJECT BAD / OUT OF BOUNDS FACIAL POSES ////

	bool reportNewPose = true;
	double degreesDifference, distance, scaledRotationThreshold, scaledTranslationThreshold;
	double timeScale = (double)(frameTimestamps.estimatedEndTimestamp - frameTimestamps.startTimestamp) / (double)(1.0 / 30.0);
	if(previouslyReportedFacialPose.set) {
		scaledRotationThreshold = poseRotationHighRejectionThreshold * timeScale;
		scaledTranslationThreshold = poseTranslationHighRejectionThreshold * timeScale;
		degreesDifference = Utilities::degreesDifferenceBetweenTwoRotationMatrices(previouslyReportedFacialPose.rotationMatrix, tempPose.rotationMatrix);
		distance = Utilities::lineDistance(Point3d(tempPose.translationVector), Point3d(previouslyReportedFacialPose.translationVector));
		if(degreesDifference > scaledRotationThreshold || distance > scaledTranslationThreshold) {
			logger->warn("Dropping facial pose due to high rotation (%.02lf) or high motion (%.02lf)!", degreesDifference, distance);
			reportNewPose = false;
		}
	}
	if(tempPose.translationVector.at<double>(0) < poseTranslationMinX || tempPose.translationVector.at<double>(0) > poseTranslationMaxX ||
	  tempPose.translationVector.at<double>(1) < poseTranslationMinY || tempPose.translationVector.at<double>(1) > poseTranslationMaxY ||
	  tempPose.translationVector.at<double>(2) < poseTranslationMinZ || tempPose.translationVector.at<double>(2) > poseTranslationMaxZ) {
		logger->warn("Dropping facial pose due to out of bounds translation: <%.02f, %.02f, %.02f>", tempPose.translationVector.at<double>(0), tempPose.translationVector.at<double>(1), tempPose.translationVector.at<double>(2));
		reportNewPose = false;
	}
	Vec3d angles = Utilities::rotationMatrixToEulerAngles(tempPose.rotationMatrix);

	if(fabs(angles[0]) > poseRotationPlusMinusX || fabs(angles[1]) > poseRotationPlusMinusY || fabs(angles[2]) > poseRotationPlusMinusZ) {
		logger->warn("Dropping facial pose due to out of bounds angle: <%.02f, %.02f, %.02f>", angles[0], angles[1], angles[2]);
		reportNewPose = false;
	}
	if(!reportNewPose) {
		if(previouslyReportedFacialPose.set) {
			if(tempPose.timestamp - previouslyReportedFacialPose.timestamp >= poseRejectionResetAfterSeconds) {
				logger->warn("Facial pose has come back bad consistantly for %.02lf seconds! Unsetting the face pose completely.", tempPose.timestamp - previouslyReportedFacialPose.timestamp);
				previouslyReportedFacialPose.set = false;
			}
			output->facialPose = previouslyReportedFacialPose;
		} else {
			output->facialPose.set = false;
		}
		return;
	}

	//// DO FACIAL POSE SMOOTHING ////

	facialPoseSmoothingBuffer.push_back(tempPose);
	while(facialPoseSmoothingBuffer.front().timestamp <= (frameTimestamp - poseSmoothingOverSeconds)) {
		facialPoseSmoothingBuffer.pop_front();
	}

	tempPose.translationVector = (Mat_<double>(3,1) << 0.0, 0.0, 0.0);
	tempPose.rotationMatrix = (Mat_<double>(3,3) <<
			0.0, 0.0, 0.0,
			0.0, 0.0, 0.0,
			0.0, 0.0, 0.0);

	double combinedWeights = 0.0;
	for(FacialPose pose : facialPoseSmoothingBuffer) {
		double progress = (pose.timestamp - (frameTimestamp - poseSmoothingOverSeconds)) / poseSmoothingOverSeconds;
		double weight = std::pow(progress, (double)poseSmoothingExponent) - combinedWeights;
		combinedWeights += weight;
		for(int j = 0; j < 3; j++) {
			tempPose.translationVector.at<double>(j) += pose.translationVector.at<double>(j) * weight;
		}
		for(int j = 0; j < 9; j++) {
			tempPose.rotationMatrix.at<double>(j) += pose.rotationMatrix.at<double>(j) * weight;
		}
	}

	tempPose.set = true;
	angles = Utilities::rotationMatrixToEulerAngles(tempPose.rotationMatrix);
	// logger->verbose("Facial Pose Angle: <%.02f, %.02f, %.02f>; Translation: <%.02f, %.02f, %.02f>", angles[0], angles[1], angles[2], tempPose.translationVector.at<double>(0), tempPose.translationVector.at<double>(1), tempPose.translationVector.at<double>(2));

	//// REJECT NOISY SOLUTIONS ////

	tempPose.rotationMatrixInternal = tempPose.rotationMatrix.clone();
	tempPose.translationVectorInternal = tempPose.translationVector.clone();
	if(previouslyReportedFacialPose.set) {
		int i;
		double delta;

		// Do the de-noising thing first for the externally-facing matrices
		Vec3d prevAngles = Utilities::rotationMatrixToEulerAngles(previouslyReportedFacialPose.rotationMatrix);
		scaledRotationThreshold = poseRotationLowRejectionThreshold * timeScale;
		scaledTranslationThreshold = poseTranslationLowRejectionThreshold * timeScale;

		for(i = 0; i < 3; i++) {
			delta = angles[i] - prevAngles[i];
			if(fabs(delta) <= scaledRotationThreshold) {
				angles[i] = prevAngles[i];
			}
		}
		tempPose.rotationMatrix = Utilities::eulerAnglesToRotationMatrix(angles);

		for(i = 0; i < 3; i++) {
			delta = tempPose.translationVector.at<double>(i) - previouslyReportedFacialPose.translationVector.at<double>(i);
			if(fabs(delta) <= scaledTranslationThreshold) {
				tempPose.translationVector.at<double>(i) = previouslyReportedFacialPose.translationVector.at<double>(i);
			}
		}

		// Do the de-noising thing again, but for the internally-facing matrices
		prevAngles = Utilities::rotationMatrixToEulerAngles(previouslyReportedFacialPose.rotationMatrixInternal);
		scaledRotationThreshold = poseRotationLowRejectionThresholdInternal * timeScale;
		scaledTranslationThreshold = poseTranslationLowRejectionThresholdInternal * timeScale;

		for(i = 0; i < 3; i++) {
			delta = angles[i] - prevAngles[i];
			if(fabs(delta) <= scaledRotationThreshold) {
				angles[i] = prevAngles[i];
			}
		}
		tempPose.rotationMatrixInternal = Utilities::eulerAnglesToRotationMatrix(angles);

		for(i = 0; i < 3; i++) {
			delta = tempPose.translationVectorInternal.at<double>(i) - previouslyReportedFacialPose.translationVectorInternal.at<double>(i);
			if(fabs(delta) <= scaledTranslationThreshold) {
				tempPose.translationVectorInternal.at<double>(i) = previouslyReportedFacialPose.translationVectorInternal.at<double>(i);
			}
		}
	}

	output->facialPose = tempPose;
	previouslyReportedFacialPose = output->facialPose;
}

void FaceTracker::doPrecalculateFacialPlaneNormal(WorkerPoolWorker *worker, WorkingFrame *workingFrame, FaceTrackerOutput *output) {
	if(!output->facialPose.set) {
		return;
	}
	Mat planeNormalMat = (Mat_<double>(3, 1) << 0.0, 0.0, -1.0);
	planeNormalMat = output->facialPose.rotationMatrix * planeNormalMat;
	output->facialPose.facialPlaneNormal = Vec3d(planeNormalMat.at<double>(0), planeNormalMat.at<double>(1), planeNormalMat.at<double>(2));
}

bool FaceTracker::doConvertLandmarkPointToImagePoint(dlib::point *src, Point2d *dst, double detectionScaleFactor) {
	if(*src == OBJECT_PART_NOT_PRESENT) {
		return false;
	}
	*dst = Point2d(src->x(), src->y());
	dst->x /= detectionScaleFactor;
	dst->y /= detectionScaleFactor;
	return true;
}

void FaceTracker::renderPreviewHUD(Mat frame, FrameNumber frameNumber, int density) {
	YerFace_MutexLock(myMutex);
	if(frameNumber < 0 || outputFrames.find(frameNumber) == outputFrames.end()) {
		YerFace_MutexUnlock(myMutex);
		throw invalid_argument("FaceTracker::renderPreviewHUD() passed invalid frame number");
	}
	FaceTrackerOutput output = outputFrames[frameNumber];
	YerFace_MutexUnlock(myMutex);

	YerFace_MutexLock(myAssignmentMutex);
	FacialCameraModel camera = facialCameraModel;
	YerFace_MutexUnlock(myAssignmentMutex);

	if(density > 0) {
		if(output.facialPose.set) {
			std::vector<Point3d> gizmo3d(6);
			std::vector<Point2d> gizmo2d;
			gizmo3d[0] = Point3d(-50,0.0,0.0);
			gizmo3d[1] = Point3d(50,0.0,0.0);
			gizmo3d[2] = Point3d(0.0,50,0.0);
			gizmo3d[3] = Point3d(0.0,-50,0.0);
			gizmo3d[4] = Point3d(0.0,0.0,50);
			gizmo3d[5] = Point3d(0.0,0.0,-50);
			
			Mat tempRotationVector;
			Rodrigues(output.facialPose.rotationMatrix, tempRotationVector);
			projectPoints(gizmo3d, tempRotationVector, output.facialPose.translationVector, camera.cameraMatrix, camera.distortionCoefficients, gizmo2d);
			arrowedLine(frame, gizmo2d[0], gizmo2d[1], Scalar(0, 0, 255), 2);
			arrowedLine(frame, gizmo2d[2], gizmo2d[3], Scalar(255, 0, 0), 2);
			arrowedLine(frame, gizmo2d[4], gizmo2d[5], Scalar(0, 255, 0), 2);
		}
	}
	if(density > 3) {
		if(output.facialFeatures.set) {
			for(auto feature : output.facialFeatures.featuresExposed.features) {
				Utilities::drawX(frame, feature, Scalar(147, 20, 255));
			}
		}
	}

	if(density > 4) {
		if(output.facialPose.set) {
			std::vector<Point3d> edges3d;
			std::vector<Point2d> edges2d;

			std::list<double> depths = { depthSliceA, depthSliceB, depthSliceC, depthSliceD, depthSliceE, depthSliceF, depthSliceG };
			for(double depth : depths) {
				double planeWidth = 300;
				double gridIncrement = 25;
				double planeEdge = (planeWidth / 2.0);
				for(double x = planeEdge * -1.0; x <= planeEdge; x = x + gridIncrement) {
					edges3d.push_back(Point3d(x, planeEdge, depth));
					edges3d.push_back(Point3d(x, planeEdge * -1.0, depth));
				}
				for(double y = planeEdge * -1.0; y <= planeEdge; y = y + gridIncrement) {
					edges3d.push_back(Point3d(planeEdge, y, depth));
					edges3d.push_back(Point3d(planeEdge * -1.0, y, depth));
				}
			}

			projectPoints(edges3d, output.facialPose.rotationMatrix, output.facialPose.translationVector, camera.cameraMatrix, camera.distortionCoefficients, edges2d);

			for(unsigned int i = 0; i + 1 < edges2d.size(); i = i + 2) {
				cv::line(frame, edges2d[i], edges2d[i + 1], Scalar(255, 255, 255));
			}
		}
	}
}

FacialFeatures FaceTracker::getFacialFeatures(FrameNumber frameNumber) {
	FacialFeatures val;
	YerFace_MutexLock(myMutex);
	if(frameNumber < 0 || outputFrames.find(frameNumber) == outputFrames.end()) {
		YerFace_MutexUnlock(myMutex);
		throw invalid_argument("FaceTracker::getFacialFeatures() passed invalid frame number");
	}
	val = outputFrames[frameNumber].facialFeatures.featuresExposed;
	YerFace_MutexUnlock(myMutex);
	return val;
}

FacialCameraModel FaceTracker::getFacialCameraModel(void) {
	FacialCameraModel val;
	YerFace_MutexLock(myAssignmentMutex);
	val = facialCameraModel;
	YerFace_MutexUnlock(myAssignmentMutex);
	return val;
}

FacialPose FaceTracker::getFacialPose(FrameNumber frameNumber) {
	FacialPose val;
	YerFace_MutexLock(myMutex);
	if(frameNumber < 0 || outputFrames.find(frameNumber) == outputFrames.end()) {
		YerFace_MutexUnlock(myMutex);
		throw invalid_argument("FaceTracker::getFacialPose() passed invalid frame number");
	}
	val = outputFrames[frameNumber].facialPose;
	YerFace_MutexUnlock(myMutex);
	return val;
}

FacialPlane FaceTracker::getCalculatedFacialPlaneForWorkingFacialPose(FrameNumber frameNumber, MarkerType markerType) {
	FacialPose facialPose;
	YerFace_MutexLock(myMutex);
	if(frameNumber < 0 || outputFrames.find(frameNumber) == outputFrames.end()) {
		YerFace_MutexUnlock(myMutex);
		throw invalid_argument("FaceTracker::getCalculatedFacialPlaneForWorkingFacialPose() passed invalid frame number");
	}
	facialPose = outputFrames[frameNumber].facialPose;
	YerFace_MutexUnlock(myMutex);

	if(!facialPose.set) {
		throw runtime_error("Can't do FaceTracker::getCalculatedFacialPlaneForWorkingFacialPose() when no working FacialPose is set.");
	}

	double depth = 0;
	switch(markerType.type) {
		default:
			throw runtime_error("Unsupported MarkerType!");
		case EyelidLeftTop:
		case EyelidLeftBottom:
		case EyelidRightTop:
		case EyelidRightBottom:
			depth = depthSliceG;
			break;
		case EyebrowLeftInner:
		case EyebrowRightInner:
			depth = depthSliceE;
			break;
		case EyebrowLeftMiddle:
		case EyebrowRightMiddle:
			depth = depthSliceF;
			break;
		case EyebrowLeftOuter:
		case EyebrowRightOuter:
			depth = depthSliceH;
			break;
		case LipsLeftCorner:
		case LipsRightCorner:
			depth = depthSliceD;
			break;
		case LipsLeftTop:
		case LipsRightTop:
			depth = depthSliceC;
			break;
		case LipsLeftBottom:
		case LipsRightBottom:
			depth = depthSliceB;
			break;
		case Jaw:
			depth = depthSliceA;
			break;
	}

	Mat translationOffset = (Mat_<double>(3,1) << 0.0, 0.0, depth);
	translationOffset = facialPose.rotationMatrix * translationOffset;

	FacialPlane facialPlane;
	Mat translationVector = facialPose.translationVector + translationOffset;
	facialPlane.planePoint = Point3d(translationVector.at<double>(0), translationVector.at<double>(1), translationVector.at<double>(2));
	facialPlane.planeNormal = facialPose.facialPlaneNormal;

	return facialPlane;
}

void FaceTracker::handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps) {
	FrameNumber frameNumber = frameTimestamps.frameNumber;
	FaceTracker *self = (FaceTracker *)userdata;
	FaceTrackerOutput output;
	FaceTrackerAssignmentTask assignment;
	switch(newStatus) {
		default:
			throw logic_error("Handler passed unsupported frame status change event!");
		case FRAME_STATUS_NEW:
			output.set = false;
			output.facialFeatures.set = false;
			output.facialFeatures.featuresExposed.set = false;
			output.facialPose.set = false;
			YerFace_MutexLock(self->myMutex);
			self->outputFrames[frameNumber] = output;
			YerFace_MutexUnlock(self->myMutex);
			assignment.frameNumber = frameNumber;
			assignment.readyForAssignment = false;
			YerFace_MutexLock(self->myAssignmentMutex);
			self->pendingAssignmentFrameNumbers[frameNumber] = assignment;
			YerFace_MutexUnlock(self->myAssignmentMutex);
			break;
		case FRAME_STATUS_TRACKING:
			// self->logger->verbose("handleFrameStatusChange() Frame #%lld waiting on me. Queue depth is now %lu", frameNumber, self->pendingFrameNumbers.size());
			YerFace_MutexLock(self->myMutex);
			self->pendingPredictionFrameNumbers.push_back(frameNumber);
			YerFace_MutexUnlock(self->myMutex);
			self->predictorWorkerPool->sendWorkerSignal();
			break;
		case FRAME_STATUS_GONE:
			YerFace_MutexLock(self->myMutex);
			self->outputFrames.erase(frameNumber);
			YerFace_MutexUnlock(self->myMutex);
			break;
	}
}

void FaceTracker::predictorWorkerInitializer(WorkerPoolWorker *worker, void *ptr) {
	FaceTracker *self = (FaceTracker *)ptr;
	FaceTrackerWorker *innerWorker = new FaceTrackerWorker();
	innerWorker->self = self;
	deserialize(self->featureDetectionModelFileName.c_str()) >> innerWorker->shapePredictor;
	worker->ptr = (void *)innerWorker;
}

bool FaceTracker::predictorWorkerHandler(WorkerPoolWorker *worker) {
	FaceTrackerWorker *innerWorker = (FaceTrackerWorker *)worker->ptr;
	FaceTracker *self = innerWorker->self;

	bool didWork = false;
	FrameNumber myFrameNumber = -1;

	YerFace_MutexLock(self->myMutex);
	//// CHECK FOR WORK ////
	if(self->pendingPredictionFrameNumbers.size() > 0) {
		myFrameNumber = self->pendingPredictionFrameNumbers.front();
		self->pendingPredictionFrameNumbers.pop_front();
	}
	YerFace_MutexUnlock(self->myMutex);

	//// DO THE WORK ////
	if(myFrameNumber > 0) {
		MetricsTick tick = self->metricsPredictor->startClock();

		WorkingFrame *workingFrame = self->frameServer->getWorkingFrame(myFrameNumber);

		FaceTrackerOutput output;
		output.set = false;
		output.facialFeatures.set = false;
		output.facialFeatures.featuresExposed.set = false;
		output.facialPose.set = false;
		output.frameNumber = myFrameNumber;

		self->doIdentifyFeatures(worker, workingFrame, &output);

		YerFace_MutexLock(self->myMutex);
		self->outputFrames[myFrameNumber] = output;
		YerFace_MutexUnlock(self->myMutex);

		YerFace_MutexLock(self->myAssignmentMutex);
		self->pendingAssignmentFrameNumbers[myFrameNumber].readyForAssignment = true;
		YerFace_MutexUnlock(self->myAssignmentMutex);
		self->assignmentWorkerPool->sendWorkerSignal();

		self->metricsPredictor->endClock(tick);
		didWork = true;
	}

	return didWork;
}

bool FaceTracker::assignmentWorkerHandler(WorkerPoolWorker *worker) {
	FaceTracker *self = (FaceTracker *)worker->ptr;

	bool didWork = false;
	FrameNumber myFrameNumber = -1;
	static FrameNumber lastFrameNumber = -1;

	YerFace_MutexLock(self->myAssignmentMutex);
	//// CHECK FOR WORK ////
	for(auto pendingAssignmentPair : self->pendingAssignmentFrameNumbers) {
		if(myFrameNumber < 0 || pendingAssignmentPair.first < myFrameNumber) {
			myFrameNumber = pendingAssignmentPair.first;
		}
	}
	if(myFrameNumber > 0 && !self->pendingAssignmentFrameNumbers[myFrameNumber].readyForAssignment) {
		// self->logger->verbose("BLOCKED on frame %ld because it is not ready!", myFrameNumber);
		myFrameNumber = -1;
	}
	if(myFrameNumber > 0) {
		self->pendingAssignmentFrameNumbers.erase(myFrameNumber);
	}
	YerFace_MutexUnlock(self->myAssignmentMutex);

	YerFace_MutexLock(self->myMutex);
	FaceTrackerOutput output;
	if(myFrameNumber > 0) {
		output = self->outputFrames[myFrameNumber];
	}
	YerFace_MutexUnlock(self->myMutex);

	//// DO THE WORK ////
	if(myFrameNumber > 0) {
		// self->logger->verbose("Face Tracker Assignment Thread handling frame #%lld", myFrameNumber);
		if(myFrameNumber <= lastFrameNumber) {
			throw logic_error("FaceTracker handling frames out of order!");
		}
		lastFrameNumber = myFrameNumber;

		MetricsTick tick = self->metricsAssignment->startClock();

		WorkingFrame *workingFrame = self->frameServer->getWorkingFrame(myFrameNumber);
		
		YerFace_MutexLock(self->myAssignmentMutex);
		if(!self->facialCameraModel.set) {
			self->doInitializeCameraModel(workingFrame);
		}
		self->doCalculateFacialTransformation(worker, workingFrame, &output);
		self->doPrecalculateFacialPlaneNormal(worker, workingFrame, &output);
		YerFace_MutexUnlock(self->myAssignmentMutex);

		YerFace_MutexLock(self->myMutex);
		self->outputFrames[myFrameNumber] = output;
		YerFace_MutexUnlock(self->myMutex);

		self->frameServer->setWorkingFrameStatusCheckpoint(myFrameNumber, FRAME_STATUS_TRACKING, "faceTracker.ran");
		self->metricsAssignment->endClock(tick);

		didWork = true;
	}

	return didWork;
}

}; //namespace YerFace
