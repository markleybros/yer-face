
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

FaceTracker::FaceTracker(string myFeatureDetectionModelFileName, string myFaceDetectionModelFileName, SDLDriver *mySDLDriver, FrameDerivatives *myFrameDerivatives, bool myPerformOpticalTracking, float myTrackingBoxPercentage, float myMaxTrackerDriftPercentage, double myPoseSmoothingOverSeconds, double myPoseSmoothingExponent, double myPoseSmoothingRotationLowRejectionThreshold, double myPoseSmoothingTranslationLowRejectionThreshold, double myPoseSmoothingRotationHighRejectionThreshold, double myPoseSmoothingTranslationHighRejectionThreshold) {
	featureDetectionModelFileName = myFeatureDetectionModelFileName;
	faceDetectionModelFileName = myFaceDetectionModelFileName;
	trackerState = DETECTING;
	working.classificationBox.set = false;
	working.trackingBox.set = false;
	working.faceRect.set = false;
	working.facialFeatures.set = false;
	working.facialFeatures.featuresExposed.set = false;
	working.facialPose.set = false;
	working.previouslyReportedFacialPose.set = false;
	complete.classificationBox.set = false;
	complete.trackingBox.set = false;
	complete.faceRect.set = false;
	complete.facialFeatures.set = false;
	complete.facialPose.set = false;
	facialCameraModel.set = false;

	sdlDriver = mySDLDriver;
	if(sdlDriver == NULL) {
		throw invalid_argument("sdlDriver cannot be NULL");
	}
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	performOpticalTracking = myPerformOpticalTracking;
	trackingBoxPercentage = myTrackingBoxPercentage;
	if(trackingBoxPercentage <= 0.0) {
		throw invalid_argument("trackingBoxPercentage cannot be less than or equal to zero");
	}
	maxTrackerDriftPercentage = myMaxTrackerDriftPercentage;
	if(maxTrackerDriftPercentage <= 0.0) {
		throw invalid_argument("maxTrackerDriftPercentage cannot be less than or equal to zero");
	}
	poseSmoothingOverSeconds = myPoseSmoothingOverSeconds;
	if(poseSmoothingOverSeconds <= 0.0) {
		throw invalid_argument("poseSmoothingOverSeconds cannot be less than or equal to zero.");
	}
	poseSmoothingExponent = myPoseSmoothingExponent;
	if(poseSmoothingExponent <= 0.0) {
		throw invalid_argument("poseSmoothingExponent cannot be less than or equal to zero.");
	}
	poseSmoothingRotationLowRejectionThreshold = myPoseSmoothingRotationLowRejectionThreshold;
	if(poseSmoothingRotationLowRejectionThreshold <= 0.0) {
		throw invalid_argument("poseSmoothingRotationLowRejectionThreshold cannot be less than or equal to zero.");
	}
	poseSmoothingTranslationLowRejectionThreshold = myPoseSmoothingTranslationLowRejectionThreshold;
	if(poseSmoothingTranslationLowRejectionThreshold <= 0.0) {
		throw invalid_argument("poseSmoothingRotationLowRejectionThreshold cannot be less than or equal to zero.");
	}
	poseSmoothingRotationHighRejectionThreshold = myPoseSmoothingRotationHighRejectionThreshold;
	if(poseSmoothingRotationHighRejectionThreshold <= 0.0) {
		throw invalid_argument("poseSmoothingRotationHighRejectionThreshold cannot be less than or equal to zero.");
	}
	poseSmoothingTranslationHighRejectionThreshold = myPoseSmoothingTranslationHighRejectionThreshold;
	if(poseSmoothingTranslationHighRejectionThreshold <= 0.0) {
		throw invalid_argument("poseSmoothingRotationHighRejectionThreshold cannot be less than or equal to zero.");
	}

	logger = new Logger("FaceTracker");
	metrics = new Metrics("FaceTracker", frameDerivatives);

	if((myWrkMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((myCmpMutex = SDL_CreateMutex()) == NULL) {
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
	logger->debug("FaceTracker object constructed and ready to go! Using Face Detection Method: %s", usingDNNFaceDetection ? "DNN" : "HOG");
}

FaceTracker::~FaceTracker() {
	logger->debug("FaceTracker object destructing...");
	SDL_DestroyMutex(myWrkMutex);
	SDL_DestroyMutex(myCmpMutex);
	delete metrics;
	delete logger;
}

// Pose recovery approach largely informed by the following sources:
//  - https://www.learnopencv.com/head-pose-estimation-using-opencv-and-dlib/
//  - https://github.com/severin-lemaignan/gazr/
TrackerState FaceTracker::processCurrentFrame(void) {
	metrics->startClock();

	YerFace_MutexLock(myWrkMutex);

	classificationScaleFactor = frameDerivatives->getClassificationScaleFactor();
	dlibClassificationFrame = cv_image<bgr_pixel>(frameDerivatives->getClassificationFrame());

	if(performOpticalTracking) {
		performTracking();
	}

	doClassifyFace();

	if(performOpticalTracking) {
		if(working.classificationBox.set) {
			if(!working.trackingBox.set || trackerDriftingExcessively()) {
				performInitializationOfTracker();
			}
		}
	}

	assignFaceRect();

	doIdentifyFeatures();

	doCalculateFacialTransformation();

	doCalculateFacialPlane();

	TrackerState status = trackerState;

	YerFace_MutexUnlock(myWrkMutex);

	metrics->endClock();

	return status;
}

void FaceTracker::advanceWorkingToCompleted(void) {
	YerFace_MutexLock(myWrkMutex);
	YerFace_MutexLock(myCmpMutex);
	complete = working;
	YerFace_MutexUnlock(myCmpMutex);
	working.classificationBox.set = false;
	working.trackingBox.set = false;
	working.faceRect.set = false;
	working.facialFeatures.set = false;
	working.facialFeatures.featuresExposed.set = false;
	working.facialPose.set = false;
	YerFace_MutexUnlock(myWrkMutex);
}

void FaceTracker::performInitializationOfTracker(void) {
	if(!working.classificationBox.set) {
		throw invalid_argument("FaceTracker::performInitializationOfTracker() called while markerDetectedSet is false");
	}
	trackerState = TRACKING;
	tracker = TrackerKCF::create();
	working.trackingBox.rect = Rect(Utilities::insetBox(working.classificationBox.boxNormalSize, trackingBoxPercentage));
	working.trackingBox.set = true;

	tracker->init(frameDerivatives->getWorkingFrame(), working.trackingBox.rect);
}

bool FaceTracker::performTracking(void) {
	if(trackerState == TRACKING) {
		bool trackSuccess = tracker->update(frameDerivatives->getWorkingFrame(), working.trackingBox.rect);
		if(!trackSuccess) {
			working.trackingBox.set = false;
			return false;
		}
		working.trackingBox.set = true;
		return true;
	}
	return false;
}

bool FaceTracker::trackerDriftingExcessively(void) {
	if(!working.classificationBox.set || !working.trackingBox.set) {
		throw invalid_argument("FaceTracker::trackerDriftingExcessively() called while one or both of working.classificationBox.set or working.trackingBox.set are false");
	}
	double actualDistance = Utilities::lineDistance(Utilities::centerRect(working.classificationBox.boxNormalSize), Utilities::centerRect(working.trackingBox.rect));
	double maxDistance = std::sqrt(working.classificationBox.boxNormalSize.area()) * maxTrackerDriftPercentage;
	if(actualDistance > maxDistance) {
		logger->warn("Optical tracker drifting excessively! Resetting it.");
		return true;
	}
	return false;
}

void FaceTracker::doClassifyFace(void) {
	working.classificationBox.set = false;
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
		tempBoxNormalSize = Utilities::scaleRect(tempBox, 1.0 / classificationScaleFactor);
		if(working.trackingBox.set) {
			if((tempBoxNormalSize & working.trackingBox.rect).area() <= 0) {
				continue;
			}
		}
		if((int)face.area() > bestFaceArea) {
			bestFace = i;
			bestFaceArea = face.area();
			bestFaceBox = tempBox;
			bestFaceBoxNormalSize = tempBoxNormalSize;
		}
	}
	if(bestFace >= 0) {
		trackerState = TRACKING;
		working.classificationBox.box = bestFaceBox;
		working.classificationBox.boxNormalSize = bestFaceBoxNormalSize;
		working.classificationBox.set = true;
	}
}

void FaceTracker::assignFaceRect(void) {
	working.faceRect.set = false;
	Rect2d trackingBoxNormalSize;
	if(working.trackingBox.set) {
		trackingBoxNormalSize = Utilities::insetBox(working.trackingBox.rect, 1.0 / trackingBoxPercentage);
	}
	if(working.classificationBox.set && working.trackingBox.set) {
		working.faceRect.rect.x = (working.classificationBox.boxNormalSize.x + trackingBoxNormalSize.x) / 2.0;
		working.faceRect.rect.y = (working.classificationBox.boxNormalSize.y + trackingBoxNormalSize.y) / 2.0;
		working.faceRect.rect.width = (working.classificationBox.boxNormalSize.width + trackingBoxNormalSize.width) / 2.0;
		working.faceRect.rect.height = (working.classificationBox.boxNormalSize.height + trackingBoxNormalSize.height) / 2.0;
		working.faceRect.set = true;
	} else if(working.classificationBox.set) {
		working.faceRect.rect = working.classificationBox.boxNormalSize;
		working.faceRect.set = true;
	} else if(working.trackingBox.set) {
		working.faceRect.rect = trackingBoxNormalSize;
		working.faceRect.set = true;
	} else {
		if(trackerState == TRACKING) {
			trackerState = LOST;
			logger->warn("Lost face completely! Will keep searching...");
		}
	}
}

void FaceTracker::doIdentifyFeatures(void) {
	working.facialFeatures.set = false;
	if(!working.faceRect.set) {
		return;
	}
	dlib::rectangle dlibClassificationBox = dlib::rectangle(
		working.faceRect.rect.x * classificationScaleFactor,
		working.faceRect.rect.y * classificationScaleFactor,
		(working.faceRect.rect.width + working.faceRect.rect.x) * classificationScaleFactor,
		(working.faceRect.rect.height + working.faceRect.rect.y) * classificationScaleFactor);

	full_object_detection result = shapePredictor(dlibClassificationFrame, dlibClassificationBox);

	working.facialFeatures.features.clear();
	working.facialFeatures.features3D.clear();
	dlib::point part;
	Point2d partPoint;
	std::vector<int> featureIndexes = {IDX_NOSE_SELLION, IDX_EYE_RIGHT_OUTER_CORNER, IDX_EYE_LEFT_OUTER_CORNER, IDX_EYE_RIGHT_INNER_CORNER, IDX_EYE_LEFT_INNER_CORNER, IDX_JAW_RIGHT_TOP, IDX_JAW_LEFT_TOP, IDX_NOSE_TIP, IDX_MENTON};
	for(int featureIndex : featureIndexes) {
		part = result.part(featureIndex);
		if(!doConvertLandmarkPointToImagePoint(&part, &partPoint)) {
			trackerState = LOST;
			return;
		}

		bool pushCorrelationPoint = true;
		switch(featureIndex) {
			default:
				throw logic_error("bad facial feature index");
			case IDX_NOSE_SELLION:
				working.facialFeatures.features3D.push_back(VERTEX_NOSE_SELLION);
				working.facialFeatures.featuresExposed.noseSellion = partPoint;
				break;
			case IDX_EYE_RIGHT_OUTER_CORNER:
				working.facialFeatures.features3D.push_back(VERTEX_EYE_RIGHT_OUTER_CORNER);
				working.facialFeatures.featuresExposed.eyeRightOuterCorner = partPoint;
				break;
			case IDX_EYE_LEFT_OUTER_CORNER:
				working.facialFeatures.features3D.push_back(VERTEX_EYE_LEFT_OUTER_CORNER);
				working.facialFeatures.featuresExposed.eyeLeftOuterCorner = partPoint;
				break;
			case IDX_EYE_RIGHT_INNER_CORNER:
				working.facialFeatures.featuresExposed.eyeRightInnerCorner = partPoint;
				pushCorrelationPoint = false;
				break;
			case IDX_EYE_LEFT_INNER_CORNER:
				working.facialFeatures.featuresExposed.eyeLeftInnerCorner = partPoint;
				pushCorrelationPoint = false;
				break;
			case IDX_JAW_RIGHT_TOP:
				working.facialFeatures.features3D.push_back(VERTEX_RIGHT_EAR);
				working.facialFeatures.featuresExposed.jawRightTop = partPoint;
				break;
			case IDX_JAW_LEFT_TOP:
				working.facialFeatures.features3D.push_back(VERTEX_LEFT_EAR);
				working.facialFeatures.featuresExposed.jawLeftTop = partPoint;
				break;
			case IDX_NOSE_TIP:
				working.facialFeatures.features3D.push_back(VERTEX_NOSE_TIP);
				working.facialFeatures.featuresExposed.noseTip = partPoint;
				break;
			case IDX_MENTON:
				working.facialFeatures.features3D.push_back(VERTEX_MENTON);
				working.facialFeatures.featuresExposed.menton = partPoint;
				break;
		}
		if(pushCorrelationPoint) {
			working.facialFeatures.features.push_back(partPoint);
		}
	}

	//Stommion needs a little extra help.
	part = result.part(IDX_MOUTH_CENTER_INNER_TOP);
	Point2d mouthTop;
	if(!doConvertLandmarkPointToImagePoint(&part, &mouthTop)) {
		trackerState = LOST;
		return;
	}
	part = result.part(IDX_MOUTH_CENTER_INNER_BOTTOM);
	Point2d mouthBottom;
	if(!doConvertLandmarkPointToImagePoint(&part, &mouthBottom)) {
		trackerState = LOST;
		return;
	}
	partPoint = (mouthTop + mouthBottom) * 0.5;
	working.facialFeatures.features.push_back(partPoint);
	working.facialFeatures.featuresExposed.stommion = partPoint;
	working.facialFeatures.features3D.push_back(VERTEX_STOMMION);
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
	Rodrigues(tempRotationVector, tempPose.rotationMatrix);

	Mat translationOffset = (Mat_<double>(3,1) << 0.0, 0.0, -30.0); //An offset to bring the planar origin closer to alignment with the majority of the markers.
	translationOffset = tempPose.rotationMatrix * translationOffset;
	tempPose.translationVector = tempPose.translationVector + translationOffset;

	//// REJECT BAD / OUT OF BOUNDS FACIAL POSES ////
	bool reportNewPose = true;
	double degreesDifference, distance;
	if(working.previouslyReportedFacialPose.set) {
		degreesDifference = Utilities::degreesDifferenceBetweenTwoRotationMatrices(working.previouslyReportedFacialPose.rotationMatrix, tempPose.rotationMatrix);
		distance = Utilities::lineDistance(Point3d(tempPose.translationVector), Point3d(working.previouslyReportedFacialPose.translationVector));
		if(degreesDifference > poseSmoothingRotationHighRejectionThreshold || distance > poseSmoothingTranslationHighRejectionThreshold) {
			logger->warn("Dropping facial pose due to high rotation (%.02lf) or high motion (%.02lf)!", degreesDifference, distance);
			reportNewPose = false;
		}
	}
	if(!reportNewPose) {
		if(working.previouslyReportedFacialPose.set) {
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
	// Vec3d angles = Utilities::rotationMatrixToEulerAngles(tempPose.rotationMatrix);
	// logger->verbose("Facial Pose Angle: <%.02f, %.02f, %.02f>; Translation: <%.02f, %.02f, %.02f>", angles[0], angles[1], angles[2], tempPose.translationVector.at<double>(0), tempPose.translationVector.at<double>(1), tempPose.translationVector.at<double>(2));

	//// REJECT NOISY SOLUTIONS ////

	reportNewPose = true;
	if(working.previouslyReportedFacialPose.set) {
		degreesDifference = Utilities::degreesDifferenceBetweenTwoRotationMatrices(working.previouslyReportedFacialPose.rotationMatrix, tempPose.rotationMatrix);
		distance = Utilities::lineDistance(Point3d(tempPose.translationVector), Point3d(working.previouslyReportedFacialPose.translationVector));
		if(degreesDifference < poseSmoothingRotationLowRejectionThreshold && distance < poseSmoothingTranslationLowRejectionThreshold) {
			// logger->verbose("Dropping facial pose due to low rotation (%.02lf) and low motion (%.02lf)!", degreesDifference, distance);
			reportNewPose = false;
		}
	}

	if(reportNewPose) {
		working.facialPose = tempPose;
		working.previouslyReportedFacialPose = working.facialPose;
	} else {
		working.facialPose = working.previouslyReportedFacialPose;
	}
}

void FaceTracker::doCalculateFacialPlane(void) {
	if(!working.facialPose.set) {
		return;
	}
	working.facialPose.planePoint = Point3d(working.facialPose.translationVector.at<double>(0), working.facialPose.translationVector.at<double>(1), working.facialPose.translationVector.at<double>(2));
	Mat planeNormalMat = (Mat_<double>(3, 1) << 0.0, 0.0, -1.0);
	planeNormalMat = working.facialPose.rotationMatrix * planeNormalMat;
	working.facialPose.planeNormal = Vec3d(planeNormalMat.at<double>(0), planeNormalMat.at<double>(1), planeNormalMat.at<double>(2));
}

bool FaceTracker::doConvertLandmarkPointToImagePoint(dlib::point *src, Point2d *dst) {
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
			gizmo3d[2] = Point3d(0.0,-50,0.0);
			gizmo3d[3] = Point3d(0.0,50,0.0);
			gizmo3d[4] = Point3d(0.0,0.0,-50);
			gizmo3d[5] = Point3d(0.0,0.0,50);
			
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
	if(density > 2) {
		if(complete.classificationBox.set) {
			cv::rectangle(frame, complete.classificationBox.boxNormalSize, Scalar(0, 255, 0), 1);
		}
		if(complete.trackingBox.set) {
			cv::rectangle(frame, complete.trackingBox.rect, Scalar(255, 0, 0), 1);
		}
	}
	if(density > 3) {
		if(complete.facialFeatures.set) {
			for(auto feature : complete.facialFeatures.features) {
				Utilities::drawX(frame, feature, Scalar(147, 20, 255));
			}
		}
	}
	YerFace_MutexUnlock(myCmpMutex);
}

TrackerState FaceTracker::getTrackerState(void) {
	YerFace_MutexLock(myWrkMutex);
	TrackerState val = trackerState;
	YerFace_MutexUnlock(myWrkMutex);
	return val;
}

FacialRect FaceTracker::getFacialBoundingBox(void) {
	YerFace_MutexLock(myWrkMutex);
	FacialRect val = working.faceRect;
	YerFace_MutexUnlock(myWrkMutex);
	return val;
}

FacialFeatures FaceTracker::getFacialFeatures(void) {
	YerFace_MutexLock(myWrkMutex);
	FacialFeatures val = working.facialFeatures.featuresExposed;
	YerFace_MutexUnlock(myWrkMutex);
	return val;
}

FacialCameraModel FaceTracker::getFacialCameraModel(void) {
	YerFace_MutexLock(myWrkMutex);
	FacialCameraModel val = facialCameraModel;
	YerFace_MutexUnlock(myWrkMutex);
	return val;
}

FacialPose FaceTracker::getWorkingFacialPose(void) {
	YerFace_MutexLock(myWrkMutex);
	FacialPose val = working.facialPose;
	YerFace_MutexUnlock(myWrkMutex);
	return val;
}

FacialPose FaceTracker::getCompletedFacialPose(void) {
	YerFace_MutexLock(myCmpMutex);
	FacialPose val = complete.facialPose;
	YerFace_MutexUnlock(myCmpMutex);
	return val;
}

}; //namespace YerFace
