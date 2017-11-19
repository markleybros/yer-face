
#include "FrameDerivatives.hpp"
#include <exception>
#include <cstdio>

using namespace std;

namespace YerFace {

FrameDerivatives::FrameDerivatives(double myClassificationScaleFactor) {
	if(myClassificationScaleFactor < 0.0 || myClassificationScaleFactor > 1.0) {
		throw invalid_argument("Classification Scale Factor is invalid.");
	}
	classificationScaleFactor = myClassificationScaleFactor;
	fprintf(stderr, "FrameDerivatives constructed and ready to go!\n");
}

FrameDerivatives::~FrameDerivatives() {
	fprintf(stderr, "FrameDerivatives object destructing...\n");
}

void FrameDerivatives::setCurrentFrame(Mat newFrame) {
	currentFrame = newFrame;

	resize(currentFrame, classificationFrame, Size(), classificationScaleFactor, classificationScaleFactor);

	previewFrameCloned = false;
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
