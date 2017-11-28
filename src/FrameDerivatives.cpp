
#include "FrameDerivatives.hpp"
#include <exception>
#include <cstdio>

using namespace std;

namespace YerFace {

FrameDerivatives::FrameDerivatives(int myClassificationBoundingBox, double myClassificationScaleFactor) {
	logger = new Logger("FrameDerivatives");
	if(myClassificationBoundingBox < 0) {
		throw invalid_argument("Classification Bounding Box is invalid.");
	}
	classificationBoundingBox = myClassificationBoundingBox;
	if(myClassificationScaleFactor < 0.0 || myClassificationScaleFactor > 1.0) {
		throw invalid_argument("Classification Scale Factor is invalid.");
	}
	classificationScaleFactor = myClassificationScaleFactor;
	metrics = new Metrics("FrameDerivatives");
	logger->debug("FrameDerivatives constructed and ready to go!");
}

FrameDerivatives::~FrameDerivatives() {
	logger->debug("FrameDerivatives object destructing...");
	delete metrics;
	delete logger;
}

void FrameDerivatives::setCurrentFrame(Mat newFrame) {
	metrics->startClock();
	currentFrame = newFrame;

	Size frameSize = currentFrame.size();

	if(classificationBoundingBox > 0) {
		if(frameSize.width >= frameSize.height) {
			classificationScaleFactor = (double)classificationBoundingBox / (double)frameSize.width;
		} else {
			classificationScaleFactor = (double)classificationBoundingBox / (double)frameSize.height;
		}
	}

	resize(currentFrame, classificationFrame, Size(), classificationScaleFactor, classificationScaleFactor);

	static bool reportedScale = false;
	if(!reportedScale) {
		logger->debug("Scaled current frame <%dx%d> down to <%dx%d> for classification", frameSize.width, frameSize.height, classificationFrame.size().width, classificationFrame.size().height);
		reportedScale = true;
	}

	previewFrameCloned = false;
	metrics->endClock();
	logger->verbose("FrameDerivatives %s", metrics->getTimesString());
}
Mat FrameDerivatives::getCurrentFrame(void) {
	return currentFrame;
}

Mat FrameDerivatives::getClassificationFrame(void) {
	return classificationFrame;
}

Mat FrameDerivatives::getPreviewFrame(void) {
	if(!previewFrameCloned) {
		previewFrame = currentFrame.clone();
		previewFrameCloned = true;
	}
	return previewFrame;
}

double FrameDerivatives::getClassificationScaleFactor(void) {
	return classificationScaleFactor;
}

Size FrameDerivatives::getCurrentFrameSize(void) {
	return currentFrame.size();
}

}; //namespace YerFace
