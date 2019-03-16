
#include "EventLogger.hpp"

using namespace std;

namespace YerFace {

EventLogger::EventLogger(json config, string myEventFile, OutputDriver *myOutputDriver, FrameServer *myFrameServer) {
	eventTimestampAdjustment = config["YerFace"]["EventLogger"]["eventTimestampAdjustment"];
	eventFilename = myEventFile;
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

	eventReplay = false;
	if(eventFilename.length() > 0) {
		eventFilestream.open(eventFilename, ifstream::in | ifstream::binary);
		if(eventFilestream.fail()) {
			throw invalid_argument("could not open inEvents for reading");
		}
		nextPacket = json::object();
		eventReplay = true;
	}

	FrameStatusChangeEventCallback frameStatusChangeCallback;
	frameStatusChangeCallback.userdata = (void *)this;
	frameStatusChangeCallback.callback = handleFrameStatusChange;
	frameStatusChangeCallback.newStatus = FRAME_STATUS_NEW;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_DRAINING;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_GONE;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);

	logger->debug("EventLogger object constructed and ready to go!");
}

EventLogger::~EventLogger() {
	logger->debug("EventLogger object destructing...");
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
		logger->warn("Encountered unsupported event type [%s]! Are you using an old version of YerFace?", eventName.c_str());
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
		frameEvents[frameTimestamps.frameNumber] = payload;
	}
	YerFace_MutexUnlock(myMutex);
}

void EventLogger::processNextPacket(FrameTimestamps frameTimestamps) {
	if(nextPacket.size()) {
		double frameStart = frameTimestamps.startTimestamp - eventTimestampAdjustment;
		double frameEnd = frameTimestamps.estimatedEndTimestamp - eventTimestampAdjustment;
		double packetTime = nextPacket["meta"]["startTime"];
		if(packetTime < frameEnd) {
			if(packetTime >= 0.0 && packetTime < frameStart) {
				logger->warn("==== EVENT REPLAY PACKET LATE! Processing anyway... ====");
			}
			json event;
			try {
				event = nextPacket.at("events");
			} catch(nlohmann::detail::out_of_range &e) {
				return;
			}

			for(json::iterator iter = event.begin(); iter != event.end(); ++iter) {
				logEvent(iter.key(), iter.value(), frameTimestamps, true, nextPacket);
			}
			nextPacket = json::object();
		} else {
			eventReplayHold = true;
		}
	}
}

void EventLogger::handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps) {
	EventLogger *self = (EventLogger *)userdata;
	FrameNumber frameNumber = frameTimestamps.frameNumber;
	switch(newStatus) {
		default:
			throw logic_error("Handler passed unsupported frame status change event!");
		case FRAME_STATUS_NEW:
			YerFace_MutexLock(self->myMutex);
			self->frameEvents[frameNumber] = json::object();
			if(self->eventReplay) {
				self->eventReplayHold = false;

				self->processNextPacket(frameTimestamps);

				string line;
				while(!self->eventReplayHold && getline(self->eventFilestream, line)) {
					self->nextPacket = json::parse(line);
					self->processNextPacket(frameTimestamps);
				}
			}
			YerFace_MutexUnlock(self->myMutex);
			break;
		case FRAME_STATUS_DRAINING:
			YerFace_MutexLock(self->myMutex);
			// self->outputDriver->insertCompletedFrameData("events", self->frameEvents[frameNumber]); //FIXME - event lifecycle
			self->frameEvents.erase(frameNumber);
			YerFace_MutexUnlock(self->myMutex);
			break;
		case FRAME_STATUS_GONE:
			//FIXME
			break;
	}
}

} //namespace YerFace
