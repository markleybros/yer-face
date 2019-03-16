#pragma once

#include "Logger.hpp"
#include "OutputDriver.hpp"
#include "FrameServer.hpp"
#include "Utilities.hpp"

#include <list>

using namespace std;

namespace YerFace {

class OutputDriver;

class EventType {
public:
	string name;
	function<bool(string eventName, json eventPayload, json sourcePacket)> replayCallback;
};

class EventLogger {
public:
	EventLogger(json config, string myEventFile, OutputDriver *myOutputDriver, FrameServer *myFrameServer);
	~EventLogger();
	void registerEventType(EventType eventType);
	void logEvent(string eventName, json payload, FrameTimestamps frameTimestamps, bool propagate = false, json sourcePacket = json::object());
private:
	void processNextPacket(FrameTimestamps frameTimestamps);
	static void handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps);
	string eventFilename;
	OutputDriver *outputDriver;
	FrameServer *frameServer;

	Logger *logger;

	double eventTimestampAdjustment;
	ifstream eventFilestream;

	SDL_mutex *myMutex;
	list<EventType> registeredEventTypes;
	unordered_map<FrameNumber, json> frameEvents;
	bool eventReplay, eventReplayHold;
	json nextPacket;
};

}; //namespace YerFace
