
#include "EventLogger.hpp"

using namespace std;

namespace YerFace {

EventLogger::EventLogger(json config, string myEventFile, OutputDriver *myOutputDriver, FrameDerivatives *myFrameDerivatives, double myFrom) {
	eventTimestampAdjustment = config["YerFace"]["EventLogger"]["eventTimestampAdjustment"];
	eventFilename = myEventFile;
	outputDriver = myOutputDriver;
	if(outputDriver == NULL) {
		throw invalid_argument("outputDriver cannot be NULL");
	}
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	from = myFrom;
	logger = new Logger("EventLogger");

	events = json::object();

	eventReplay = false;
	if(eventFilename.length() > 0) {
		eventFilestream.open(eventFilename, ifstream::in | ifstream::binary);
		if(eventFilestream.fail()) {
			throw invalid_argument("could not open inEvents for reading");
		}
		nextPacket = json::object();
		eventReplay = true;
	}

	logger->debug("EventLogger object constructed and ready to go!");
}

EventLogger::~EventLogger() {
	logger->debug("EventLogger object destructing...");
	delete logger;
}

void EventLogger::registerEventType(EventType eventType) {
	if(eventType.name.length() < 1) {
		throw invalid_argument("Event Type needs a name.");
	}
	for(EventType registered : registeredEventTypes) {
		if(eventType.name == registered.name) {
			throw invalid_argument("Event Types must have UNIQUE names");
		}
	}
	registeredEventTypes.push_back(eventType);
}

void EventLogger::logEvent(string eventName, json payload, bool propagate, json sourcePacket) {
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
		return;
	}
	bool insertEventInCompletedFrameData = true;
	if(propagate) {
		insertEventInCompletedFrameData = event.replayCallback(eventName, payload, sourcePacket);
	}
	if(insertEventInCompletedFrameData) {
		events[eventName] = payload;
	}
}

void EventLogger::startNewFrame(void) {
	if(eventReplay) {
		eventReplayHold = false;
		workingFrameTimestamps = frameDerivatives->getWorkingFrameTimestamps();

		processNextPacket();

		string line;
		while(!eventReplayHold && getline(eventFilestream, line)) {
			nextPacket = json::parse(line);
			processNextPacket();
		}
	}
}

void EventLogger::handleCompletedFrame(void) {
	if(!events.empty()) {
		outputDriver->insertCompletedFrameData("events", events);
		events = json::object();
	}
}

void EventLogger::processNextPacket(void) {
	if(nextPacket.size()) {
		double frameStart = workingFrameTimestamps.startTimestamp - eventTimestampAdjustment;
		double frameEnd = workingFrameTimestamps.estimatedEndTimestamp - eventTimestampAdjustment;
		double packetTime = nextPacket["meta"]["startTime"];
		if(from != -1.0 && from > 0.0) {
			packetTime = packetTime - from;
			nextPacket["meta"]["startTime"] = packetTime;
		}
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
				logEvent(iter.key(), iter.value(), true, nextPacket);
				nextPacket = json::object();
			}
		} else {
			eventReplayHold = true;
		}
	}
}

} //namespace YerFace
