
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

FaceTracker::FaceTracker(string myModelFileName, FrameDerivatives *myFrameDerivatives, float myTrackingBoxPercentage, float myMaxTrackerDriftPercentage, int myPoseSmoothingBufferSize, float myPoseSmoothingExponent) {
	modelFileName = myModelFileName;
	trackerState = DETECTING;
	classificationBoxSet = false;
	trackingBoxSet = false;
	faceRectSet = false;
	facialFeaturesSet = false;
	facialCameraModel.set = false;
	facialPose.set = false;

	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	trackingBoxPercentage = myTrackingBoxPercentage;
	if(trackingBoxPercentage <= 0.0) {
		throw invalid_argument("trackingBoxPercentage cannot be less than or equal to zero");
	}
	maxTrackerDriftPercentage = myMaxTrackerDriftPercentage;
	if(maxTrackerDriftPercentage <= 0.0) {
		throw invalid_argument("maxTrackerDriftPercentage cannot be less than or equal to zero");
	}
	poseSmoothingBufferSize = myPoseSmoothingBufferSize;
	if(poseSmoothingBufferSize <= 0) {
		throw invalid_argument("poseSmoothingBufferSize cannot be less than or equal to zero.");
	}
	poseSmoothingExponent = myPoseSmoothingExponent;
	if(poseSmoothingExponent <= 0.0) {
		throw invalid_argument("poseSmoothingExponent cannot be less than or equal to zero.");
	}

	frontalFaceDetector = get_frontal_face_detector();
	deserialize(modelFileName.c_str()) >> shapePredictor;
	metrics = new Metrics();
	fprintf(stderr, "FaceTracker object constructed and ready to go!\n");
}

FaceTracker::~FaceTracker() {
	delete metrics;
	fprintf(stderr, "FaceTracker object destructing...\n");
}

// Pose recovery approach largely informed by the following sources:
//  - https://www.learnopencv.com/head-pose-estimation-using-opencv-and-dlib/
//  - https://github.com/severin-lemaignan/gazr/
TrackerState FaceTracker::processCurrentFrame(void) {
	metrics->startClock();
	classificationScaleFactor = frameDerivatives->getClassificationScaleFactor();

	performTracking();

	dlibClassificationFrame = cv_image<bgr_pixel>(frameDerivatives->getClassificationFrame());

	doClassifyFace();

	if(classificationBoxSet) {
		if(!trackingBoxSet || trackerDriftingExcessively()) {
			performInitializationOfTracker();
		}
	}

	assignFaceRect();

	doIdentifyFeatures();

	doCalculateFacialTransformation();

	doCalculateFacialPlane();

	metrics->endClock();
	fprintf(stderr, "FaceTracker %s\n", metrics->getTimesString());
	return trackerState;
}


void FaceTracker::performInitializationOfTracker(void) {
	if(!classificationBoxSet) {
		throw invalid_argument("FaceTracker::performInitializationOfTracker() called while markerDetectedSet is false");
	}
	trackerState = TRACKING;
	#if (CV_MINOR_VERSION < 3)
	tracker = Tracker::create("KCF");
	#else
	tracker = TrackerKCF::create();
	#endif
	trackingBox = Rect(Utilities::insetBox(classificationBoxNormalSize, trackingBoxPercentage));
	trackingBoxSet = true;

	tracker->init(frameDerivatives->getCurrentFrame(), trackingBox);
}

bool FaceTracker::performTracking(void) {
	if(trackerState == TRACKING) {
		bool trackSuccess = tracker->update(frameDerivatives->getCurrentFrame(), trackingBox);
		if(!trackSuccess) {
			trackingBoxSet = false;
			return false;
		}
		trackingBoxSet = true;
		return true;
	}
	return false;
}

bool FaceTracker::trackerDriftingExcessively(void) {
	if(!classificationBoxSet || !trackingBoxSet) {
		throw invalid_argument("FaceTracker::trackerDriftingExcessively() called while one or both of classificationBoxSet or trackingBoxSet are false");
	}
	double actualDistance = Utilities::lineDistance(Utilities::centerRect(classificationBoxNormalSize), Utilities::centerRect(trackingBox));
	double maxDistance = std::sqrt(classificationBoxNormalSize.area()) * maxTrackerDriftPercentage;
	if(actualDistance > maxDistance) {
		fprintf(stderr, "FaceTracker: WARNING: Optical tracker drifting excessively! Resetting it.\n");
		return true;
	}
	return false;
}

void FaceTracker::doClassifyFace(void) {
	classificationBoxSet = false;
	//Using dlib's built-in HOG face detector instead of a CNN-based detector because it trades off accuracy for speed.
	std::vector<dlib::rectangle> faces = frontalFaceDetector(dlibClassificationFrame);

	int bestFace = -1;
	int bestFaceArea = -1;
	Rect2d tempBox, tempBoxNormalSize, bestFaceBox, bestFaceBoxNormalSize;
	size_t facesCount = faces.size();
	for(size_t i = 0; i < facesCount; i++) {
		tempBox.x = faces[i].left();
		tempBox.y = faces[i].top();
		tempBox.width = faces[i].right() - tempBox.x;
		tempBox.height = faces[i].bottom() - tempBox.y;
		tempBoxNormalSize = Utilities::scaleRect(tempBox, 1.0 / classificationScaleFactor);
		if(trackingBoxSet) {
			if((tempBoxNormalSize & trackingBox).area() <= 0) {
				continue;
			}
		}
		if((int)faces[i].area() > bestFaceArea) {
			bestFace = i;
			bestFaceArea = faces[i].area();
			bestFaceBox = tempBox;
			bestFaceBoxNormalSize = tempBoxNormalSize;
		}
	}
	if(bestFace >= 0) {
		trackerState = TRACKING;
		classificationBox = bestFaceBox;
		classificationBoxNormalSize = bestFaceBoxNormalSize;
		classificationBoxSet = true;
	}
}

void FaceTracker::assignFaceRect(void) {
	faceRectSet = false;
	Rect2d trackingBoxNormalSize;
	if(trackingBoxSet) {
		trackingBoxNormalSize = Utilities::insetBox(trackingBox, 1.0 / trackingBoxPercentage);
	}
	if(classificationBoxSet && trackingBoxSet) {
		faceRect.x = (classificationBoxNormalSize.x + trackingBoxNormalSize.x) / 2.0;
		faceRect.y = (classificationBoxNormalSize.y + trackingBoxNormalSize.y) / 2.0;
		faceRect.width = (classificationBoxNormalSize.width + trackingBoxNormalSize.width) / 2.0;
		faceRect.height = (classificationBoxNormalSize.height + trackingBoxNormalSize.height) / 2.0;
		faceRectSet = true;
	} else if(classificationBoxSet) {
		faceRect = classificationBoxNormalSize;
		faceRectSet = true;
	} else if(trackingBoxSet) {
		faceRect = trackingBoxNormalSize;
		faceRectSet = true;
	} else {
		if(trackerState == TRACKING) {
			trackerState = LOST;
			fprintf(stderr, "FaceTracker: Lost face completely! Will keep searching...\n");
		}
	}
}

void FaceTracker::doIdentifyFeatures(void) {
	facialFeaturesSet = false;
	if(!faceRectSet) {
		return;
	}
	dlib::rectangle dlibClassificationBox = dlib::rectangle(
		faceRect.x * classificationScaleFactor,
		faceRect.y * classificationScaleFactor,
		(faceRect.width + faceRect.x) * classificationScaleFactor,
		(faceRect.height + faceRect.y) * classificationScaleFactor);

	full_object_detection result = shapePredictor(dlibClassificationFrame, dlibClassificationBox);

	Mat prevFrame = frameDerivatives->getPreviewFrame();

	facialFeatures.clear();
	facialFeatures3d.clear();
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
				facialFeatures3d.push_back(VERTEX_NOSE_SELLION);
				facialFeaturesExposed.noseSellion = partPoint;
				break;
			case IDX_EYE_RIGHT_OUTER_CORNER:
				facialFeatures3d.push_back(VERTEX_EYE_RIGHT_OUTER_CORNER);
				facialFeaturesExposed.eyeRightOuterCorner = partPoint;
				break;
			case IDX_EYE_LEFT_OUTER_CORNER:
				facialFeatures3d.push_back(VERTEX_EYE_LEFT_OUTER_CORNER);
				facialFeaturesExposed.eyeLeftOuterCorner = partPoint;
				break;
			case IDX_EYE_RIGHT_INNER_CORNER:
				facialFeaturesExposed.eyeRightInnerCorner = partPoint;
				pushCorrelationPoint = false;
				break;
			case IDX_EYE_LEFT_INNER_CORNER:
				facialFeaturesExposed.eyeLeftInnerCorner = partPoint;
				pushCorrelationPoint = false;
				break;
			case IDX_JAW_RIGHT_TOP:
				facialFeatures3d.push_back(VERTEX_RIGHT_EAR);
				facialFeaturesExposed.jawRightTop = partPoint;
				break;
			case IDX_JAW_LEFT_TOP:
				facialFeatures3d.push_back(VERTEX_LEFT_EAR);
				facialFeaturesExposed.jawLeftTop = partPoint;
				break;
			case IDX_NOSE_TIP:
				facialFeatures3d.push_back(VERTEX_NOSE_TIP);
				facialFeaturesExposed.noseTip = partPoint;
				break;
			case IDX_MENTON:
				facialFeatures3d.push_back(VERTEX_MENTON);
				facialFeaturesExposed.menton = partPoint;
				break;
		}
		if(pushCorrelationPoint) {
			facialFeatures.push_back(partPoint);
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
	facialFeatures.push_back(partPoint);
	facialFeaturesExposed.stommion = partPoint;
	facialFeatures3d.push_back(VERTEX_STOMMION);
	facialFeaturesSet = true;
}

void FaceTracker::doInitializeCameraModel(void) {
	//Totally fake, idealized camera.
	Size frameSize = frameDerivatives->getCurrentFrameSize();
	double focalLength = frameSize.width;
	Point2d center = Point2d(frameSize.width / 2, frameSize.height / 2);
	facialCameraModel.cameraMatrix = Utilities::generateFakeCameraMatrix(focalLength, center);
	facialCameraModel.distortionCoefficients = Mat::zeros(4, 1, DataType<double>::type);
	facialCameraModel.set = true;
}

void FaceTracker::doCalculateFacialTransformation(void) {
	if(!facialFeaturesSet) {
		return;
	}
	if(!facialCameraModel.set) {
		doInitializeCameraModel();
	}

	FacialPose tempPose;
	tempPose.set = false;
	Mat tempRotationVector;

	solvePnP(facialFeatures3d, facialFeatures, facialCameraModel.cameraMatrix, facialCameraModel.distortionCoefficients, tempRotationVector, tempPose.translationVector);
	Rodrigues(tempRotationVector, tempPose.rotationMatrix);

	Mat translationOffset = (Mat_<double>(3,1) << 0.0, 0.0, -30.0); //An offset to bring the planar origin closer to alignment with the majority of the markers.
	translationOffset = tempPose.rotationMatrix * translationOffset;
	tempPose.translationVector = tempPose.translationVector + translationOffset;

	facialPoseSmoothingBuffer.push_back(tempPose);
	while(facialPoseSmoothingBuffer.size() > (unsigned int)poseSmoothingBufferSize) {
		facialPoseSmoothingBuffer.pop_front();
	}

	tempPose.translationVector = (Mat_<double>(3,1) << 0.0, 0.0, 0.0);
	tempPose.rotationMatrix = (Mat_<double>(3,3) <<
			0.0, 0.0, 0.0,
			0.0, 0.0, 0.0,
			0.0, 0.0, 0.0);

	unsigned long numBufferEntries = facialPoseSmoothingBuffer.size();
	double combinedWeights = 0.0;
	int i = 0;
	for(FacialPose pose : facialPoseSmoothingBuffer) {
		double weight = std::pow((double)(i + 1) / (double)numBufferEntries, (double)poseSmoothingExponent) - combinedWeights;
		combinedWeights += weight;
		for(int j = 0; j < 3; j++) {
			tempPose.translationVector.at<double>(j) += pose.translationVector.at<double>(j) * weight;
		}
		for(int j = 0; j < 9; j++) {
			tempPose.rotationMatrix.at<double>(j) += pose.rotationMatrix.at<double>(j) * weight;
		}
		i++;
	}

	facialPose = tempPose;
	facialPose.set = true;

	// Vec3d angles = Utilities::rotationMatrixToEulerAngles(facialPose.rotationMatrix);
	// fprintf(stderr, "FaceTracker Facial Pose Angle: <%.02f, %.02f, %.02f>; Translation: <%.02f, %.02f, %.02f>\n", angles[0], angles[1], angles[2], facialPose.translationVector.at<double>(0), facialPose.translationVector.at<double>(1), facialPose.translationVector.at<double>(2));
}

void FaceTracker::doCalculateFacialPlane(void) {
	facialPose.planePoint = Point3d(facialPose.translationVector.at<double>(0), facialPose.translationVector.at<double>(1), facialPose.translationVector.at<double>(2));
	Mat planeNormalMat = (Mat_<double>(3, 1) << 0.0, 0.0, -1.0);
	planeNormalMat = facialPose.rotationMatrix * planeNormalMat;
	facialPose.planeNormal = Vec3d(planeNormalMat.at<double>(0), planeNormalMat.at<double>(1), planeNormalMat.at<double>(2));
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

void FaceTracker::renderPreviewHUD(bool verbose) {
	Mat frame = frameDerivatives->getPreviewFrame();
	if(verbose) {
		if(classificationBoxSet) {
			cv::rectangle(frame, classificationBoxNormalSize, Scalar(0, 255, 0), 1);
		}
		if(trackingBoxSet) {
			cv::rectangle(frame, trackingBox, Scalar(255, 0, 0), 1);
		}
		if(faceRectSet) {
			cv::rectangle(frame, faceRect, Scalar(255, 255, 0), 1);
		}
		if(facialFeaturesSet) {
			size_t featuresCount = facialFeatures.size();
			for(size_t i = 0; i < featuresCount; i++) {
				Utilities::drawX(frame, facialFeatures[i], Scalar(0, 255, 0));
			}
		}
	}
	if(facialPose.set) {
		std::vector<Point3d> gizmo3d(6);
		std::vector<Point2d> gizmo2d;
		gizmo3d[0] = Point3d(-50,0.0,0.0);
		gizmo3d[1] = Point3d(50,0.0,0.0);
		gizmo3d[2] = Point3d(0.0,-50,0.0);
		gizmo3d[3] = Point3d(0.0,50,0.0);
		gizmo3d[4] = Point3d(0.0,0.0,-50);
		gizmo3d[5] = Point3d(0.0,0.0,50);
		
		Mat tempRotationVector;
		Rodrigues(facialPose.rotationMatrix, tempRotationVector);
		projectPoints(gizmo3d, tempRotationVector, facialPose.translationVector, facialCameraModel.cameraMatrix, facialCameraModel.distortionCoefficients, gizmo2d);
		arrowedLine(frame, gizmo2d[0], gizmo2d[1], Scalar(0, 0, 255), 2);
		arrowedLine(frame, gizmo2d[2], gizmo2d[3], Scalar(255, 0, 0), 2);
		arrowedLine(frame, gizmo2d[4], gizmo2d[5], Scalar(0, 255, 0), 2);
	}
}

TrackerState FaceTracker::getTrackerState(void) {
	return trackerState;
}

FacialBoundingBox FaceTracker::getFacialBoundingBox(void) {
	FacialBoundingBox box;
	box.rect = faceRect;
	box.set = faceRectSet;
	return box;
}

FacialFeatures FaceTracker::getFacialFeatures(void) {
	facialFeaturesExposed.set = facialFeaturesSet;
	return facialFeaturesExposed;
}

FacialCameraModel FaceTracker::getFacialCameraModel(void) {
	return facialCameraModel;
}

FacialPose FaceTracker::getFacialPose(void) {
	return facialPose;
}

}; //namespace YerFace
