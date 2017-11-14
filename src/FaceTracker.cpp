
#include "FaceTracker.hpp"
#include "Utilities.hpp"
#include <exception>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace std;
using namespace dlib;

namespace YerFace {

FaceTracker::FaceTracker(string myModelFileName, FrameDerivatives *myFrameDerivatives, float myMinFaceSizePercentage) {
	modelFileName = myModelFileName;
	trackerState = DETECTING;
	classificationBoxSet = false;

	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	minFaceSizePercentage = myMinFaceSizePercentage;
	if(minFaceSizePercentage <= 0.0 || minFaceSizePercentage > 1.0) {
		throw invalid_argument("minFaceSizePercentage is out of range.");
	}
	frontalFaceDetector = get_frontal_face_detector();
	deserialize(modelFileName.c_str()) >> shapePredictor;
	fprintf(stderr, "FaceTracker object constructed and ready to go!\n");
}

FaceTracker::~FaceTracker() {
	fprintf(stderr, "FaceTracker object destructing...\n");
}

TrackerState FaceTracker::processCurrentFrame(void) {
	doClassifyFace();

	Mat classificationFrame = frameDerivatives->getClassificationFrame();
	dlibClassificationFrame = cv_image<bgr_pixel>(classificationFrame);

	if(!classificationBoxSet) {
		return trackerState;
	}

	full_object_detection result = shapePredictor(dlibClassificationFrame, classificationBoxDlib);

	Mat prevFrame = frameDerivatives->getPreviewFrame();
	double classificationScaleFactor = frameDerivatives->getClassificationScaleFactor();
	for(unsigned long i = 0; i < result.num_parts(); i++) {
		dlib::point part = result.part(i);
		if(part == OBJECT_PART_NOT_PRESENT) {
			continue;
		}
		Point2d partPoint = Point2d(part.x(), part.y());
		fprintf(stderr, "part %lu <%ld, %ld>\n", i, part.x(), part.y());
		partPoint.x /= classificationScaleFactor;
		partPoint.y /= classificationScaleFactor;
		fprintf(stderr, "after scaling... <%.02f, %.02f>\n", partPoint.x, partPoint.y);
		Utilities::drawX(prevFrame, partPoint, Scalar(0, 0, 255), 10, 3);
	}

	return trackerState;
}

void FaceTracker::doClassifyFace(void) {
	classificationBoxSet = false;
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

void FaceTracker::renderPreviewHUD(bool verbose) {
	Mat frame = frameDerivatives->getPreviewFrame();
	if(verbose) {
		if(classificationBoxSet) {
			cv::rectangle(frame, classificationBoxNormalSize, Scalar(0, 255, 0), 1);
		}
	}
}

TrackerState FaceTracker::getTrackerState(void) {
	return trackerState;
}

tuple<Rect2d, bool> FaceTracker::getFaceRect(void) {
	return make_tuple(faceRect, faceRectSet);
}

}; //namespace YerFace
