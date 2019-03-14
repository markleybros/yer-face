
#include "FrameServer.hpp"
#include "Utilities.hpp"
#include <exception>
#include <cstdio>

using namespace std;

namespace YerFace {

FrameServer::FrameServer(json config, Status *myStatus, bool myLowLatency) {
	logger = new Logger("FrameServer");
	status = myStatus;
	if(status == NULL) {
		throw invalid_argument("status cannot be NULL");
	}
	lowLatency = myLowLatency;
	string lowLatencyKey = "LowLatency";
	if(!lowLatency) {
		lowLatencyKey = "Offline";
	}
	detectionBoundingBox = config["YerFace"]["FrameServer"][lowLatencyKey]["detectionBoundingBox"];
	if(detectionBoundingBox < 0) {
		throw invalid_argument("Detection Bounding Box is invalid.");
	}
	detectionScaleFactor = config["YerFace"]["FrameServer"][lowLatencyKey]["detectionScaleFactor"];
	if(detectionScaleFactor < 0.0 || detectionScaleFactor > 1.0) {
		throw invalid_argument("Detection Scale Factor is invalid.");
	}

	for(unsigned int i = 0; i <= FRAME_STATUS_MAX; i++) {
		onFrameStatusChangeCallbacks[i].clear();
	}

	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((myCond = SDL_CreateCond()) == NULL) {
		throw runtime_error("Failed creating condition!");
	}

	draining = false;
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
	YerFace_MutexUnlock(myMutex);

	SDL_WaitThread(herderThread, NULL);
	
	YerFace_MutexLock(myMutex);
	if(frameStore.size() > 0) {
		logger->warn("Frames are still sitting in the frame store! Draining did not complete!");
	}
	YerFace_MutexUnlock(myMutex);

	SDL_DestroyCond(myCond);
	SDL_DestroyMutex(myMutex);
	delete metrics;
	delete logger;
}

void FrameServer::onFrameServerDrainedEvent(FrameServerDrainedEventCallback callback) {
	YerFace_MutexLock(myMutex);
	onFrameServerDrainedCallbacks.push_back(callback);
	YerFace_MutexUnlock(myMutex);
}

void FrameServer::onFrameStatusChangeEvent(FrameStatusChangeEventCallback callback) {
	checkStatusValue(callback.newStatus);
	YerFace_MutexLock(myMutex);
	onFrameStatusChangeCallbacks[callback.newStatus].push_back(callback);
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

	if(frameStore.size() >= YERFACE_FRAMESERVER_MAX_QUEUEDEPTH) {
		logger->warn("FrameStore has hit the maximum allowable queue depth of %d! Main loop is now BLOCKED! If this happens a lot, consider some tuning.", YERFACE_FRAMESERVER_MAX_QUEUEDEPTH);
		while(frameStore.size() >= YERFACE_FRAMESERVER_MAX_QUEUEDEPTH) {
			YerFace_MutexUnlock(myMutex);
			SDL_Delay(5);
			YerFace_MutexLock(myMutex);
		}
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

	if(detectionBoundingBox > 0) {
		if(frameSize.width >= frameSize.height) {
			detectionScaleFactor = (double)detectionBoundingBox / (double)frameSize.width;
		} else {
			detectionScaleFactor = (double)detectionBoundingBox / (double)frameSize.height;
		}
	}
	workingFrame->detectionScaleFactor = detectionScaleFactor;

	resize(workingFrame->frame, workingFrame->detectionFrame, Size(), detectionScaleFactor, detectionScaleFactor);

	static bool reportedScale = false;
	if(!reportedScale) {
		logger->debug("Scaled current frame <%dx%d> down to <%dx%d> for detection", frameSize.width, frameSize.height, workingFrame->detectionFrame.size().width, workingFrame->detectionFrame.size().height);
		reportedScale = true;
	}

	// Set all of the registered checkpoints to FALSE to accurately record the frame's status.
	for(unsigned int i = 0; i <= FRAME_STATUS_MAX; i++) {
		for(string checkpointKey : statusCheckpoints[i]) {
			workingFrame->checkpoints[i][checkpointKey] = false;
		}
	}

	frameStore[workingFrame->frameTimestamps.frameNumber] = workingFrame;
	// logger->verbose("Inserted new working frame %ld into frame store. Frame store size is now %lu", workingFrame->frameTimestamps.frameNumber, frameStore.size());

	setFrameStatus(workingFrame->frameTimestamps, FRAME_STATUS_NEW);

	metrics->endClock(tick);

	SDL_CondSignal(myCond);
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
	SDL_CondSignal(myCond);
	YerFace_MutexUnlock(myMutex);
}

bool FrameServer::isDrained(void) {
	bool drained;
	YerFace_MutexLock(myMutex);
	drained = draining && frameStore.size() == 0;
	YerFace_MutexUnlock(myMutex);
	return drained;
}

void FrameServer::destroyFrame(FrameNumber frameNumber) {
	SDL_DestroyMutex(frameStore[frameNumber]->previewFrameMutex);
	// logger->verbose("Cleaning up GONE Frame #%lld ...", frameNumber);
	delete frameStore[frameNumber];
	frameStore.erase(frameNumber);
}

void FrameServer::setFrameStatus(FrameTimestamps frameTimestamps, WorkingFrameStatus newStatus) {
	checkStatusValue(newStatus);
	YerFace_MutexLock(myMutex);
	frameStore[frameTimestamps.frameNumber]->status = newStatus;
	// logger->verbose("Setting Frame #%lld Status to %d ...", frameNumber, newStatus);
	for(auto callback : onFrameStatusChangeCallbacks[newStatus]) {
		callback.callback(callback.userdata, newStatus, frameTimestamps);
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
	while(!self->isDrained()) {
		if(self->status->getIsPaused() && self->status->getIsRunning()) {
			YerFace_MutexUnlock(self->myMutex);
			SDL_Delay(100);
			YerFace_MutexLock(self->myMutex);
			continue;
		}

		bool didWork = false;
		std::list<FrameNumber> garbageFrames;
		// self->logger->verbose("Frame Herder Top-of-loop. FrameStore size: %lu", self->frameStore.size());
		for(auto framePair : self->frameStore) {
			FrameNumber frameNumber = framePair.first;
			WorkingFrame *workingFrame = framePair.second;
			WorkingFrameStatus status = workingFrame->status;

			//Does this frame need to be garbage collected?
			if(status == FRAME_STATUS_GONE) {
				didWork = true;
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
				didWork = true;
				self->setFrameStatus(workingFrame->frameTimestamps, (WorkingFrameStatus)(status + 1));
			}
		}

		//Destroy GONE frames.
		while(garbageFrames.size() > 0) {
			self->destroyFrame(garbageFrames.front());
			garbageFrames.pop_front();
		}

		//Sleep until needed.
		if(!didWork) {
			int result = SDL_CondWaitTimeout(self->myCond, self->myMutex, 1000);
			if(result < 0) {
				throw runtime_error("CondWaitTimeout() failed!");
			} else if(result == SDL_MUTEX_TIMEDOUT) {
				if(!self->status->getIsPaused()) {
					self->logger->warn("Timed out waiting for Condition signal!");
				}
			}
		}
	}

	//Drained.
	for(auto callback : self->onFrameServerDrainedCallbacks) {
		callback.callback(callback.userdata);
	}

	YerFace_MutexUnlock(self->myMutex);

	self->logger->verbose("Frame Herder Thread quitting...");
	return 0;
}

}; //namespace YerFace
