#pragma once

#include "Logger.hpp"
#include "OutputDriver.hpp"
#include "FrameDerivatives.hpp"
#include "Utilities.hpp"

#include <list>
#include <fstream>

using namespace std;

namespace YerFace {

class OutputDriver;

class EventType {
public:
	string name;
	function<void(string eventName, json eventPayload)> replayCallback;
};

class EventLogger {
public:
	EventLogger(json config, string myEventFile, OutputDriver *myOutputDriver, FrameDerivatives *myFrameDerivatives);
	~EventLogger();
	void registerEventType(EventType eventType);
	void logEvent(string eventName, json payload, bool propagate = false);
	void startNewFrame(void);
	void handleCompletedFrame(void);
private:
	void processNextPacket(void);
	string eventFilename;
	OutputDriver *outputDriver;
	FrameDerivatives *frameDerivatives;
	Logger *logger;

	FrameTimestamps workingFrameTimestamps;

	double eventTimestampAdjustment;
	bool eventReplay, eventReplayHold;
	ifstream eventFilestream;
	json nextPacket;

	list<EventType> registeredEventTypes;

	json events;
};

}; //namespace YerFace
