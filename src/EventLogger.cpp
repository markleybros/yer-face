
#include "EventLogger.hpp"

using namespace std;

namespace YerFace {

EventLogger::EventLogger(json config, string myEventFile, double myEventFileStartSeconds, Status *myStatus, OutputDriver *myOutputDriver, FrameServer *myFrameServer) {
	replayWorkerPool = NULL;
	eventFilename = myEventFile;
	eventFileStartSeconds = myEventFileStartSeconds;
	if(eventFileStartSeconds < 0.0) {
		throw invalid_argument("you probably don't want your start seconds to be negative");
	}
	status = myStatus;
	if(status == NULL) {
		throw invalid_argument("status cannot be NULL");
	}
	outputDriver = myOutputDriver;
	if(outputDriver == NULL) {
		throw invalid_argument("outputDriver cannot be NULL");
	}
	frameServer = myFrameServer;
	if(frameServer == NULL) {
		throw invalid_argument("frameServer cannot be NULL");
	}
	logger = new Logger("EventLogger");

	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	outputDriver->registerFrameData("events");

	eventReplay = false;
	frameEvents.clear();
	pendingReplayFrames.clear();
	if(eventFilename.length() > 0) {
		eventFilestream.open(eventFilename, ifstream::in | ifstream::binary);
		if(eventFilestream.fail()) {
			throw invalid_argument("could not open inEvents for reading");
		}
		nextPacket = json::object();
		eventReplay = true;

		//We also want to introduce a checkpoint so that frames cannot TRANSITION AWAY from FRAME_STATUS_PREPROCESS without our blessing.
		frameServer->registerFrameStatusCheckpoint(FRAME_STATUS_PREPROCESS, "eventLogger.ran");

		WorkerPoolParameters workerPoolParameters;
		workerPoolParameters.name = "EventLogger.Replay";
		workerPoolParameters.numWorkers = 1;
		workerPoolParameters.numWorkersPerCPU = 0.0;
		workerPoolParameters.initializer = NULL;
		workerPoolParameters.deinitializer = NULL;
		workerPoolParameters.usrPtr = (void *)this;
		workerPoolParameters.handler = replayWorkerHandler;
		replayWorkerPool = new WorkerPool(config, status, frameServer, workerPoolParameters);
	}

	FrameStatusChangeEventCallback frameStatusChangeCallback;
	frameStatusChangeCallback.userdata = (void *)this;
	frameStatusChangeCallback.callback = handleFrameStatusChange;
	frameStatusChangeCallback.newStatus = FRAME_STATUS_NEW;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_PREPROCESS;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_LATE_PROCESSING;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_GONE;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);

	logger->debug1("EventLogger object constructed and ready to go!");
}

EventLogger::~EventLogger() noexcept(false) {
	logger->debug1("EventLogger object destructing...");

	if(replayWorkerPool) {
		delete replayWorkerPool;
	}

	YerFace_MutexLock(myMutex);
	if(pendingReplayFrames.size() > 0) {
		logger->err("Frames are still pending for replay! Woe is me!");
	}
	if(frameEvents.size() > 0) {
		logger->err("Frame events are still pending! Woe is me!");
	}
	YerFace_MutexUnlock(myMutex);

	SDL_DestroyMutex(myMutex);
	delete logger;
}

void EventLogger::registerEventType(EventType eventType) {
	if(eventType.name.length() < 1) {
		throw invalid_argument("Event Type needs a name.");
	}
	YerFace_MutexLock(myMutex);
	for(EventType registered : registeredEventTypes) {
		if(eventType.name == registered.name) {
			YerFace_MutexUnlock(myMutex);
			throw invalid_argument("Event Types must have UNIQUE names");
		}
	}
	registeredEventTypes.push_back(eventType);
	YerFace_MutexUnlock(myMutex);
}

void EventLogger::logEvent(string eventName, json payload, FrameTimestamps frameTimestamps, bool propagate, json sourcePacket) {
	// logger->info("Got logEvent() at frame #" YERFACE_FRAMENUMBER_FORMAT " of type [%s] with payload: %s", frameTimestamps.frameNumber, eventName.c_str(), payload.dump(-1, ' ', true).c_str());
	YerFace_MutexLock(myMutex);
	bool eventFound = false;
	EventType event;
	for(EventType registered : registeredEventTypes) {
		if(eventName == registered.name) {
			event = registered;
			eventFound = true;
			break;
		}
	}
	if(!eventFound) {
		logger->warning("Encountered unsupported event type [%s]! Are you using an old version of YerFace?", eventName.c_str());
		YerFace_MutexUnlock(myMutex);
		return;
	}
	auto frameEventsIter = frameEvents.find(frameTimestamps.frameNumber);
	if(frameEventsIter == frameEvents.end()) {
		YerFace_MutexUnlock(myMutex);
		throw invalid_argument("logEvent() called with bad frame number!");
	}
	bool insertEventInCompletedFrameData = true;
	if(propagate) {
		insertEventInCompletedFrameData = event.replayCallback(eventName, payload, sourcePacket);
	}
	if(insertEventInCompletedFrameData) {
		if(frameEvents[frameTimestamps.frameNumber].find(eventName) == frameEvents[frameTimestamps.frameNumber].end()) {
			frameEvents[frameTimestamps.frameNumber][eventName] = payload;
		} else {
			if(frameEvents[frameTimestamps.frameNumber][eventName].is_array() && payload.is_array()) {
				for(json eventElement : payload) {
					frameEvents[frameTimestamps.frameNumber][eventName].push_back(eventElement);
				}
			} else {
				throw logic_error("trying to propagate logged event data multiple times for the same frame, but not in a supported manner");
			}
		}
	}
	YerFace_MutexUnlock(myMutex);
}

void EventLogger::processNextPacket(FrameTimestamps frameTimestamps) {
	// logger->info("Got processNextPacket()");
	if(nextPacket.size()) {
		double frameDurationHalf = (frameTimestamps.estimatedEndTimestamp - frameTimestamps.startTimestamp) / 2.0;
		double frameStart = frameTimestamps.startTimestamp;
		double frameEnd = frameTimestamps.estimatedEndTimestamp - frameDurationHalf;
		double packetTime = nextPacket["meta"]["startTime"];
		packetTime -= eventFileStartSeconds;
		// if(nextPacket.find("events") != nextPacket.end()) {
		// 	logger->info("==== EVENT REPLAY ATTEMPT: [packetTime: %lf, currentFrameNumber: " YERFACE_FRAMENUMBER_FORMAT ", currentFrameStart: %lf, currentFrameEnd: %lf]; Candidate Packet Events: %s", packetTime, frameTimestamps.frameNumber, frameStart, frameEnd, nextPacket["events"].dump(-1, ' ', true).c_str());
		// }
		if(packetTime < frameEnd) {
			if(packetTime >= 0.0 && packetTime < (frameStart - frameDurationHalf)) {
				logger->err("EVENT REPLAY PACKET VERY LATE! Processing anyway... [packetTime: %lf, currentFrameStart: %lf, currentFrameEnd: %lf]", packetTime, frameStart, frameEnd);
			}

			// Based on timestamps, we've decided that this source event packet maps to "now" in the actual
			// input. Since we depend on ["meta"]["frameNumber"] downstream to tell us what frame to affect,
			// we should remap now.
			nextPacket["meta"]["frameNumber"] = frameTimestamps.frameNumber;

			if(!nextPacket.contains("events")) {
				nextPacket = json::object();
				return;
			}
			json event = nextPacket["events"];

			for(json::iterator iter = event.begin(); iter != event.end(); ++iter) {
				logEvent(iter.key(), iter.value(), frameTimestamps, true, nextPacket);
			}
			nextPacket = json::object();
		} else {
			// logger->info("==== EVENT REPLAY HOLDING...");
			eventReplayHold = true;
		}
	}
}

void EventLogger::handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps) {
	EventLogger *self = (EventLogger *)userdata;
	FrameNumber frameNumber = frameTimestamps.frameNumber;
	EventLoggerReplayTask replay;
	switch(newStatus) {
		default:
			throw logic_error("Handler passed unsupported frame status change event!");
		case FRAME_STATUS_NEW:
			YerFace_MutexLock(self->myMutex);
			self->frameEvents[frameNumber] = json::object();
			YerFace_MutexUnlock(self->myMutex);
			if(self->eventReplay) {
				replay.frameTimestamps = frameTimestamps;
				replay.readyForReplay = false;
				YerFace_MutexLock(self->myMutex);
				self->pendingReplayFrames[frameNumber] = replay;
				YerFace_MutexUnlock(self->myMutex);
			}
			break;
		case FRAME_STATUS_PREPROCESS:
			if(self->eventReplay) {
				YerFace_MutexLock(self->myMutex);
				self->pendingReplayFrames[frameNumber].readyForReplay = true;
				YerFace_MutexUnlock(self->myMutex);
				if(self->replayWorkerPool != NULL) {
					self->replayWorkerPool->sendWorkerSignal();
				}
			}
			break;
		case FRAME_STATUS_LATE_PROCESSING:
			YerFace_MutexLock(self->myMutex);
			self->outputDriver->insertFrameData("events", self->frameEvents[frameNumber], frameNumber);
			YerFace_MutexUnlock(self->myMutex);
			break;
		case FRAME_STATUS_GONE:
			YerFace_MutexLock(self->myMutex);
			self->frameEvents.erase(frameNumber);
			YerFace_MutexUnlock(self->myMutex);
			break;
	}
}

bool EventLogger::replayWorkerHandler(WorkerPoolWorker *worker) {
	EventLogger *self = (EventLogger *)worker->ptr;

	static FrameNumber lastFrameNumber = -1;
	bool didWork = false;
	FrameNumber myFrameNumber = -1;
	FrameTimestamps frameTimestamps;

	YerFace_MutexLock(self->myMutex);
	//// CHECK FOR WORK ////
	for(auto pendingFrameNumber : self->pendingReplayFrames) {
		if(myFrameNumber < 0 || pendingFrameNumber.first < myFrameNumber) {
			myFrameNumber = pendingFrameNumber.first;
		}
	}
	if(myFrameNumber > 0 && !self->pendingReplayFrames[myFrameNumber].readyForReplay) {
		// self->logger->verbose("BLOCKED on frame " YERFACE_FRAMENUMBER_FORMAT " because it is not ready!", myFrameNumber);
		myFrameNumber = -1;
	}
	if(myFrameNumber > 0) {
		frameTimestamps = self->pendingReplayFrames[myFrameNumber].frameTimestamps;
		self->pendingReplayFrames.erase(myFrameNumber);
	}
	YerFace_MutexUnlock(self->myMutex);

	//// DO THE WORK ////
	if(myFrameNumber > 0) {
		if(myFrameNumber <= lastFrameNumber) {
			throw logic_error("EventLogger handling frames out of order!");
		}
		lastFrameNumber = myFrameNumber;

		self->eventReplayHold = false;

		self->logger->debug4("EVENT REPLAY: Playing up to frame #" YERFACE_FRAMENUMBER_FORMAT " at time: %lf-%lf", frameTimestamps.frameNumber, frameTimestamps.startTimestamp, frameTimestamps.estimatedEndTimestamp);

		YerFace_MutexLock(self->myMutex);
		self->processNextPacket(frameTimestamps);
		string line;
		while(!self->eventReplayHold && getline(self->eventFilestream, line)) {
			self->nextPacket = json::parse(line);
			self->processNextPacket(frameTimestamps);
		}
		if(self->eventReplayHold) {
			self->logger->debug4("HOLDING...");
		}
		YerFace_MutexUnlock(self->myMutex);

		self->logger->debug4("DONE EVENT REPLAY: Finished frame #" YERFACE_FRAMENUMBER_FORMAT " at time: %lf-%lf", frameTimestamps.frameNumber, frameTimestamps.startTimestamp, frameTimestamps.estimatedEndTimestamp);

		self->frameServer->setWorkingFrameStatusCheckpoint(myFrameNumber, FRAME_STATUS_PREPROCESS, "eventLogger.ran");

		didWork = true;
	}

	return didWork;
}

} //namespace YerFace
