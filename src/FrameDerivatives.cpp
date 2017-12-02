
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

void FrameDerivatives::setCurrentFrame(Mat newFrame) {
	if(SDL_LockMutex(myMutex) != 0) {
		throw runtime_error("Failed to lock mutex.");
	}
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
	SDL_UnlockMutex(myMutex);
}

Mat FrameDerivatives::getCurrentFrame(void) {
	if(SDL_LockMutex(myMutex) != 0) {
		throw runtime_error("Failed to lock mutex.");
	}
	Mat value = currentFrame;
	SDL_UnlockMutex(myMutex);
	return value;
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
	if(!previewFrameCloned) {
		previewFrame = currentFrame.clone();
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

Size FrameDerivatives::getCurrentFrameSize(void) {
	if(SDL_LockMutex(myMutex) != 0) {
		throw runtime_error("Failed to lock mutex.");
	}
	Size size = currentFrame.size();
	SDL_UnlockMutex(myMutex);
	return size;
}

}; //namespace YerFace
