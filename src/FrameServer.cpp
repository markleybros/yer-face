
#include "FrameServer.hpp"
#include "Utilities.hpp"
#include <exception>
#include <cstdio>

using namespace std;

namespace YerFace {

FrameServer::FrameServer(json config, bool myLowLatency) {
	logger = new Logger("FrameServer");
	lowLatency = myLowLatency;
	string lowLatencyKey = "LowLatency";
	if(!lowLatency) {
		lowLatencyKey = "Offline";
	}
	classificationBoundingBox = config["YerFace"]["FrameServer"][lowLatencyKey]["classificationBoundingBox"];
	if(classificationBoundingBox < 0) {
		throw invalid_argument("Classification Bounding Box is invalid.");
	}
	classificationScaleFactor = config["YerFace"]["FrameServer"][lowLatencyKey]["classificationScaleFactor"];
	if(classificationScaleFactor < 0.0 || classificationScaleFactor > 1.0) {
		throw invalid_argument("Classification Scale Factor is invalid.");
	}

	for(unsigned int i = 0; i <= FRAME_STATUS_MAX; i++) {
		onFrameStatusChangeCallbacks[i].clear();
	}

	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	draining = false;
	herderRunning = true;
	if((herderThread = SDL_CreateThread(FrameServer::frameHerderLoop, "FrameHerder", (void *)this)) == NULL) {
		throw runtime_error("Failed starting thread!");
	}

	metrics = new Metrics(config, "FrameServer");
	logger->debug("FrameServer constructed and ready to go!");
}

FrameServer::~FrameServer() noexcept(false) {
	logger->debug("FrameServer object destructing...");

	YerFace_MutexLock(myMutex);
	if(!draining) {
		logger->warn("Was never set to draining! You should always drain the FrameServer before destructing it.");
		draining = true;
	}
	herderRunning = false;
	YerFace_MutexUnlock(myMutex);

	SDL_WaitThread(herderThread, NULL);
	
	YerFace_MutexLock(myMutex);
	if(frameStore.size() > 0) {
		logger->warn("Frames are still sitting in the frame store! Draining did not complete!");
	}
	YerFace_MutexUnlock(myMutex);

	SDL_DestroyMutex(myMutex);
	delete metrics;
	delete logger;
}

void FrameServer::onFrameStatusChangeEvent(WorkingFrameStatus newStatus, function<void(FrameNumber frameNumber, WorkingFrameStatus newStatus)> callback) {
	checkStatusValue(newStatus);
	YerFace_MutexLock(myMutex);
	onFrameStatusChangeCallbacks[newStatus].push_back(callback);
	YerFace_MutexUnlock(myMutex);
}

void FrameServer::registerFrameStatusCheckpoint(WorkingFrameStatus status, string checkpointKey) {
	checkStatusValue(status);
	if(status == FRAME_STATUS_GONE) {
		throw invalid_argument("Somebody tried to register a checkpoint for FRAME_STATUS_GONE, but this doesn't make sense because FRAME_STATUS_GONE means the frame is about to be cleaned up.");
	}
	YerFace_MutexLock(myMutex);
	statusCheckpoints[status].push_back(checkpointKey);
	YerFace_MutexUnlock(myMutex);
}

void FrameServer::insertNewFrame(VideoFrame *videoFrame) {
	MetricsTick tick = metrics->startClock();

	YerFace_MutexLock(myMutex);

	if(draining) {
		YerFace_MutexUnlock(myMutex);
		throw logic_error("Can't insert new frame while draining!");
	}

	WorkingFrame *workingFrame = new WorkingFrame();

	if((workingFrame->previewFrameMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	workingFrame->frame = videoFrame->frameCV.clone();
	workingFrame->previewFrame = workingFrame->frame.clone();

	frameSize = workingFrame->frame.size();
	frameSizeSet = true;

	workingFrame->frameTimestamps = videoFrame->timestamp;

	if(classificationBoundingBox > 0) {
		if(frameSize.width >= frameSize.height) {
			classificationScaleFactor = (double)classificationBoundingBox / (double)frameSize.width;
		} else {
			classificationScaleFactor = (double)classificationBoundingBox / (double)frameSize.height;
		}
	}

	resize(workingFrame->frame, workingFrame->classificationFrame, Size(), classificationScaleFactor, classificationScaleFactor);

	static bool reportedScale = false;
	if(!reportedScale) {
		logger->debug("Scaled current frame <%dx%d> down to <%dx%d> for classification", frameSize.width, frameSize.height, workingFrame->classificationFrame.size().width, workingFrame->classificationFrame.size().height);
		reportedScale = true;
	}

	// Set all of the registered checkpoints to FALSE to accurately record the frame's status.
	for(unsigned int i = 0; i <= FRAME_STATUS_MAX; i++) {
		for(string checkpointKey : statusCheckpoints[i]) {
			workingFrame->checkpoints[i][checkpointKey] = false;
		}
	}

	frameStore[workingFrame->frameTimestamps.frameNumber] = workingFrame;
	logger->verbose("Inserted new working frame %ld into frame store. Frame store size is now %lu", workingFrame->frameTimestamps.frameNumber, frameStore.size());

	setFrameStatus(workingFrame->frameTimestamps.frameNumber, FRAME_STATUS_NEW);

	metrics->endClock(tick);
	YerFace_MutexUnlock(myMutex);
}

void FrameServer::setDraining(void) {
	YerFace_MutexLock(myMutex);
	if(draining) {
		YerFace_MutexUnlock(myMutex);
		throw logic_error("Can't set draining while already draining!");
	}
	draining = true;
	logger->verbose("Set to draining!");
	YerFace_MutexUnlock(myMutex);
}

bool FrameServer::isDrained(void) {
	bool drained;
	YerFace_MutexLock(myMutex);
	drained = draining && frameStore.size() == 0;
	YerFace_MutexUnlock(myMutex);
	return drained;
}

WorkingFrame *FrameServer::getWorkingFrame(FrameNumber frameNumber) {
	YerFace_MutexLock(myMutex);
	auto frameIter = frameStore.find(frameNumber);
	if(frameIter == frameStore.end()) {
		YerFace_MutexUnlock(myMutex);
		throw runtime_error("getWorkingFrame() called, but the referenced frame does not exist in the frame store!");
	}
	YerFace_MutexUnlock(myMutex);
	return frameIter->second;
}

void FrameServer::setWorkingFrameStatusCheckpoint(FrameNumber frameNumber, WorkingFrameStatus status, string checkpointKey) {
	checkStatusValue(status);
	YerFace_MutexLock(myMutex);
	WorkingFrame *frame;
	try {
		frame = getWorkingFrame(frameNumber);
	} catch(exception &e) {
		YerFace_MutexUnlock(myMutex);
		throw;
	}
	if(status != frame->status) {
		YerFace_MutexUnlock(myMutex);
		throw logic_error("Trying to set a checkpoint on a status for a frame whose current status does not match!");
	}
	auto checkpointIter = frame->checkpoints[status].find(checkpointKey);
	if(checkpointIter == frame->checkpoints[status].end()) {
		YerFace_MutexUnlock(myMutex);
		throw logic_error("Trying to set a checkpoint on a status for a frame but that checkpoint was never registered!");
	}
	if(checkpointIter->second) {
		YerFace_MutexUnlock(myMutex);
		throw logic_error("Trying to set a checkpoint on a status for a frame, but the checkpoint was already set!");
	}
	frame->checkpoints[status][checkpointKey] = true;
	YerFace_MutexUnlock(myMutex);
}

// Mat FrameServer::getWorkingPreviewFrame(void) {
// 	YerFace_MutexLock(myMutex);
// 	if(!workingFrameSet) {
// 		YerFace_MutexUnlock(myMutex);
// 		throw runtime_error("getWorkingPreviewFrame() called, but no working frame set");
// 	}
// 	if(!workingPreviewFrameSet) {
// 		workingPreviewFrame = workingFrame.clone();
// 		workingPreviewFrameSet = true;
// 	}
// 	Mat value = workingPreviewFrame;
// 	YerFace_MutexUnlock(myMutex);
// 	return value;
// }

// Mat FrameServer::getCompletedPreviewFrame(void) {
// 	YerFace_MutexLock(myMutex);
// 	if(!completedFrameSet) {
// 		YerFace_MutexUnlock(myMutex);
// 		throw runtime_error("getCompletedPreviewFrame() called, but no completed frame set");
// 	}
// 	if(!completedPreviewFrameSet) {
// 		completedPreviewFrame = completedPreviewFrameSource.clone();
// 		completedPreviewFrameSet = true;
// 	}
// 	Mat value = completedPreviewFrame;
// 	YerFace_MutexUnlock(myMutex);
// 	return value;
// }

// void FrameServer::resetCompletedPreviewFrame(void) {
// 	YerFace_MutexLock(myMutex);
// 	completedPreviewFrameSet = false;
// 	YerFace_MutexUnlock(myMutex);
// }

// Size FrameServer::getWorkingFrameSize(void) {
// 	YerFace_MutexLock(myMutex);
// 	if(!workingFrameSizeSet) {
// 		YerFace_MutexUnlock(myMutex);
// 		throw runtime_error("getWorkingFrameSize() called, but no cached working frame size");
// 	}
// 	Size size = workingFrameSize;
// 	YerFace_MutexUnlock(myMutex);
// 	return size;
// }

// FrameTimestamps FrameServer::getWorkingFrameTimestamps(void) {
// 	YerFace_MutexLock(myMutex);
// 	if(!workingFrameTimestamps.set) {
// 		YerFace_MutexUnlock(myMutex);
// 		throw runtime_error("getWorkingFrameTimestamps() called, but no working frame timestamps available");
// 	}
// 	FrameTimestamps timestamps = workingFrameTimestamps;
// 	YerFace_MutexUnlock(myMutex);
// 	return timestamps;
// }

// FrameTimestamps FrameServer::getCompletedFrameTimestamps(void) {
// 	YerFace_MutexLock(myMutex);
// 	if(!completedFrameTimestamps.set) {
// 		YerFace_MutexUnlock(myMutex);
// 		throw runtime_error("getCompletedFrameTimestamps() called, but no timestamps set");
// 	}
// 	FrameTimestamps timestamps = completedFrameTimestamps;
// 	YerFace_MutexUnlock(myMutex);
// 	return timestamps;
// }

// bool FrameServer::getCompletedFrameSet(void) {
// 	YerFace_MutexLock(myMutex);
// 	bool status = completedFrameSet;
// 	YerFace_MutexUnlock(myMutex);
// 	return status;
// }

void FrameServer::destroyFrame(FrameNumber frameNumber) {
	SDL_DestroyMutex(frameStore[frameNumber]->previewFrameMutex);
	delete frameStore[frameNumber];
	frameStore.erase(frameNumber);
}

void FrameServer::setFrameStatus(FrameNumber frameNumber, WorkingFrameStatus newStatus) {
	checkStatusValue(newStatus);
	YerFace_MutexLock(myMutex);
	frameStore[frameNumber]->status = newStatus;
	for(auto callback : onFrameStatusChangeCallbacks[newStatus]) {
		callback(frameNumber, newStatus);
	}
	YerFace_MutexUnlock(myMutex);
}

void FrameServer::checkStatusValue(WorkingFrameStatus status) {
	if(status < 0 || status > FRAME_STATUS_MAX) {
		throw invalid_argument("passed invalid WorkingFrameStatus!");
	}
}

int FrameServer::frameHerderLoop(void *ptr) {
	FrameServer *self = (FrameServer *)ptr;
	self->logger->verbose("Frame Herder Thread alive!");

	YerFace_MutexLock(self->myMutex);
	while(self->herderRunning || !self->isDrained()) {
		std::list<FrameNumber> garbageFrames;
		for(auto framePair : self->frameStore) {
			FrameNumber frameNumber = framePair.first;
			WorkingFrame *workingFrame = framePair.second;
			WorkingFrameStatus status = workingFrame->status;

			//Does this frame need to be garbage collected?
			if(status == FRAME_STATUS_GONE) {
				garbageFrames.push_back(frameNumber);
				continue;
			}

			//Is this frame eligible for a new status?
			bool checkpointsPassed = true;
			for(auto checkpointPair : workingFrame->checkpoints[status]) {
				if(!checkpointPair.second) {
					checkpointsPassed = false;
				}
			}

			//Advance this frame to the next status.
			if(checkpointsPassed) {
				self->setFrameStatus(frameNumber, (WorkingFrameStatus)(status + 1));
			}
		}

		//Destroy GONE frames.
		while(garbageFrames.size() > 0) {
			self->destroyFrame(garbageFrames.front());
			garbageFrames.pop_front();
		}

		//Relinquish control.
		YerFace_MutexUnlock(self->myMutex);
		SDL_Delay(0);
		YerFace_MutexLock(self->myMutex);
	}
	YerFace_MutexUnlock(self->myMutex);

	self->logger->verbose("Frame Herder Thread quitting...");
	return 0;
}

}; //namespace YerFace
