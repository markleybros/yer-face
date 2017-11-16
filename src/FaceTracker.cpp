
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

FaceTracker::FaceTracker(string myModelFileName, FrameDerivatives *myFrameDerivatives, int myFeatureBufferSize, float myFeatureSmoothingExponent) {
	modelFileName = myModelFileName;
	trackerState = DETECTING;
	classificationBoxSet = false;
	facialFeaturesSet = false;
	facialFeaturesInitialSet = false;
	facialTransformationSet = false;
	facialRotation = Vec3d(0.0, 0.0, 0.0);
	facialTranslation = Vec3d(0.0, 0.0, 0.0);

	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	featureBufferSize = myFeatureBufferSize;
	if(featureBufferSize <= 0) {
		throw invalid_argument("featureBufferSize cannot be less than or equal to zero.");
	}
	featureSmoothingExponent = myFeatureSmoothingExponent;
	if(featureSmoothingExponent <= 0.0) {
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

	doCalculateFacialTransformation();

	return trackerState;
}

void FaceTracker::doClassifyFace(void) {
	classificationBoxSet = false;
	//FIXME -- is this the "new CNN method" mentioned in the dlib blog?
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

	facialFeaturesBuffer.push_back(tempFeatures);
	while(facialFeaturesBuffer.size() > (unsigned int)featureBufferSize) {
		facialFeaturesBuffer.pop_front();
	}

	unsigned long numBufferEntries = facialFeaturesBuffer.size();
	unsigned long numFeatures = tempFeatures.size();
	for(i = 0; i < numFeatures; i++) {
		tempFeatures[i].x = 0.0;
		tempFeatures[i].y = 0.0;
	}
	double combinedWeights = 0.0;
	i = 0;
	for(std::vector<Point2d> featureSlot : facialFeaturesBuffer) {
		double weight = std::pow((double)(i + 1) / (double)numBufferEntries, (double)featureSmoothingExponent) - combinedWeights;
		combinedWeights += weight;
		for(j = 0; j < numFeatures; j++) {
			tempFeatures[j].x += featureSlot[j].x * weight;
			tempFeatures[j].y += featureSlot[j].y * weight;
		}
		i++;
	}

	facialFeatures = tempFeatures;
	facialFeaturesSet = true;
}

void FaceTracker::doCalculateFacialTransformation(void) {
	if(!facialFeaturesSet) {
		return;
	}
	if(!facialFeaturesInitialSet) {
		if(facialFeaturesBuffer.size() < (unsigned int)featureBufferSize) {
			return;
		}
		facialFeaturesInitial = facialFeatures;
		facialFeaturesInitialSet = true;
		return;
	}
	Size frameSize = frameDerivatives->getCurrentFrame().size();
	Point2d principalPoint = Point2d(frameSize.width / 2.0, frameSize.height / 2.0);
	Mat tempMatrix, tempMatrix2;
	try {
		tempMatrix = findEssentialMat(facialFeaturesInitial, facialFeatures, 1.0, principalPoint, RANSAC);
	} catch(exception &e) {
		fprintf(stderr, "FaceTracker: WARNING: Failed findEssentialMat(). Got exception: %s", e.what());
		return;
	}
	Vec3d vRotation, vTranslation, vRotationBest, vTranslationBest;
	double smallestDelta = -1;
	for(int i = 0; (i * 3) < tempMatrix.rows; i++) {
		tempMatrix2 = Mat(tempMatrix, Rect(0, i * 3, 3, 3));
		Mat rotation, translation;
		try {
			recoverPose(tempMatrix2, facialFeaturesInitial, facialFeatures, rotation, translation, 1.0, principalPoint);
		} catch(exception &e) {
			fprintf(stderr, "FaceTracker: WARNING: Failed recoverPose(). Got exception: %s", e.what());
			continue;
		}
		vRotation = Utilities::rotationMatrixToEulerAngles(rotation);
		vTranslation = Vec3d(translation.at<double>(0), translation.at<double>(1), translation.at<double>(2));
		double delta = Utilities::degreesDelta(vRotation[0], facialRotation[0]) +
				Utilities::degreesDelta(vRotation[1], facialRotation[1]) +
				Utilities::degreesDelta(vRotation[2], facialRotation[2]);
		if(smallestDelta < 0.0 || delta < smallestDelta) {
			smallestDelta = delta;
			vRotationBest = vRotation;
			vTranslationBest = vTranslation;
		}
		// fprintf(stderr, "FaceTracker: INFO: pose recovered... rotation: <%.02f, %.02f, %.02f>, translation: <%.02f, %.02f, %.02f>, delta: %.02f\n", vRotation[0], vRotation[1], vRotation[2], vTranslation[0], vTranslation[1], vTranslation[2], delta);
	}
	if(smallestDelta >= 0.0) {
		facialRotation = vRotationBest;
		facialTranslation = vTranslationBest;
		fprintf(stderr, "FaceTracker: INFO: Best facial transformation... Rotation: <%.02f, %.02f, %.02f>, Translation: <%.02f, %.02f, %.02f>\n", facialRotation[0], facialRotation[1], facialRotation[2], facialTranslation[0], facialTranslation[1], facialTranslation[2]);
		facialTransformationSet = true;
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
		size_t lineCount = facialFeatures.size();
		for(size_t i = 0; i < (lineCount - 1); i++) {
			line(frame, facialFeatures[i], facialFeatures[i+1], Scalar(0, 255, 0));
		}
	}
	// if(perspectiveTransformationMatrixSet) {
	// 	Mat warped;
	// 	warpPerspective(frame, warped, perspectiveTransformationMatrix, Size(1920, 1080), WARP_INVERSE_MAP);
	// 	imshow("othername", warped);
	// 	waitKey(1);
	// }
}

TrackerState FaceTracker::getTrackerState(void) {
	return trackerState;
}

}; //namespace YerFace
