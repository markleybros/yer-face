
#include "FrameDerivatives.hpp"
#include "Utilities.hpp"
#include <exception>
#include <cstdio>

using namespace std;

namespace YerFace {

FrameDerivatives::FrameDerivatives(int myClassificationBoundingBox, double myClassificationScaleFactor) {
	logger = new Logger("FrameDerivatives");
	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if(myClassificationBoundingBox < 0) {
		throw invalid_argument("Classification Bounding Box is invalid.");
	}
	classificationBoundingBox = myClassificationBoundingBox;
	if(myClassificationScaleFactor < 0.0 || myClassificationScaleFactor > 1.0) {
		throw invalid_argument("Classification Scale Factor is invalid.");
	}
	completedFrameSet = false;
	classificationScaleFactor = myClassificationScaleFactor;
	metrics = new Metrics("FrameDerivatives");
	logger->debug("FrameDerivatives constructed and ready to go!");
}

FrameDerivatives::~FrameDerivatives() {
	logger->debug("FrameDerivatives object destructing...");
	SDL_DestroyMutex(myMutex);
	delete metrics;
	delete logger;
}

void FrameDerivatives::setWorkingFrame(Mat newFrame, unsigned long newFrameNumber) {
	YerFace_MutexLock(myMutex);
	metrics->startClock();
	workingFrame = newFrame;
	workingFrameNumber = newFrameNumber;

	Size frameSize = workingFrame.size();

	if(classificationBoundingBox > 0) {
		if(frameSize.width >= frameSize.height) {
			classificationScaleFactor = (double)classificationBoundingBox / (double)frameSize.width;
		} else {
			classificationScaleFactor = (double)classificationBoundingBox / (double)frameSize.height;
		}
	}

	resize(workingFrame, classificationFrame, Size(), classificationScaleFactor, classificationScaleFactor);

	static bool reportedScale = false;
	if(!reportedScale) {
		logger->debug("Scaled current frame <%dx%d> down to <%dx%d> for classification", frameSize.width, frameSize.height, classificationFrame.size().width, classificationFrame.size().height);
		reportedScale = true;
	}

	previewFrameCloned = false;
	metrics->endClock();
	YerFace_MutexUnlock(myMutex);
}

Mat FrameDerivatives::getWorkingFrame(void) {
	YerFace_MutexLock(myMutex);
	Mat value = workingFrame;
	YerFace_MutexUnlock(myMutex);
	return value;
}

unsigned long FrameDerivatives::getWorkingFrameNumber(void) {
	YerFace_MutexLock(myMutex);
	unsigned long value = workingFrameNumber;
	YerFace_MutexUnlock(myMutex);
	return value;
}

void FrameDerivatives::advanceWorkingFrameToCompleted(void) {
	YerFace_MutexLock(myMutex);
	completedFrame = workingFrame.clone();
	completedFrameNumber = workingFrameNumber;
	completedFrameSet = true;
	YerFace_MutexUnlock(myMutex);
}

Mat FrameDerivatives::getClassificationFrame(void) {
	YerFace_MutexLock(myMutex);
	Mat value = classificationFrame;
	YerFace_MutexUnlock(myMutex);
	return value;
}

Mat FrameDerivatives::getPreviewFrame(void) {
	YerFace_MutexLock(myMutex);
	if(!completedFrameSet) {
		YerFace_MutexUnlock(myMutex);
		throw runtime_error("Preview frame was requested, but no working frame has been advanced to completed frame yet.");
	}
	if(!previewFrameCloned) {
		previewFrame = completedFrame.clone();
		previewFrameCloned = true;
	}
	Mat value = previewFrame;
	YerFace_MutexUnlock(myMutex);
	return value;
}

void FrameDerivatives::resetPreviewFrame(void) {
	YerFace_MutexLock(myMutex);
	previewFrameCloned = false;
	YerFace_MutexUnlock(myMutex);
}

double FrameDerivatives::getClassificationScaleFactor(void) {
	YerFace_MutexLock(myMutex);
	double status = classificationScaleFactor;
	YerFace_MutexUnlock(myMutex);
	return status;
}

Size FrameDerivatives::getWorkingFrameSize(void) {
	YerFace_MutexLock(myMutex);
	Size size = workingFrame.size();
	YerFace_MutexUnlock(myMutex);
	return size;
}

}; //namespace YerFace
