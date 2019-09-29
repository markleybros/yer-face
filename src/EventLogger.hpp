#pragma once

#include "Logger.hpp"
#include "OutputDriver.hpp"
#include "FrameServer.hpp"
#include "Utilities.hpp"
#include "Status.hpp"
#include "WorkerPool.hpp"

#include <list>

using namespace std;

namespace YerFace {

class OutputDriver;

class EventType {
public:
	string name;
	function<bool(string eventName, json eventPayload, json sourcePacket)> replayCallback;
};

class EventLoggerReplayTask {
public:
	FrameTimestamps frameTimestamps;
	bool readyForReplay;
};

class EventLogger {
public:
	EventLogger(json config, string myEventFile, double myEventFileStartSeconds, Status *myStatus, OutputDriver *myOutputDriver, FrameServer *myFrameServer);
	~EventLogger() noexcept(false);
	void registerEventType(EventType eventType);
	void logEvent(string eventName, json payload, FrameTimestamps frameTimestamps, bool propagate = false, json sourcePacket = json::object());
private:
	void processNextPacket(FrameTimestamps frameTimestamps);
	static void handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps);
	static bool replayWorkerHandler(WorkerPoolWorker *worker);
	string eventFilename;
	double eventFileStartSeconds;
	Status *status;
	OutputDriver *outputDriver;
	FrameServer *frameServer;

	Logger *logger;

	ifstream eventFilestream;

	WorkerPool *replayWorkerPool;
	SDL_mutex *myMutex;
	list<EventType> registeredEventTypes;
	unordered_map<FrameNumber, json> frameEvents;
	unordered_map<FrameNumber, EventLoggerReplayTask> pendingReplayFrames;
	bool eventReplay, eventReplayHold;
	json nextPacket;
};

}; //namespace YerFace
