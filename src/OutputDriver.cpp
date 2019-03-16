
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
	for(auto waitingForMe : waitingOn) {
		if(waitingForMe) {
			return false;
		}
	}
	return true;
}

OutputDriver::OutputDriver(json config, String myOutputFilename, Status *myStatus, FrameServer *myFrameServer, FaceTracker *myFaceTracker, SDLDriver *mySDLDriver) {
	serverThread = NULL;
	outputFilename = myOutputFilename;
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
	if((streamFlagsMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((connectionListMutex = SDL_CreateMutex()) == NULL) {
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

	autoBasisTransmitted = false;
	basisFlagged = false;
	// FIXME - event lifecycle
	// sdlDriver->onBasisFlagEvent([this] (void) -> void {
	// 	if(this->eventLogger != NULL) {
	// 		this->eventLogger->logEvent("basis", (json)true);
	// 	}
	// 	this->handleNewBasisEvent();
	// });
	logger = new Logger("OutputDriver");

	//Constrain websocket server logs a bit for sanity.
	server.get_alog().clear_channels(log::alevel::all);
	server.get_alog().set_channels(log::alevel::connect | log::alevel::disconnect | log::alevel::app | log::alevel::http | log::alevel::fail);
	server.get_elog().clear_channels(log::elevel::all);
	server.get_elog().set_channels(log::elevel::info | log::elevel::warn | log::elevel::rerror | log::elevel::fatal);

	if(websocketServerEnabled) {
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
		SDL_WaitThread(serverThread, NULL);
	}

	SDL_DestroyMutex(streamFlagsMutex);
	SDL_DestroyMutex(connectionListMutex);
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

	// FIXME - event lifecycle
	// EventType basisEvent;
	// basisEvent.name = "basis";
	// basisEvent.replayCallback = [this] (string eventName, json eventPayload, json sourcePacket) -> bool {
	// 	if(eventName != "basis" || (bool)eventPayload != true) {
	// 		this->logger->warn("Got an unsupported replay event!");
	// 		return false;
	// 	}
	// 	return this->handleReplayBasisEvent(sourcePacket);
	// };
	// eventLogger->registerEventType(basisEvent);
}

// FIXME - event lifecycle
// void OutputDriver::handleNewBasisEvent(bool generatedByUserInput) {
// 	this->logger->verbose("Got a Basis Flag event. Handling...");
// 	YerFace_MutexLock(streamFlagsMutex);
// 	basisFlagged = true;
// 	YerFace_MutexUnlock(streamFlagsMutex);
// }

// FIXME - event lifecycle
// bool OutputDriver::handleReplayBasisEvent(json sourcePacket) {
// 	this->logger->verbose("Received replayed Basis Flag event. Rebroadcasting...");
// 	if((double)sourcePacket["meta"]["startTime"] < 0.0) {
// 		sourcePacket["meta"]["frameNumber"] = -1;
// 		sourcePacket["meta"]["startTime"] = 0.0;
// 		YerFace_MutexLock(streamFlagsMutex);
// 		autoBasisTransmitted = true;
// 		outputNewFrame(sourcePacket, true);
// 		YerFace_MutexUnlock(streamFlagsMutex);
// 		return false;
// 	}
// 	handleNewBasisEvent(false);
// 	return true;
// }

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
	YerFace_MutexLock(this->streamFlagsMutex);
	std::stringstream jsonString;
	jsonString << lastBasisFrame.dump(-1, ' ', true);
	YerFace_MutexUnlock(this->streamFlagsMutex);

	YerFace_MutexLock(this->connectionListMutex);
	logger->verbose("WebSocket Connection Opened: 0x%X", handle);
	server.send(handle, jsonString.str(), websocketpp::frame::opcode::text);
	connectionList.insert(handle);
	YerFace_MutexUnlock(this->connectionListMutex);
}

void OutputDriver::serverOnClose(websocketpp::connection_hdl handle) {
	YerFace_MutexLock(this->connectionListMutex);
	connectionList.erase(handle);
	logger->verbose("WebSocket Connection Closed: 0x%X", handle);
	YerFace_MutexUnlock(this->connectionListMutex);
}

void OutputDriver::serverOnTimer(websocketpp::lib::error_code const &ec) {
	if(ec) {
		logger->error("WebSocket Library Reported an Error: %s", ec.message().c_str());
		status->setIsRunning(false);
		server.stop();
		return;
	}
	if(!status->getIsRunning()) {
		server.stop();
	}
	serverSetQuitPollTimer();
}

void OutputDriver::handleOutputFrame(OutputFrameContainer *outputFrame) {
	outputFrame->frame["meta"] = json::object();
	outputFrame->frame["meta"]["frameNumber"] = outputFrame->frameTimestamps.frameNumber;
	outputFrame->frame["meta"]["startTime"] = outputFrame->frameTimestamps.startTimestamp;
	outputFrame->frame["meta"]["basis"] = false; //Default to no basis. Will override below as necessary.

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

	if(allPropsSet && !autoBasisTransmitted) {
		autoBasisTransmitted = true;
		outputFrame->frame["meta"]["basis"] = true;
		logger->info("All properties set. Transmitting initial basis flag automatically.");
	}

	YerFace_MutexLock(this->streamFlagsMutex);
	if(basisFlagged) {
		autoBasisTransmitted = true;
		basisFlagged = false;
		outputFrame->frame["meta"]["basis"] = true;
		logger->info("Transmitting basis flag based on received basis event.");
	}
	outputNewFrame(outputFrame->frame);
	YerFace_MutexUnlock(this->streamFlagsMutex);
}

void OutputDriver::registerLateFrameData(string key) {
	lateFrameWaitOn.push_back(key);
}

// void OutputDriver::updateLateFrameData(FrameNumber frameNumber, string key, json value) {
// 	if(!writerThread) {
// 		return;
// 	}
// 	YerFace_MutexLock(outputBufMutex);
// 	bool success = false;
// 	for(auto iter = outputBuf.begin(); iter != outputBuf.end(); ++iter) {
// 		if((FrameNumber)iter->frame["meta"]["frameNumber"] == frameNumber) {
// 			iter->frame[key] = value;
// 			iter->waitingOn[key] = false;
// 			success = true;
// 		}
// 	}
// 	if(!success) {
// 		throw runtime_error("could not update desired frame! buffer slippage or something goofy is going on");
// 	}
// 	YerFace_MutexUnlock(outputBufMutex);
// }

// void OutputDriver::insertCompletedFrameData(string key, json value) {
// 	extraFrameData[key] = value;
// }

void OutputDriver::serverSetQuitPollTimer(void) {
	server.set_timer(100, websocketpp::lib::bind(&OutputDriver::serverOnTimer,this,::_1));
}

void OutputDriver::outputNewFrame(json frame, bool replay) {
	YerFace_MutexLock(this->streamFlagsMutex);
	if(frame["meta"]["basis"]) {
		lastBasisFrame = frame;
	}
	YerFace_MutexUnlock(this->streamFlagsMutex);

	std::stringstream jsonString;
	jsonString << frame.dump(-1, ' ', true);

	YerFace_MutexLock(this->connectionListMutex);
	try {
		for(auto handle : connectionList) {
			server.send(handle, jsonString.str(), websocketpp::frame::opcode::text);
		}
	} catch (websocketpp::exception const &e) {
		logger->warn("Got a websocket exception: %s", e.what());
	}
	YerFace_MutexUnlock(this->connectionListMutex);
}

bool OutputDriver::workerHandler(WorkerPoolWorker *worker) {
	OutputDriver *self = (OutputDriver *)worker->ptr;

	bool didWork = false;
	OutputFrameContainer *outputFrame = NULL;

	YerFace_MutexLock(self->workerMutex);
	//// CHECK FOR WORK ////
	for(auto pendingFramePair : self->pendingFrames) {
		if(outputFrame == NULL || pendingFramePair.first < outputFrame->frameTimestamps.frameNumber) {
			if(self->pendingFrames[pendingFramePair.first].isReady() && !self->pendingFrames[pendingFramePair.first].outputProcessed) {
				outputFrame = &self->pendingFrames[pendingFramePair.first];
			}
		}
	}
	YerFace_MutexUnlock(self->workerMutex);

	//// DO THE WORK ////
	if(outputFrame != NULL) {
		// self->logger->verbose("Output Worker Thread handling frame #%lld", outputFrame->frameTimestamps.frameNumber);
		self->handleOutputFrame(outputFrame);

		if(self->outputFilename.length() > 0) {
			self->outputFilestream << outputFrame->frame.dump(-1, ' ', true) << "\n";
		}

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
			newOutputFrame.frame = json::object();
			newOutputFrame.outputProcessed = false;
			newOutputFrame.frameIsDraining = false;
			newOutputFrame.frameTimestamps = frameTimestamps;
			for(string waitOn : self->lateFrameWaitOn) {
				newOutputFrame.waitingOn[waitOn] = true;
			}
			newOutputFrame.frame = json::object();
			YerFace_MutexLock(self->workerMutex);
			self->pendingFrames[frameNumber] = newOutputFrame;
			YerFace_MutexUnlock(self->workerMutex);
			break;
		case FRAME_STATUS_DRAINING:
			YerFace_MutexLock(self->workerMutex);
			// self->logger->verbose("handleFrameStatusChange() Frame #%lld waiting on me. Queue depth is now %lu", frameNumber, self->pendingFrames.size());
			self->pendingFrames[frameNumber].frameIsDraining = true;
			YerFace_MutexUnlock(self->workerMutex);
			self->workerPool->sendWorkerSignal();
			break;
		case FRAME_STATUS_GONE:
			YerFace_MutexLock(self->workerMutex);
			self->pendingFrames.erase(frameNumber);
			YerFace_MutexUnlock(self->workerMutex);
			break;
	}
}

}; //namespace YerFace
