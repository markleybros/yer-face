
#include "FrameDerivatives.hpp"
#include <exception>
#include <stdio.h>

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

	//Perform derivation for classification operations.
	Mat tempFrame;
	cvtColor(currentFrame, tempFrame, COLOR_BGR2GRAY);
	resize(tempFrame, classificationFrame, Size(), classificationScaleFactor, classificationScaleFactor);
	equalizeHist(classificationFrame, classificationFrame);

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

}; //namespace YerFace
