
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

FaceTracker::FaceTracker(string myModelFileName, FrameDerivatives *myFrameDerivatives) {
	modelFileName = myModelFileName;
	trackerState = DETECTING;
	classificationBoxSet = false;
	facialFeaturesSet = false;

	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	frontalFaceDetector = get_frontal_face_detector();
	deserialize(modelFileName.c_str()) >> shapePredictor;
	fprintf(stderr, "FaceTracker object constructed and ready to go!\n");
}

FaceTracker::~FaceTracker() {
	fprintf(stderr, "FaceTracker object destructing...\n");
}

TrackerState FaceTracker::processCurrentFrame(void) {
	Mat classificationFrame = frameDerivatives->getClassificationFrame();
	dlibClassificationFrame = cv_image<bgr_pixel>(classificationFrame);

	doClassifyFace();

	if(!classificationBoxSet) {
		return trackerState;
	}

	doIdentifyFeatures();

	doCalculateFacialTransformation();

	return trackerState;
}

void FaceTracker::doClassifyFace(void) {
	classificationBoxSet = false;
	//Using dlib's built-in HOG face detector instead of a CNN-based detector because it trades off accuracy for speed.
	std::vector<dlib::rectangle> faces = frontalFaceDetector(dlibClassificationFrame);

	int largestFace = -1;
	int largestFaceArea = -1;
	size_t facesCount = faces.size();
	for(size_t i = 0; i < facesCount; i++) {
		if((int)faces[i].area() > largestFaceArea) {
			largestFace = i;
			largestFaceArea = faces[i].area();
		}
	}
	if(largestFace >= 0) {
		trackerState = TRACKING;
		classificationBox.x = faces[largestFace].left();
		classificationBox.y = faces[largestFace].top();
		classificationBox.width = faces[largestFace].right() - classificationBox.x;
		classificationBox.height = faces[largestFace].bottom() - classificationBox.y;
		double classificationScaleFactor = frameDerivatives->getClassificationScaleFactor();
		classificationBoxNormalSize = Utilities::scaleRect(classificationBox, 1.0 / classificationScaleFactor);
		classificationBoxDlib = faces[largestFace];
		classificationBoxSet = true;
	} else {
		if(trackerState != DETECTING) {
			trackerState = LOST;
		}
	}
}

void FaceTracker::doIdentifyFeatures(void) {
	facialFeaturesSet = false;

	full_object_detection result = shapePredictor(dlibClassificationFrame, classificationBoxDlib);

	Mat prevFrame = frameDerivatives->getPreviewFrame();
	double classificationScaleFactor = frameDerivatives->getClassificationScaleFactor();
	std::vector<Point2d> tempFeatures(NUM_TRACKED_FEATURES);
	unsigned long i, j;
	for(i = j = 0; i < result.num_parts(); i++) {
		if(i == IDX_CHIN || i == IDX_NOSE_TIP || i == IDX_EYE_RIGHT_OUTER_CORNER || i == IDX_EYE_LEFT_OUTER_CORNER || i == IDX_MOUTH_RIGHT_OUTER_CORNER || i == IDX_MOUTH_LEFT_OUTER_CORNER) {
			dlib::point part = result.part(i);
			if(part == OBJECT_PART_NOT_PRESENT) {
				trackerState = LOST;
				return;
			}
			Point2d partPoint = Point2d(part.x(), part.y());
			partPoint.x /= classificationScaleFactor;
			partPoint.y /= classificationScaleFactor;
			Mat frame = frameDerivatives->getPreviewFrame(); //FIXME
			Utilities::drawX(frame, partPoint, Scalar(255, 0, 0));
			tempFeatures[j] = partPoint;
			j++;
		}
	}

	facialFeatures = tempFeatures;
	facialFeaturesSet = true;
}

void FaceTracker::doCalculateFacialTransformation(void) {
	if(!facialFeaturesSet) {
		return;
	}
}

void FaceTracker::renderPreviewHUD(bool verbose) {
	Mat frame = frameDerivatives->getPreviewFrame();
	if(verbose) {
		if(classificationBoxSet) {
			cv::rectangle(frame, classificationBoxNormalSize, Scalar(0, 255, 0), 1);
		}
	}
	if(facialFeaturesSet) {
		size_t featuresCount = facialFeatures.size();
		for(size_t i = 0; i < featuresCount; i++) {
			Utilities::drawX(frame, facialFeatures[i], Scalar(0, 255, 0));
		}
	}
}

TrackerState FaceTracker::getTrackerState(void) {
	return trackerState;
}

}; //namespace YerFace
