
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
	workingFrameSet = false;
	completedFrameSet = false;
	workingPreviewFrameSet = false;
	completedPreviewFrameSet = false;
	workingFrameSizeSet = false;
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

void FrameDerivatives::setWorkingFrame(Mat newFrame, double timestamp) {
	YerFace_MutexLock(myMutex);
	metrics->startClock();
	workingFrame = newFrame.clone();

	Size frameSize = workingFrame.size();

	workingFrameSize = frameSize;
	workingFrameSizeSet = true;

	workingFrameTimestamp = timestamp;
	workingFrameTimestampSet = true;

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


	workingFrameSet = true;
	workingPreviewFrameSet = false;

	metrics->endClock();
	YerFace_MutexUnlock(myMutex);
}

Mat FrameDerivatives::getWorkingFrame(void) {
	YerFace_MutexLock(myMutex);
	if(!workingFrameSet) {
		YerFace_MutexUnlock(myMutex);
		throw runtime_error("getWorkingFrame() called, but no working frame set");
	}
	Mat value = workingFrame;
	YerFace_MutexUnlock(myMutex);
	return value;
}

void FrameDerivatives::advanceWorkingFrameToCompleted(void) {
	YerFace_MutexLock(myMutex);
	if(!workingFrameSet) {
		YerFace_MutexUnlock(myMutex);
		throw runtime_error("advanceWorkingFrameToCompleted() called, but no working frame set");
	}
	completedFrame = workingFrame;
	completedFrameSet = true;
	workingFrameSet = false;
	if(workingPreviewFrameSet) {
		completedPreviewFrameSource = workingPreviewFrame;
	} else {
		completedPreviewFrameSource = completedFrame;
	}
	completedPreviewFrameSet = false;
	workingPreviewFrameSet = false;
	YerFace_MutexUnlock(myMutex);
}

Mat FrameDerivatives::getClassificationFrame(void) {
	YerFace_MutexLock(myMutex);
	if(!workingFrameSet) {
		YerFace_MutexUnlock(myMutex);
		throw runtime_error("getClassificationFrame() called, but no working frame set");
	}
	Mat value = classificationFrame;
	YerFace_MutexUnlock(myMutex);
	return value;
}

Mat FrameDerivatives::getWorkingPreviewFrame(void) {
	YerFace_MutexLock(myMutex);
	if(!workingFrameSet) {
		YerFace_MutexUnlock(myMutex);
		throw runtime_error("getWorkingPreviewFrame() called, but no working frame set");
	}
	if(!workingPreviewFrameSet) {
		workingPreviewFrame = workingFrame.clone();
		workingPreviewFrameSet = true;
	}
	Mat value = workingPreviewFrame;
	YerFace_MutexUnlock(myMutex);
	return value;
}

Mat FrameDerivatives::getCompletedPreviewFrame(void) {
	YerFace_MutexLock(myMutex);
	if(!completedFrameSet) {
		YerFace_MutexUnlock(myMutex);
		throw runtime_error("getCompletedPreviewFrame() called, but no completed frame set");
	}
	if(!completedPreviewFrameSet) {
		completedPreviewFrame = completedPreviewFrameSource.clone();
		completedPreviewFrameSet = true;
	}
	Mat value = completedPreviewFrame;
	YerFace_MutexUnlock(myMutex);
	return value;
}

void FrameDerivatives::resetCompletedPreviewFrame(void) {
	YerFace_MutexLock(myMutex);
	completedPreviewFrameSet = false;
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
	if(!workingFrameSizeSet) {
		YerFace_MutexUnlock(myMutex);
		throw runtime_error("getWorkingFrameSize() called, but no cached working frame size");
	}
	Size size = workingFrameSize;
	YerFace_MutexUnlock(myMutex);
	return size;
}

double FrameDerivatives::getWorkingFrameTimestamp(void) {
	YerFace_MutexLock(myMutex);
	if(!workingFrameTimestampSet) {
		YerFace_MutexUnlock(myMutex);
		throw runtime_error("getWorkingFrameTimestamp() called, but no cached working frame timestamp");
	}
	double timestamp = workingFrameTimestamp;
	YerFace_MutexUnlock(myMutex);
	return timestamp;
}

bool FrameDerivatives::getCompletedFrameSet(void) {
	YerFace_MutexLock(myMutex);
	bool status = completedFrameSet;
	YerFace_MutexUnlock(myMutex);
	return status;
}


}; //namespace YerFace
