
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

FaceTracker::FaceTracker(json config, SDLDriver *mySDLDriver, FrameDerivatives *myFrameDerivatives, bool myLowLatency) {
	featureDetectionModelFileName = config["YerFace"]["FaceTracker"]["dlibFaceLandmarks"];
	faceDetectionModelFileName = config["YerFace"]["FaceTracker"]["dlibFaceDetector"];
	working.faceRect.set = false;
	working.facialFeatures.set = false;
	working.facialFeatures.featuresExposed.set = false;
	working.facialPose.set = false;
	working.previouslyReportedFacialPose.set = false;
	complete.faceRect.set = false;
	complete.facialFeatures.set = false;
	complete.facialPose.set = false;
	newestClassificationBox.run = false;
	newestClassificationBox.set = false;
	facialCameraModel.set = false;

	sdlDriver = mySDLDriver;
	if(sdlDriver == NULL) {
		throw invalid_argument("sdlDriver cannot be NULL");
	}
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	lowLatency = myLowLatency;
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
	metrics = new Metrics(config, "FaceTracker", frameDerivatives);

	if((myCmpMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((myClassificationMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	deserialize(featureDetectionModelFileName.c_str()) >> shapePredictor;
	if(faceDetectionModelFileName.length() > 0) {
		usingDNNFaceDetection = true;
		deserialize(faceDetectionModelFileName.c_str()) >> faceDetectionModel;
	} else {
		usingDNNFaceDetection = false;
		frontalFaceDetector = get_frontal_face_detector();
	}

	classifierRunning = false;
	if(lowLatency) {
		classifierRunning = true;
		if((classifierThread = SDL_CreateThread(FaceTracker::runClassificationLoop, "TrackerLoop", (void *)this)) == NULL) {
			throw runtime_error("Failed starting thread!");
		}
	}

	logger->debug("FaceTracker object constructed and ready to go! Using Face Detection Method: %s, Low Latency Mode: %s", usingDNNFaceDetection ? "DNN" : "HOG", lowLatency ? "Enabled" : "Disabled");
}

FaceTracker::~FaceTracker() noexcept(false) {
	logger->debug("FaceTracker object destructing...");
	if(lowLatency) {
		YerFace_MutexLock(myClassificationMutex);
		classifierRunning = false;
		YerFace_MutexUnlock(myClassificationMutex);
		SDL_WaitThread(classifierThread, NULL);
	}
	SDL_DestroyMutex(myCmpMutex);
	SDL_DestroyMutex(myClassificationMutex);
	delete metrics;
	delete logger;
}

// Pose recovery approach largely informed by the following sources:
//  - https://www.learnopencv.com/head-pose-estimation-using-opencv-and-dlib/
//  - https://github.com/severin-lemaignan/gazr/
void FaceTracker::processCurrentFrame(void) {
	metrics->startClock();

	ClassificationFrame classificationFrame = frameDerivatives->getClassificationFrame();

	if(!lowLatency) {
		doClassifyFace(classificationFrame);
	} else {
		static bool didClassifierRun = false;
		while(!didClassifierRun) {
			YerFace_MutexLock(myClassificationMutex);
			if(newestClassificationBox.run) {
				YerFace_MutexUnlock(myClassificationMutex);
				didClassifierRun = true;
				break;
			}
			YerFace_MutexUnlock(myClassificationMutex);
			SDL_Delay(10);
		}
	}

	assignFaceRect();

	doIdentifyFeatures(classificationFrame);

	doCalculateFacialTransformation();

	doPrecalculateFacialPlaneNormal();

	metrics->endClock();
}

void FaceTracker::advanceWorkingToCompleted(void) {
	YerFace_MutexLock(myCmpMutex);
	complete = working;
	YerFace_MutexUnlock(myCmpMutex);
	working.faceRect.set = false;
	working.facialFeatures.set = false;
	working.facialFeatures.featuresExposed.set = false;
	working.facialPose.set = false;
}

void FaceTracker::doClassifyFace(ClassificationFrame classificationFrame) {
	dlib::cv_image<dlib::bgr_pixel> dlibClassificationFrame = cv_image<bgr_pixel>(classificationFrame.frame);
	std::vector<dlib::rectangle> faces;

	if(usingDNNFaceDetection) {
		//Using dlib's CNN-based face detector which can (optimistically) be pushed out to the GPU
		dlib::matrix<dlib::rgb_pixel> imageMatrix;
		dlib::assign_image(imageMatrix, dlibClassificationFrame);
		std::vector<dlib::mmod_rect> detections = faceDetectionModel(imageMatrix);
		for(dlib::mmod_rect detection : detections) {
			faces.push_back(detection.rect);
		}
	} else {
		//Using dlib's built-in HOG face detector instead of a CNN-based detector
		faces = frontalFaceDetector(dlibClassificationFrame);
	}

	int bestFace = -1;
	int bestFaceArea = -1;
	Rect2d tempBox, tempBoxNormalSize, bestFaceBox, bestFaceBoxNormalSize;
	int i = -1;
	for(dlib::rectangle face : faces) {
		i++;
		tempBox.x = face.left();
		tempBox.y = face.top();
		tempBox.width = face.right() - tempBox.x;
		tempBox.height = face.bottom() - tempBox.y;
		tempBoxNormalSize = Utilities::scaleRect(tempBox, 1.0 / classificationFrame.scaleFactor);
		if((int)face.area() > bestFaceArea) {
			bestFace = i;
			bestFaceArea = face.area();
			bestFaceBox = tempBox;
			bestFaceBoxNormalSize = tempBoxNormalSize;
		}
	}
	YerFace_MutexLock(myClassificationMutex);
	newestClassificationBox.run = true;
	newestClassificationBox.set = false;
	if(bestFace >= 0) {
		newestClassificationBox.box = bestFaceBox;
		newestClassificationBox.boxNormalSize = bestFaceBoxNormalSize;
		newestClassificationBox.set = true;
	}
	YerFace_MutexUnlock(myClassificationMutex);
}

void FaceTracker::assignFaceRect(void) {
	YerFace_MutexLock(myClassificationMutex);
	working.faceRect.set = false;
	if(newestClassificationBox.set) {
		working.faceRect.rect = newestClassificationBox.boxNormalSize;
		working.faceRect.set = true;
	} else {
		logger->warn("Lost face completely! Will keep searching...");
	}
	YerFace_MutexUnlock(myClassificationMutex);
}

void FaceTracker::doIdentifyFeatures(ClassificationFrame classificationFrame) {
	working.facialFeatures.set = false;
	if(!working.faceRect.set) {
		return;
	}
	dlib::cv_image<dlib::bgr_pixel> dlibClassificationFrame = cv_image<bgr_pixel>(classificationFrame.frame);
	dlib::rectangle dlibClassificationBox = dlib::rectangle(
		working.faceRect.rect.x * classificationFrame.scaleFactor,
		working.faceRect.rect.y * classificationFrame.scaleFactor,
		(working.faceRect.rect.width + working.faceRect.rect.x) * classificationFrame.scaleFactor,
		(working.faceRect.rect.height + working.faceRect.rect.y) * classificationFrame.scaleFactor);

	full_object_detection result = shapePredictor(dlibClassificationFrame, dlibClassificationBox);

	working.facialFeatures.featuresExposed.features.clear();
	working.facialFeatures.featuresExposed.features.resize(result.num_parts());

	working.facialFeatures.features.clear();
	working.facialFeatures.features3D.clear();
	dlib::point part;
	Point2d partPoint;
	for(unsigned long featureIndex = 0; featureIndex < result.num_parts(); featureIndex++) {
		part = result.part(featureIndex);
		if(!doConvertLandmarkPointToImagePoint(&part, &partPoint, classificationFrame.scaleFactor)) {
			return;
		}

		working.facialFeatures.featuresExposed.features[featureIndex] = partPoint;

		bool pushCorrelationPoint = false;
		switch(featureIndex) {
			case IDX_NOSE_SELLION:
				working.facialFeatures.features3D.push_back(vertexNoseSellion);
				pushCorrelationPoint = true;
				break;
			case IDX_RIGHTEYE_OUTER_CORNER:
				working.facialFeatures.features3D.push_back(vertexEyeRightOuterCorner);
				pushCorrelationPoint = true;
				break;
			case IDX_LEFTEYE_OUTER_CORNER:
				working.facialFeatures.features3D.push_back(vertexEyeLeftOuterCorner);
				pushCorrelationPoint = true;
				break;
			case IDX_JAWLINE_0:
				working.facialFeatures.features3D.push_back(vertexRightEar);
				pushCorrelationPoint = true;
				break;
			case IDX_JAWLINE_16:
				working.facialFeatures.features3D.push_back(vertexLeftEar);
				pushCorrelationPoint = true;
				break;
			case IDX_NOSE_TIP:
				working.facialFeatures.features3D.push_back(vertexNoseTip);
				pushCorrelationPoint = true;
				break;
			case IDX_JAWLINE_8:
				working.facialFeatures.features3D.push_back(vertexMenton);
				pushCorrelationPoint = true;
				break;
		}
		if(pushCorrelationPoint) {
			working.facialFeatures.features.push_back(partPoint);
		}
	}

	//Stommion needs a little extra help.
	part = result.part(IDX_MOUTHIN_CENTER_TOP);
	Point2d mouthTop;
	if(!doConvertLandmarkPointToImagePoint(&part, &mouthTop, classificationFrame.scaleFactor)) {
		return;
	}
	part = result.part(IDX_MOUTHIN_CENTER_BOTTOM);
	Point2d mouthBottom;
	if(!doConvertLandmarkPointToImagePoint(&part, &mouthBottom, classificationFrame.scaleFactor)) {
		return;
	}
	partPoint = (mouthTop + mouthTop + mouthBottom) / 3.0;
	working.facialFeatures.features.push_back(partPoint);
	working.facialFeatures.features3D.push_back(vertexStommion);
	working.facialFeatures.set = true;
	working.facialFeatures.featuresExposed.set = true;
}

void FaceTracker::doInitializeCameraModel(void) {
	//Totally fake, idealized camera.
	Size frameSize = frameDerivatives->getWorkingFrameSize();
	double focalLength = frameSize.width;
	Point2d center = Point2d(frameSize.width / 2, frameSize.height / 2);
	facialCameraModel.cameraMatrix = Utilities::generateFakeCameraMatrix(focalLength, center);
	facialCameraModel.distortionCoefficients = Mat::zeros(4, 1, DataType<double>::type);
	facialCameraModel.set = true;
}

void FaceTracker::doCalculateFacialTransformation(void) {
	if(!working.facialFeatures.set) {
		working.previouslyReportedFacialPose.set = false;
		return;
	}
	if(!facialCameraModel.set) {
		doInitializeCameraModel();
	}

	FrameTimestamps frameTimestamps = frameDerivatives->getWorkingFrameTimestamps();
	double frameTimestamp = frameTimestamps.startTimestamp;
	FacialPose tempPose;
	tempPose.timestamp = frameTimestamp;
	tempPose.set = false;
	Mat tempRotationVector;

	//// DO FACIAL POSE SOLUTION ////

	solvePnP(working.facialFeatures.features3D, working.facialFeatures.features, facialCameraModel.cameraMatrix, facialCameraModel.distortionCoefficients, tempRotationVector, tempPose.translationVector);
	tempRotationVector.at<double>(0) = tempRotationVector.at<double>(0) * -1.0;
	tempRotationVector.at<double>(1) = tempRotationVector.at<double>(1) * -1.0;
	Rodrigues(tempRotationVector, tempPose.rotationMatrix);

	//// REJECT BAD / OUT OF BOUNDS FACIAL POSES ////

	bool reportNewPose = true;
	double degreesDifference, distance, scaledRotationThreshold, scaledTranslationThreshold;
	double timeScale = (double)(frameTimestamps.estimatedEndTimestamp - frameTimestamps.startTimestamp) / (double)(1.0 / 30.0);
	if(working.previouslyReportedFacialPose.set) {
		scaledRotationThreshold = poseRotationHighRejectionThreshold * timeScale;
		scaledTranslationThreshold = poseTranslationHighRejectionThreshold * timeScale;
		degreesDifference = Utilities::degreesDifferenceBetweenTwoRotationMatrices(working.previouslyReportedFacialPose.rotationMatrix, tempPose.rotationMatrix);
		distance = Utilities::lineDistance(Point3d(tempPose.translationVector), Point3d(working.previouslyReportedFacialPose.translationVector));
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
		if(working.previouslyReportedFacialPose.set) {
			if(tempPose.timestamp - working.previouslyReportedFacialPose.timestamp >= poseRejectionResetAfterSeconds) {
				logger->warn("Facial pose has come back bad consistantly for %.02lf seconds! Unsetting the face pose completely.", tempPose.timestamp - working.previouslyReportedFacialPose.timestamp);
				working.previouslyReportedFacialPose.set = false;
			}
			working.facialPose = working.previouslyReportedFacialPose;
		} else {
			working.facialPose.set = false;
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
	if(working.previouslyReportedFacialPose.set) {
		int i;
		double delta;

		// Do the de-noising thing first for the externally-facing matrices
		Vec3d prevAngles = Utilities::rotationMatrixToEulerAngles(working.previouslyReportedFacialPose.rotationMatrix);
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
			delta = tempPose.translationVector.at<double>(i) - working.previouslyReportedFacialPose.translationVector.at<double>(i);
			if(fabs(delta) <= scaledTranslationThreshold) {
				tempPose.translationVector.at<double>(i) = working.previouslyReportedFacialPose.translationVector.at<double>(i);
			}
		}

		// Do the de-noising thing again, but for the internally-facing matrices
		prevAngles = Utilities::rotationMatrixToEulerAngles(working.previouslyReportedFacialPose.rotationMatrixInternal);
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
			delta = tempPose.translationVectorInternal.at<double>(i) - working.previouslyReportedFacialPose.translationVectorInternal.at<double>(i);
			if(fabs(delta) <= scaledTranslationThreshold) {
				tempPose.translationVectorInternal.at<double>(i) = working.previouslyReportedFacialPose.translationVectorInternal.at<double>(i);
			}
		}
	}

	working.facialPose = tempPose;
	working.previouslyReportedFacialPose = working.facialPose;
}

void FaceTracker::doPrecalculateFacialPlaneNormal(void) {
	if(!working.facialPose.set) {
		return;
	}
	Mat planeNormalMat = (Mat_<double>(3, 1) << 0.0, 0.0, -1.0);
	planeNormalMat = working.facialPose.rotationMatrix * planeNormalMat;
	working.facialPose.facialPlaneNormal = Vec3d(planeNormalMat.at<double>(0), planeNormalMat.at<double>(1), planeNormalMat.at<double>(2));
}

FacialPlane FaceTracker::getCalculatedFacialPlaneForWorkingFacialPose(MarkerType markerType) {
	if(!working.facialPose.set) {
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
	translationOffset = working.facialPose.rotationMatrix * translationOffset;

	FacialPlane facialPlane;
	Mat translationVector = working.facialPose.translationVector + translationOffset;
	facialPlane.planePoint = Point3d(translationVector.at<double>(0), translationVector.at<double>(1), translationVector.at<double>(2));
	facialPlane.planeNormal = working.facialPose.facialPlaneNormal;

	return facialPlane;
}

bool FaceTracker::doConvertLandmarkPointToImagePoint(dlib::point *src, Point2d *dst, double classificationScaleFactor) {
	if(*src == OBJECT_PART_NOT_PRESENT) {
		return false;
	}
	*dst = Point2d(src->x(), src->y());
	dst->x /= classificationScaleFactor;
	dst->y /= classificationScaleFactor;
	return true;
}

void FaceTracker::renderPreviewHUD(void) {
	YerFace_MutexLock(myCmpMutex);
	Mat frame = frameDerivatives->getCompletedPreviewFrame();
	int density = sdlDriver->getPreviewDebugDensity();
	if(density > 0) {
		if(complete.facialPose.set) {
			std::vector<Point3d> gizmo3d(6);
			std::vector<Point2d> gizmo2d;
			gizmo3d[0] = Point3d(-50,0.0,0.0);
			gizmo3d[1] = Point3d(50,0.0,0.0);
			gizmo3d[2] = Point3d(0.0,50,0.0);
			gizmo3d[3] = Point3d(0.0,-50,0.0);
			gizmo3d[4] = Point3d(0.0,0.0,50);
			gizmo3d[5] = Point3d(0.0,0.0,-50);
			
			Mat tempRotationVector;
			Rodrigues(complete.facialPose.rotationMatrix, tempRotationVector);
			projectPoints(gizmo3d, tempRotationVector, complete.facialPose.translationVector, facialCameraModel.cameraMatrix, facialCameraModel.distortionCoefficients, gizmo2d);
			arrowedLine(frame, gizmo2d[0], gizmo2d[1], Scalar(0, 0, 255), 2);
			arrowedLine(frame, gizmo2d[2], gizmo2d[3], Scalar(255, 0, 0), 2);
			arrowedLine(frame, gizmo2d[4], gizmo2d[5], Scalar(0, 255, 0), 2);
		}
	}
	if(density > 1) {
		if(complete.faceRect.set) {
			cv::rectangle(frame, complete.faceRect.rect, Scalar(255, 255, 0), 1);
		}
	}
	if(density > 3) {
		if(complete.facialFeatures.set) {
			for(auto feature : complete.facialFeatures.featuresExposed.features) {
				Utilities::drawX(frame, feature, Scalar(147, 20, 255));
			}
		}
	}

	if(density > 4) {
		if(complete.facialPose.set) {
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

			projectPoints(edges3d, complete.facialPose.rotationMatrix, complete.facialPose.translationVector, facialCameraModel.cameraMatrix, facialCameraModel.distortionCoefficients, edges2d);

			for(unsigned int i = 0; i + 1 < edges2d.size(); i = i + 2) {
				cv::line(frame, edges2d[i], edges2d[i + 1], Scalar(255, 255, 255));
			}
		}
	}
	YerFace_MutexUnlock(myCmpMutex);
}

FacialRect FaceTracker::getFacialBoundingBox(void) {
	FacialRect val = working.faceRect;
	return val;
}

FacialFeatures FaceTracker::getFacialFeatures(void) {
	FacialFeatures val = working.facialFeatures.featuresExposed;
	return val;
}

FacialCameraModel FaceTracker::getFacialCameraModel(void) {
	FacialCameraModel val = facialCameraModel;
	return val;
}

FacialPose FaceTracker::getWorkingFacialPose(void) {
	FacialPose val = working.facialPose;
	return val;
}

FacialPose FaceTracker::getCompletedFacialPose(void) {
	YerFace_MutexLock(myCmpMutex);
	FacialPose val = complete.facialPose;
	YerFace_MutexUnlock(myCmpMutex);
	return val;
}

int FaceTracker::runClassificationLoop(void *ptr) {
	FaceTracker *self = (FaceTracker *)ptr;
	self->logger->verbose("Face Tracker Classification Thread alive!");

	signed long lastClassificationFrameNumber = -1;

	while(true) {
		ClassificationFrame classificationFrame = self->frameDerivatives->getClassificationFrame();

		if(classificationFrame.set && classificationFrame.timestamps.set &&
		  classificationFrame.timestamps.frameNumber != lastClassificationFrameNumber) {
			self->doClassifyFace(classificationFrame);
			lastClassificationFrameNumber = classificationFrame.timestamps.frameNumber;
		}

		YerFace_MutexLock(self->myClassificationMutex);
		if(!self->classifierRunning) {
			YerFace_MutexUnlock(self->myClassificationMutex);
			break;
		}
		YerFace_MutexUnlock(self->myClassificationMutex);
		SDL_Delay(1);
	}

	self->logger->verbose("Face Tracker Classification Thread quitting...");
	return 0;
}

}; //namespace YerFace
