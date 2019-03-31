
#include "OutputDriver.hpp"

#include <string>
#include <iostream>
#include <cstring>
#include <streambuf>

using namespace cv;

using namespace websocketpp;
using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

namespace YerFace {

bool OutputFrameContainer::isReady(void) {
	if(!frameIsDraining) {
		return false;
	}
	for(auto& waiting : waitingOn.items()) {
		// fprintf(stderr, "FRAME WAITING ON %s?\n", waiting.key().c_str());
		if(waiting.value()) {
			return false;
		}
	}
	return true;
}

OutputDriver::OutputDriver(json config, String myOutputFilename, Status *myStatus, FrameServer *myFrameServer, FaceTracker *myFaceTracker, SDLDriver *mySDLDriver) {
	serverThread = NULL;
	outputFilename = myOutputFilename;
	newestFrameTimestamps.frameNumber = -1;
	newestFrameTimestamps.startTimestamp = -1.0;
	newestFrameTimestamps.estimatedEndTimestamp = -1.0;
	status = myStatus;
	if(status == NULL) {
		throw invalid_argument("status cannot be NULL");
	}
	frameServer = myFrameServer;
	if(frameServer == NULL) {
		throw invalid_argument("frameServer cannot be NULL");
	}
	faceTracker = myFaceTracker;
	if(faceTracker == NULL) {
		throw invalid_argument("faceTracker cannot be NULL");
	}
	sdlDriver = mySDLDriver;
	if(sdlDriver == NULL) {
		throw invalid_argument("sdlDriver cannot be NULL");
	}
	eventLogger = NULL;
	if((basisMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((websocketMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((workerMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	websocketServerPort = config["YerFace"]["OutputDriver"]["websocketServerPort"];
	if(websocketServerPort < 1 || websocketServerPort > 65535) {
		throw runtime_error("Server port is invalid");
	}
	websocketServerEnabled = config["YerFace"]["OutputDriver"]["websocketServerEnabled"];

	//We need to know when the frame server has drained.
	frameServerDrained = false;
	FrameServerDrainedEventCallback frameServerDrainedCallback;
	frameServerDrainedCallback.userdata = (void *)this;
	frameServerDrainedCallback.callback = handleFrameServerDrainedEvent;
	frameServer->onFrameServerDrainedEvent(frameServerDrainedCallback);

	autoBasisTransmitted = false;
	sdlDriver->onBasisFlagEvent([this] (void) -> void {
		YerFace_MutexLock(this->workerMutex);
		bool eventHandled = false;
		if(this->newestFrameTimestamps.frameNumber > 0 && !this->frameServerDrained) {
			FrameNumber frameNumber = this->newestFrameTimestamps.frameNumber;
			if(this->pendingFrames.find(frameNumber) != this->pendingFrames.end()) {
				if(!this->pendingFrames[frameNumber].frameIsDraining) {
					if(this->eventLogger != NULL) {
						this->eventLogger->logEvent("basis", (json)true, this->newestFrameTimestamps);
					}
					this->handleNewBasisEvent(frameNumber);
					eventHandled = true;
				}
			}
		}
		if(!eventHandled) {
			this->logger->warn("Discarding user basis event because frame status is already drained. (Or similar bad state.)");
		}
		YerFace_MutexUnlock(this->workerMutex);
	});
	logger = new Logger("OutputDriver");

	//Constrain websocket server logs a bit for sanity.
	server.get_alog().clear_channels(log::alevel::all);
	server.get_alog().set_channels(log::alevel::connect | log::alevel::disconnect | log::alevel::app | log::alevel::http | log::alevel::fail);
	server.get_elog().clear_channels(log::elevel::all);
	server.get_elog().set_channels(log::elevel::info | log::elevel::warn | log::elevel::rerror | log::elevel::fatal);

	if(websocketServerEnabled) {
		websocketServerRunning = true;
		//Create worker thread.
		if((serverThread = SDL_CreateThread(OutputDriver::launchWebSocketServer, "HTTPServer", (void *)this)) == NULL) {
			throw runtime_error("Failed spawning worker thread!");
		}
	}

	if(outputFilename.length() > 0) {
		outputFilestream.open(outputFilename, ofstream::out | ofstream::binary | ofstream::trunc);
		if(outputFilestream.fail()) {
			throw invalid_argument("could not open outputFile for writing");
		}
	}

	//We want to know when any frame has entered various statuses.
	FrameStatusChangeEventCallback frameStatusChangeCallback;
	frameStatusChangeCallback.userdata = (void *)this;
	frameStatusChangeCallback.callback = handleFrameStatusChange;
	frameStatusChangeCallback.newStatus = FRAME_STATUS_NEW;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_DRAINING;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_GONE;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);

	//We also want to introduce a checkpoint so that frames cannot TRANSITION AWAY from FRAME_STATUS_DRAINING without our blessing.
	frameServer->registerFrameStatusCheckpoint(FRAME_STATUS_DRAINING, "outputDriver.ran");

	WorkerPoolParameters workerPoolParameters;
	workerPoolParameters.name = "OutputDriver";
	workerPoolParameters.numWorkers = 1;
	workerPoolParameters.numWorkersPerCPU = 0.0;
	workerPoolParameters.initializer = NULL;
	workerPoolParameters.deinitializer = NULL;
	workerPoolParameters.usrPtr = (void *)this;
	workerPoolParameters.handler = workerHandler;
	workerPool = new WorkerPool(config, status, frameServer, workerPoolParameters);

	logger->debug("OutputDriver object constructed and ready to go!");
};

OutputDriver::~OutputDriver() noexcept(false) {
	logger->debug("OutputDriver object destructing...");

	delete workerPool;

	YerFace_MutexLock(workerMutex);
	if(pendingFrames.size() > 0) {
		logger->error("Frames are still pending! Woe is me!");
	}
	YerFace_MutexUnlock(workerMutex);

	if(websocketServerEnabled && serverThread) {
		YerFace_MutexLock(websocketMutex);
		websocketServerRunning = false;
		YerFace_MutexUnlock(websocketMutex);
		SDL_WaitThread(serverThread, NULL);
	}

	SDL_DestroyMutex(basisMutex);
	SDL_DestroyMutex(websocketMutex);
	SDL_DestroyMutex(workerMutex);

	if(outputFilename.length() > 0 && outputFilestream.is_open()) {
		outputFilestream.close();
	}

	delete logger;
}

void OutputDriver::setEventLogger(EventLogger *myEventLogger) {
	eventLogger = myEventLogger;
	if(eventLogger == NULL) {
		throw invalid_argument("eventLogger cannot be NULL");
	}

	EventType basisEvent;
	basisEvent.name = "basis";
	basisEvent.replayCallback = [this] (string eventName, json eventPayload, json sourcePacket) -> bool {
		if(eventName != "basis" || (bool)eventPayload != true) {
			this->logger->warn("Got an unsupported replay event!");
			return false;
		}
		this->logger->verbose("Received replayed Basis Flag event. Rebroadcasting...");
		if((double)sourcePacket["meta"]["startTime"] < 0.0 || (FrameNumber)sourcePacket["meta"]["frameNumber"] < 0) {
			sourcePacket["meta"]["frameNumber"] = -1;
			sourcePacket["meta"]["startTime"] = -1.0;
			YerFace_MutexLock(this->basisMutex);
			this->autoBasisTransmitted = true;
			YerFace_MutexUnlock(this->basisMutex);
			outputNewFrame(sourcePacket);
			return false;
		}
		FrameNumber frameNumber = (FrameNumber)sourcePacket["meta"]["frameNumber"];
		handleNewBasisEvent(frameNumber);
		return true;
	};
	eventLogger->registerEventType(basisEvent);
}

void OutputDriver::handleNewBasisEvent(FrameNumber frameNumber) {
	logger->verbose("Got a Basis Flag event for Frame #%lu. Handling...", frameNumber);
	YerFace_MutexLock(workerMutex);
	pendingFrames[frameNumber].frame["meta"]["basis"] = true;
	YerFace_MutexUnlock(workerMutex);
}

int OutputDriver::launchWebSocketServer(void *data) {
	OutputDriver *self = (OutputDriver *)data;
	self->logger->verbose("WebSocket Server Thread Alive!");

	self->server.init_asio();
	self->server.set_reuse_addr(true);
	self->server.set_open_handler(bind(&OutputDriver::serverOnOpen,self,::_1));
	self->server.set_close_handler(bind(&OutputDriver::serverOnClose,self,::_1));
	self->serverSetQuitPollTimer();

	self->server.listen(self->websocketServerPort);
	self->server.start_accept();
	self->server.run();

	self->logger->verbose("WebSocket Server Thread Terminating.");
	return 0;
}

void OutputDriver::serverOnOpen(websocketpp::connection_hdl handle) {
	YerFace_MutexLock(this->basisMutex);
	string jsonString = lastBasisFrame.dump(-1, ' ', true);
	YerFace_MutexUnlock(this->basisMutex);

	YerFace_MutexLock(this->websocketMutex);
	logger->verbose("WebSocket Connection Opened.");
	server.send(handle, jsonString, websocketpp::frame::opcode::text);
	connectionList.insert(handle);
	YerFace_MutexUnlock(this->websocketMutex);
}

void OutputDriver::serverOnClose(websocketpp::connection_hdl handle) {
	YerFace_MutexLock(this->websocketMutex);
	connectionList.erase(handle);
	logger->verbose("WebSocket Connection Closed.");
	YerFace_MutexUnlock(this->websocketMutex);
}

void OutputDriver::serverOnTimer(websocketpp::lib::error_code const &ec) {
	if(ec) {
		logger->error("WebSocket Library Reported an Error: %s", ec.message().c_str());
		throw runtime_error("WebSocket server error!");
	}
	bool continueTimer = true;
	YerFace_MutexLock(websocketMutex);
	if(!websocketServerRunning) {
		server.stop();
		continueTimer = false;
	}
	YerFace_MutexUnlock(websocketMutex);
	if(continueTimer) {
		serverSetQuitPollTimer();
	}
}

void OutputDriver::handleOutputFrame(OutputFrameContainer *outputFrame) {
	// outputFrame->frame["meta"] = json::object();
	outputFrame->frame["meta"]["frameNumber"] = outputFrame->frameTimestamps.frameNumber;
	outputFrame->frame["meta"]["startTime"] = outputFrame->frameTimestamps.startTimestamp;
	if(outputFrame->frame["meta"].find("basis") == outputFrame->frame["meta"].end()) {
		//Default basis unless set earlier.
		outputFrame->frame["meta"]["basis"] = false;
	}

	bool allPropsSet = true;
	FacialPose facialPose = faceTracker->getFacialPose(outputFrame->frameTimestamps.frameNumber);
	if(facialPose.set) {
		outputFrame->frame["pose"] = json::object();
		Vec3d angles = Utilities::rotationMatrixToEulerAngles(facialPose.rotationMatrix);
		outputFrame->frame["pose"]["rotation"] = { {"x", angles[0]}, {"y", angles[1]}, {"z", angles[2]} };
		outputFrame->frame["pose"]["translation"] = { {"x", facialPose.translationVector.at<double>(0)}, {"y", facialPose.translationVector.at<double>(1)}, {"z", facialPose.translationVector.at<double>(2)} };
	} else {
		allPropsSet = false;
	}

	json trackers;
	auto markerTrackers = MarkerTracker::getMarkerTrackers();
	for(auto markerTracker : markerTrackers) {
		MarkerPoint markerPoint = markerTracker->getMarkerPoint(outputFrame->frameTimestamps.frameNumber);
		if(markerPoint.set) {
			string trackerName = markerTracker->getMarkerType().toString();
			trackers[trackerName.c_str()]["position"] = { {"x", markerPoint.point3d.x}, {"y", markerPoint.point3d.y}, {"z", markerPoint.point3d.z} };
		} else {
			allPropsSet = false;
		}
	}
	if(trackers.size()) {
		outputFrame->frame["trackers"] = trackers;
	}

	YerFace_MutexLock(this->basisMutex);
	if(allPropsSet && !autoBasisTransmitted) {
		autoBasisTransmitted = true;
		outputFrame->frame["meta"]["basis"] = true;
		logger->info("All properties set. Transmitting initial basis flag automatically.");
	}
	if((bool)outputFrame->frame["meta"]["basis"]) {
		autoBasisTransmitted = true;
		logger->info("Transmitting basis flag.");
	}
	YerFace_MutexUnlock(this->basisMutex);
	
	outputNewFrame(outputFrame->frame);
}

void OutputDriver::registerFrameData(string key) {
	YerFace_MutexLock(workerMutex);
	lateFrameWaitOn.push_back(key);
	YerFace_MutexUnlock(workerMutex);
}

void OutputDriver::insertFrameData(string key, json value, FrameNumber frameNumber) {
	YerFace_MutexLock(workerMutex);
	if(pendingFrames.find(frameNumber) == pendingFrames.end()) {
		throw runtime_error("Somebody is trying to insert frame data into a frame number which does not exist!");
	}
	if(pendingFrames[frameNumber].waitingOn.find(key) == pendingFrames[frameNumber].waitingOn.end()) {
		throw runtime_error("Somebody is trying to insert frame data which was not previously registered!");
	}
	if(pendingFrames[frameNumber].waitingOn[key] != true) {
		throw runtime_error("Somebody is trying to insert frame data which was already inserted!");
	}
	pendingFrames[frameNumber].frame[key] = value;
	pendingFrames[frameNumber].waitingOn[key] = false;
	YerFace_MutexUnlock(workerMutex);
}

void OutputDriver::serverSetQuitPollTimer(void) {
	server.set_timer(100, websocketpp::lib::bind(&OutputDriver::serverOnTimer,this,::_1));
}

void OutputDriver::outputNewFrame(json frame) {
	if(frame["meta"]["basis"]) {
		YerFace_MutexLock(this->basisMutex);
		lastBasisFrame = frame;
		YerFace_MutexUnlock(this->basisMutex);
	}

	std::string jsonString;
	jsonString = frame.dump(-1, ' ', true);

	YerFace_MutexLock(this->websocketMutex);
	try {
		for(auto handle : connectionList) {
			server.send(handle, jsonString, websocketpp::frame::opcode::text);
		}
	} catch (websocketpp::exception const &e) {
		logger->warn("Got a websocket exception: %s", e.what());
	}
	YerFace_MutexUnlock(this->websocketMutex);

	if(outputFilename.length() > 0) {
		outputFilestream << jsonString << "\n";
	}
}

bool OutputDriver::workerHandler(WorkerPoolWorker *worker) {
	OutputDriver *self = (OutputDriver *)worker->ptr;

	static FrameNumber lastFrameNumber = -1;
	bool didWork = false;
	OutputFrameContainer *outputFrame = NULL;
	FrameNumber myFrameNumber = -1;

	YerFace_MutexLock(self->workerMutex);
	//// CHECK FOR WORK ////
	for(auto pendingFramePair : self->pendingFrames) {
		if(myFrameNumber < 0 || pendingFramePair.first < myFrameNumber) {
			if(!self->pendingFrames[pendingFramePair.first].outputProcessed) {
				myFrameNumber = pendingFramePair.first;
			}
		}
	}
	if(myFrameNumber > 0) {
		outputFrame = &self->pendingFrames[myFrameNumber];
	}
	if(outputFrame != NULL && !outputFrame->isReady()) {
		// self->logger->verbose("BLOCKED on frame " YERFACE_FRAMENUMBER_FORMAT " because it is not ready!", myFrameNumber);
		myFrameNumber = -1;
		outputFrame = NULL;
	}
	YerFace_MutexUnlock(self->workerMutex);

	//// DO THE WORK ////
	if(outputFrame != NULL) {
		// self->logger->verbose("Output Worker Thread handling frame #" YERFACE_FRAMENUMBER_FORMAT, outputFrame->frameTimestamps.frameNumber);

		if(outputFrame->frameTimestamps.frameNumber <= lastFrameNumber) {
			throw logic_error("OutputDriver handling frames out of order!");
		}
		lastFrameNumber = outputFrame->frameTimestamps.frameNumber;

		self->handleOutputFrame(outputFrame);

		YerFace_MutexLock(self->workerMutex);
		outputFrame->outputProcessed = true;
		YerFace_MutexUnlock(self->workerMutex);

		self->frameServer->setWorkingFrameStatusCheckpoint(outputFrame->frameTimestamps.frameNumber, FRAME_STATUS_DRAINING, "outputDriver.ran");

		didWork = true;
	}
	return didWork;
}

void OutputDriver::handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps) {
	FrameNumber frameNumber = frameTimestamps.frameNumber;
	OutputDriver *self = (OutputDriver *)userdata;
	static OutputFrameContainer newOutputFrame;
	switch(newStatus) {
		default:
			throw logic_error("Handler passed unsupported frame status change event!");
		case FRAME_STATUS_NEW:
			// self->logger->verbose("handleFrameStatusChange() Frame #" YERFACE_FRAMENUMBER_FORMAT " appearing as new! Queue depth is now %lu", frameNumber, self->pendingFrames.size());
			newOutputFrame.frame = json::object();
			newOutputFrame.outputProcessed = false;
			newOutputFrame.frameIsDraining = false;
			newOutputFrame.frameTimestamps = frameTimestamps;
			newOutputFrame.frame = json::object();
			YerFace_MutexLock(self->workerMutex);
			newOutputFrame.waitingOn = json::object();
			for(string waitOn : self->lateFrameWaitOn) {
				// self->logger->verbose("WAITING ON: %s", waitOn.c_str());
				newOutputFrame.waitingOn[waitOn] = true;
			}
			self->pendingFrames[frameNumber] = newOutputFrame;
			self->newestFrameTimestamps = frameTimestamps;
			YerFace_MutexUnlock(self->workerMutex);
			break;
		case FRAME_STATUS_DRAINING:
			YerFace_MutexLock(self->workerMutex);
			// self->logger->verbose("handleFrameStatusChange() Frame #" YERFACE_FRAMENUMBER_FORMAT " waiting on me. Queue depth is now %lu", frameNumber, self->pendingFrames.size());
			self->pendingFrames[frameNumber].frameIsDraining = true;
			YerFace_MutexUnlock(self->workerMutex);
			self->workerPool->sendWorkerSignal();
			break;
		case FRAME_STATUS_GONE:
			YerFace_MutexLock(self->workerMutex);
			if(!self->pendingFrames[frameNumber].outputProcessed) {
				throw logic_error("Frame is gone, but not yet processed!");
			}
			self->pendingFrames.erase(frameNumber);
			YerFace_MutexUnlock(self->workerMutex);
			break;
	}
}

void OutputDriver::handleFrameServerDrainedEvent(void *userdata) {
	OutputDriver *self = (OutputDriver *)userdata;
	// self->logger->verbose("Got notification that FrameServer has drained!");
	YerFace_MutexLock(self->workerMutex);
	self->frameServerDrained = true;
	YerFace_MutexUnlock(self->workerMutex);
}

}; //namespace YerFace
