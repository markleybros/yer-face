
#include "FaceTracker.hpp"
#include "Utilities.hpp"
#include <exception>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace std;
using namespace dlib;

namespace YerFace {

FaceTracker::FaceTracker(string myModelFileName, FrameDerivatives *myFrameDerivatives, int myFeatureBufferSize) {
	modelFileName = myModelFileName;
	trackerState = DETECTING;
	classificationBoxSet = false;
	facialFeaturesSet = false;

	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	featureBufferSize = myFeatureBufferSize;
	if(featureBufferSize <= 0) {
		throw invalid_argument("featureBufferSize cannot be less than or equal to zero.");
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

void FaceTracker::doIdentifyFeatures(void) {
	facialFeaturesSet = false;

	full_object_detection result = shapePredictor(dlibClassificationFrame, classificationBoxDlib);

	//Part 0, Outer corner of Left eye. (dlib index 0 -> YerFace::FaceTracker index 0)
	//Part 1, Inner corner of Left eye. (dlib index 1 -> YerFace::FaceTracker index 1)
	//Part 2, Outer corner of Right eye. (dlib index 2 -> YerFace::FaceTracker index 4)
	//Part 3, Inner corner of Right eye. (dlib index 3 -> YerFace::FaceTracker index 3)
	//Part 4, Bottom of nose. (dlib index 4 -> YerFace::FaceTracker index 2)
	Mat prevFrame = frameDerivatives->getPreviewFrame();
	double classificationScaleFactor = frameDerivatives->getClassificationScaleFactor();
	std::vector<Point2d> tempFeatures(result.num_parts());
	int invalidPoints = 0;
	unsigned long i, j;
	for(i = 0; i < result.num_parts(); i++) {
		j = i;
		if(i == 2) {
			j = 4;
		} else if(i == 4) {
			j = 2;
		}

		dlib::point part = result.part(i);
		if(part == OBJECT_PART_NOT_PRESENT) {
			invalidPoints++;
			continue;
		}
		Point2d partPoint = Point2d(part.x(), part.y());
		partPoint.x /= classificationScaleFactor;
		partPoint.y /= classificationScaleFactor;
		tempFeatures[j] = partPoint;
	}
	if(invalidPoints > 0) {
		trackerState = LOST;
		return;
	}

	facialFeaturesBuffer.push_front(tempFeatures);
	while(facialFeaturesBuffer.size() > (unsigned int)featureBufferSize) {
		facialFeaturesBuffer.pop_back();
	}

	unsigned long numBufferEntries = facialFeaturesBuffer.size();
	unsigned long numFeatures = tempFeatures.size();
	for(i = 0; i < numFeatures; i++) {
		tempFeatures[i].x = 0.0;
		tempFeatures[i].y = 0.0;
	}
	for(std::vector<Point2d> featureSlot : facialFeaturesBuffer) {
		for(i = 0; i < numFeatures; i++) {
			tempFeatures[i].x += featureSlot[i].x;
			tempFeatures[i].y += featureSlot[i].y;
		}
	}
	for(i = 0; i < numFeatures; i++) {
		tempFeatures[i].x /= numBufferEntries;
		tempFeatures[i].y /= numBufferEntries;
	}

	facialFeatures = tempFeatures;
	facialFeaturesSet = true;
}

void FaceTracker::renderPreviewHUD(bool verbose) {
	Mat frame = frameDerivatives->getPreviewFrame();
	if(verbose) {
		if(classificationBoxSet) {
			cv::rectangle(frame, classificationBoxNormalSize, Scalar(0, 255, 0), 1);
		}
	}
	if(facialFeaturesSet) {
		size_t lineCount = facialFeatures.size();
		for(size_t i = 0; i < (lineCount - 1); i++) {
			line(frame, facialFeatures[i], facialFeatures[i+1], Scalar(0, 255, 0));
		}
	}
}

TrackerState FaceTracker::getTrackerState(void) {
	return trackerState;
}

}; //namespace YerFace
