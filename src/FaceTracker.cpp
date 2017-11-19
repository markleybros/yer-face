
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

FaceTracker::FaceTracker(string myModelFileName, FrameDerivatives *myFrameDerivatives, float myTrackingBoxPercentage, float myMaxTrackerDriftPercentage) {
	modelFileName = myModelFileName;
	trackerState = DETECTING;
	classificationBoxSet = false;
	trackingBoxSet = false;
	facialFeaturesSet = false;
	facialFeatures3dSet = false;
	cameraModelSet = false;
	poseSet = false;

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

	frontalFaceDetector = get_frontal_face_detector();
	deserialize(modelFileName.c_str()) >> shapePredictor;
	fprintf(stderr, "FaceTracker object constructed and ready to go!\n");
}

FaceTracker::~FaceTracker() {
	fprintf(stderr, "FaceTracker object destructing...\n");
}

TrackerState FaceTracker::processCurrentFrame(void) {
	performTracking();

	dlibClassificationFrame = cv_image<bgr_pixel>(frameDerivatives->getClassificationFrame());

	doClassifyFace();

	if(classificationBoxSet) {
		if(!trackingBoxSet || trackerDriftingExcessively()) {
			performInitializationOfTracker();
		}
	}

	doIdentifyFeatures();

	doCalculateFacialTransformation();

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
	double classificationScaleFactor = frameDerivatives->getClassificationScaleFactor();
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
		// classificationBox.x = faces[bestFace].left();
		// classificationBox.y = faces[bestFace].top();
		// classificationBox.width = faces[bestFace].right() - classificationBox.x;
		// classificationBox.height = faces[bestFace].bottom() - classificationBox.y;
		// classificationBoxNormalSize = Utilities::scaleRect(classificationBox, 1.0 / classificationScaleFactor);
		classificationBoxDlib = faces[bestFace]; //FIXME <-- this is condemned
		classificationBoxSet = true;
	}
}

void FaceTracker::doIdentifyFeatures(void) {
	facialFeaturesSet = false;

	full_object_detection result = shapePredictor(dlibClassificationFrame, classificationBoxDlib);

	Mat prevFrame = frameDerivatives->getPreviewFrame();

	facialFeatures.clear();
	if(!facialFeatures3dSet) {
		facialFeatures3d.clear();
	}
	dlib::point part;
	std::vector<int> featureIndexes = {IDX_NOSE_SELLION, IDX_EYE_RIGHT_OUTER_CORNER, IDX_EYE_LEFT_OUTER_CORNER, IDX_JAW_RIGHT_TOP, IDX_JAW_LEFT_TOP, IDX_NOSE_TIP, IDX_MENTON};
	for(int featureIndex : featureIndexes) {
		part = result.part(featureIndex);
		Point2d partPoint;
		if(!doConvertLandmarkPointToImagePoint(&part, &partPoint)) {
			trackerState = LOST;
			return;
		}

		facialFeatures.push_back(partPoint);
		if(!facialFeatures3dSet) {
			switch(featureIndex) {
				default:
					throw logic_error("bad facial feature index");
				case IDX_NOSE_SELLION:
					facialFeatures3d.push_back(VERTEX_NOSE_SELLION);
					break;
				case IDX_EYE_RIGHT_OUTER_CORNER:
					facialFeatures3d.push_back(VERTEX_EYE_RIGHT_OUTER_CORNER);
					break;
				case IDX_EYE_LEFT_OUTER_CORNER:
					facialFeatures3d.push_back(VERTEX_EYE_LEFT_OUTER_CORNER);
					break;
				case IDX_JAW_RIGHT_TOP:
					facialFeatures3d.push_back(VERTEX_RIGHT_EAR);
					break;
				case IDX_JAW_LEFT_TOP:
					facialFeatures3d.push_back(VERTEX_LEFT_EAR);
					break;
				case IDX_NOSE_TIP:
					facialFeatures3d.push_back(VERTEX_NOSE_TIP);
					break;
				case IDX_MENTON:
					facialFeatures3d.push_back(VERTEX_MENTON);
					break;
			}
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
	facialFeatures.push_back((mouthTop + mouthBottom) * 0.5);
	if(!facialFeatures3dSet) {
		facialFeatures3d.push_back(VERTEX_STOMMION);
		facialFeatures3dSet = true;
	}
	facialFeaturesSet = true;
}

void FaceTracker::doInitializeCameraModel(void) {
	//Totally fake, idealized camera.
	Size frameSize = frameDerivatives->getCurrentFrameSize();
	double focalLength = frameSize.width;
	Point2d center = Point2d(frameSize.width / 2, frameSize.height / 2);
	cameraMatrix = (Mat_<double>(3, 3) <<
			focalLength, 0.0, center.x,
			0.0, focalLength, center.y,
			0.0, 0.0, 1.0);
	distortionCoefficients = Mat::zeros(4, 1, DataType<double>::type);
	cameraModelSet = true;
}

// Pose recovery approach largely informed by the following sources:
//  - https://www.learnopencv.com/head-pose-estimation-using-opencv-and-dlib/
//  - https://github.com/severin-lemaignan/gazr/
void FaceTracker::doCalculateFacialTransformation(void) {
	if(!facialFeaturesSet) {
		return;
	}
	if(!cameraModelSet) {
		doInitializeCameraModel();
	}

	solvePnP(facialFeatures3d, facialFeatures, cameraMatrix, distortionCoefficients, poseRotationVector, poseTranslationVector);
	poseSet = true;

	// Mat rot_mat;
	// Rodrigues(poseRotationVector, rot_mat);
	// Vec3d angles = Utilities::rotationMatrixToEulerAngles(rot_mat);
	// fprintf(stderr, "pose angle: <%.02f, %.02f, %.02f>\n", angles[0], angles[1], angles[2]);
}

bool FaceTracker::doConvertLandmarkPointToImagePoint(dlib::point *src, Point2d *dst) {
	static double classificationScaleFactor = frameDerivatives->getClassificationScaleFactor();
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
		if(facialFeaturesSet) {
			size_t featuresCount = facialFeatures.size();
			for(size_t i = 0; i < featuresCount; i++) {
				Utilities::drawX(frame, facialFeatures[i], Scalar(0, 255, 0));
			}
		}
	}
	if(poseSet) {
		std::vector<Point3d> gizmo3d(6);
		std::vector<Point2d> gizmo2d;
		gizmo3d[0] = Point3d(-50,0.0,0.0);
		gizmo3d[1] = Point3d(50,0.0,0.0);
		gizmo3d[2] = Point3d(0.0,-50,0.0);
		gizmo3d[3] = Point3d(0.0,50,0.0);
		gizmo3d[4] = Point3d(0.0,0.0,-50);
		gizmo3d[5] = Point3d(0.0,0.0,50);
		projectPoints(gizmo3d, poseRotationVector, poseTranslationVector, cameraMatrix, distortionCoefficients, gizmo2d);
		line(frame, gizmo2d[0], gizmo2d[1], Scalar(0, 0, 255), 1);
		line(frame, gizmo2d[2], gizmo2d[3], Scalar(0, 255, 0), 1);
		line(frame, gizmo2d[4], gizmo2d[5], Scalar(255, 0, 0), 1);
	}
}

TrackerState FaceTracker::getTrackerState(void) {
	return trackerState;
}

}; //namespace YerFace
