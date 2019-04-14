#pragma once

#include "Logger.hpp"
#include "FrameServer.hpp"
#include "FaceTracker.hpp"
#include "MarkerTracker.hpp"
#include "SDLDriver.hpp"
#include "EventLogger.hpp"
#include "Utilities.hpp"
#include "Status.hpp"
#include "WorkerPool.hpp"

#include <set>

#include <fstream>

using namespace std;

namespace YerFace {

class EventLogger;

class OutputDriverWebSocketServer;

class OutputFrameContainer {
public:
	bool isReady(void);
	bool frameIsDraining;
	bool outputProcessed;
	FrameTimestamps frameTimestamps;
	json waitingOn;
	json frame;
};

class OutputDriver {
friend class OutputDriverWebSocketServer;

public:
	OutputDriver(json config, string myOutputFilename, Status *myStatus, FrameServer *myFrameServer, FaceTracker *myFaceTracker, SDLDriver *mySDLDriver);
	~OutputDriver() noexcept(false);
	void setEventLogger(EventLogger *myEventLogger);
	void registerFrameData(string key);
	void insertFrameData(string key, json value, FrameNumber frameNumber);
private:
	void handleNewBasisEvent(FrameNumber frameNumber);
	void handleOutputFrame(OutputFrameContainer *outputFrame);
	void outputNewFrame(json frame);
	static bool workerHandler(WorkerPoolWorker *worker);
	static void handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps);
	static void handleFrameServerDrainedEvent(void *userdata);

	string outputFilename;
	Status *status;
	FrameServer *frameServer;
	FaceTracker *faceTracker;
	SDLDriver *sdlDriver;
	EventLogger *eventLogger;
	Logger *logger;

	ofstream outputFilestream;

	OutputDriverWebSocketServer *webSocketServer;


	SDL_mutex *basisMutex;
	bool autoBasisTransmitted;
	json lastBasisFrame;

	WorkerPool *workerPool;
	SDL_mutex *workerMutex;
	list<string> lateFrameWaitOn;
	unordered_map<FrameNumber, OutputFrameContainer> pendingFrames;
	FrameTimestamps newestFrameTimestamps;
	bool frameServerDrained;
};

}; //namespace YerFace
