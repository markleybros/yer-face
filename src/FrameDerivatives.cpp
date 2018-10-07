
#include "FrameDerivatives.hpp"
#include "Utilities.hpp"
#include <exception>
#include <cstdio>

using namespace std;

namespace YerFace {

FrameDerivatives::FrameDerivatives(json config) {
	logger = new Logger("FrameDerivatives");
	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	classificationBoundingBox = config["YerFace"]["FrameDerivatives"]["classificationBoundingBox"];
	if(classificationBoundingBox < 0) {
		throw invalid_argument("Classification Bounding Box is invalid.");
	}
	classificationScaleFactor = config["YerFace"]["FrameDerivatives"]["classificationScaleFactor"];
	if(classificationScaleFactor < 0.0 || classificationScaleFactor > 1.0) {
		throw invalid_argument("Classification Scale Factor is invalid.");
	}
	workingFrameSet = false;
	completedFrameSet = false;
	workingPreviewFrameSet = false;
	completedPreviewFrameSet = false;
	workingFrameSizeSet = false;
	workingFrameTimestamps.frameNumber = 0;
	workingFrameTimestamps.set = false;
	completedFrameTimestamps.set = false;
	metrics = new Metrics(config, "FrameDerivatives", this);
	logger->debug("FrameDerivatives constructed and ready to go!");
}

FrameDerivatives::~FrameDerivatives() {
	logger->debug("FrameDerivatives object destructing...");
	SDL_DestroyMutex(myMutex);
	delete metrics;
	delete logger;
}

void FrameDerivatives::setWorkingFrame(VideoFrame *videoFrame) {
	YerFace_MutexLock(myMutex);
	metrics->startClock();
	workingFrame = videoFrame->frameCV.clone();

	Size frameSize = workingFrame.size();

	workingFrameSize = frameSize;
	workingFrameSizeSet = true;

	workingFrameTimestamps.frameNumber++;
	workingFrameTimestamps.startTimestamp = videoFrame->timestamp;
	workingFrameTimestamps.estimatedEndTimestamp = videoFrame->estimatedEndTimestamp;
	workingFrameTimestamps.set = true;
	// logger->verbose("Set Working Frame Timestamps... Start: %.04lf, Estimated End: %.04lf", workingFrameTimestamps.startTimestamp, workingFrameTimestamps.estimatedEndTimestamp);

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

Mat FrameDerivatives::getCompletedFrame(void) {
	YerFace_MutexLock(myMutex);
	if(!completedFrameSet) {
		YerFace_MutexUnlock(myMutex);
		throw runtime_error("getCompletedFrame() called, but no completed frame set");
	}
	Mat value = completedFrame;
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
	completedFrameTimestamps = workingFrameTimestamps;
	if(workingPreviewFrameSet) {
		completedPreviewFrameSource = workingPreviewFrame;
	} else {
		completedPreviewFrameSource = completedFrame;
	}
	completedPreviewFrameSet = false;
	workingPreviewFrameSet = false;
	YerFace_MutexUnlock(myMutex);
}

ClassificationFrame FrameDerivatives::getClassificationFrame(void) {
	YerFace_MutexLock(myMutex);
	ClassificationFrame result;
	result.timestamps.set = false;
	result.set = false;
	if(!workingFrameSet || !workingFrameTimestamps.set) {
		YerFace_MutexUnlock(myMutex);
		return result;
	}
	result.timestamps = workingFrameTimestamps;
	result.frame = classificationFrame;
	result.scaleFactor = classificationScaleFactor;
	result.set = true;
	YerFace_MutexUnlock(myMutex);
	return result;
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

FrameTimestamps FrameDerivatives::getWorkingFrameTimestamps(void) {
	YerFace_MutexLock(myMutex);
	if(!workingFrameTimestamps.set) {
		YerFace_MutexUnlock(myMutex);
		throw runtime_error("getWorkingFrameTimestamps() called, but no working frame timestamps available");
	}
	FrameTimestamps timestamps = workingFrameTimestamps;
	YerFace_MutexUnlock(myMutex);
	return timestamps;
}

FrameTimestamps FrameDerivatives::getCompletedFrameTimestamps(void) {
	YerFace_MutexLock(myMutex);
	if(!completedFrameTimestamps.set) {
		YerFace_MutexUnlock(myMutex);
		throw runtime_error("getCompletedFrameTimestamps() called, but no timestamps set");
	}
	FrameTimestamps timestamps = completedFrameTimestamps;
	YerFace_MutexUnlock(myMutex);
	return timestamps;
}

bool FrameDerivatives::getCompletedFrameSet(void) {
	YerFace_MutexLock(myMutex);
	bool status = completedFrameSet;
	YerFace_MutexUnlock(myMutex);
	return status;
}

}; //namespace YerFace
