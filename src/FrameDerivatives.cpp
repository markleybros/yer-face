
#include "FrameDerivatives.hpp"
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
	if(SDL_LockMutex(myMutex) != 0) {
		throw runtime_error("Failed to lock mutex.");
	}
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
	SDL_UnlockMutex(myMutex);
}

Mat FrameDerivatives::getWorkingFrame(void) {
	if(SDL_LockMutex(myMutex) != 0) {
		throw runtime_error("Failed to lock mutex.");
	}
	Mat value = workingFrame;
	SDL_UnlockMutex(myMutex);
	return value;
}

unsigned long FrameDerivatives::getWorkingFrameNumber(void) {
	if(SDL_LockMutex(myMutex) != 0) {
		throw runtime_error("Failed to lock mutex.");
	}
	unsigned long value = workingFrameNumber;
	SDL_UnlockMutex(myMutex);
	return value;
}

void FrameDerivatives::advanceWorkingFrameToCompleted(void) {
	if(SDL_LockMutex(myMutex) != 0) {
		throw runtime_error("Failed to lock mutex.");
	}
	completedFrame = workingFrame.clone();
	completedFrameNumber = workingFrameNumber;
	completedFrameSet = true;
	SDL_UnlockMutex(myMutex);
}

Mat FrameDerivatives::getClassificationFrame(void) {
	if(SDL_LockMutex(myMutex) != 0) {
		throw runtime_error("Failed to lock mutex.");
	}
	Mat value = classificationFrame;
	SDL_UnlockMutex(myMutex);
	return value;
}

Mat FrameDerivatives::getPreviewFrame(void) {
	if(SDL_LockMutex(myMutex) != 0) {
		throw runtime_error("Failed to lock mutex.");
	}
	if(!completedFrameSet) {
		SDL_UnlockMutex(myMutex);
		throw runtime_error("Preview frame was requested, but no working frame has been advanced to completed frame yet.");
	}
	if(!previewFrameCloned) {
		previewFrame = completedFrame.clone();
		previewFrameCloned = true;
	}
	Mat value = previewFrame;
	SDL_UnlockMutex(myMutex);
	return value;
}

void FrameDerivatives::resetPreviewFrame(void) {
	if(SDL_LockMutex(myMutex) != 0) {
		throw runtime_error("Failed to lock mutex.");
	}
	previewFrameCloned = false;
	SDL_UnlockMutex(myMutex);
}

double FrameDerivatives::getClassificationScaleFactor(void) {
	if(SDL_LockMutex(myMutex) != 0) {
		throw runtime_error("Failed to lock mutex.");
	}
	double status = classificationScaleFactor;
	SDL_UnlockMutex(myMutex);
	return status;
}

Size FrameDerivatives::getWorkingFrameSize(void) {
	if(SDL_LockMutex(myMutex) != 0) {
		throw runtime_error("Failed to lock mutex.");
	}
	Size size = workingFrame.size();
	SDL_UnlockMutex(myMutex);
	return size;
}

}; //namespace YerFace
