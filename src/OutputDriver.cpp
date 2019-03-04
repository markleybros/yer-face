
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
	for(auto waitingForMe : waitingOn) {
		if(waitingForMe) {
			return false;
		}
	}
	return true;
}

OutputDriver::OutputDriver(json config, String myOutputFilename, FrameServer *myFrameServer, FaceTracker *myFaceTracker, SDLDriver *mySDLDriver) {
	serverThread = NULL;
	writerThread = NULL;
	writerMutex = NULL;
	writerCond = NULL;
	outputBufMutex = NULL;
	outputFilename = myOutputFilename;
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
	websocketServerPort = config["YerFace"]["OutputDriver"]["websocketServerPort"];
	if(websocketServerPort < 1 || websocketServerPort > 65535) {
		throw runtime_error("Server port is invalid");
	}
	websocketServerEnabled = config["YerFace"]["OutputDriver"]["websocketServerEnabled"];

	autoBasisTransmitted = false;
	basisFlagged = false;
	sdlDriver->onBasisFlagEvent([this] (void) -> void {
		if(this->eventLogger != NULL) {
			this->eventLogger->logEvent("basis", (json)true);
		}
		this->handleNewBasisEvent();
	});
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
		outputBuf.clear();
		outputFilestream.open(outputFilename, ofstream::out | ofstream::binary | ofstream::trunc);
		if(outputFilestream.fail()) {
			throw invalid_argument("could not open outputFile for writing");
		}
		if((outputBufMutex = SDL_CreateMutex()) == NULL) {
			throw runtime_error("Failed creating mutex!");
		}
		if((writerMutex = SDL_CreateMutex()) == NULL) {
			throw runtime_error("Failed creating mutex!");
		}
		if((writerCond = SDL_CreateCond()) == NULL) {
			throw runtime_error("Failed creating condition!");
		}
		//Create worker thread.
		writerThreadRunning = true;
		if((writerThread = SDL_CreateThread(OutputDriver::writeOutputBufferToFile, "WriterThread", (void *)this)) == NULL) {
			throw runtime_error("Failed spawning worker thread!");
		}
	}

	extraFrameData = json::object();

	logger->debug("OutputDriver object constructed and ready to go!");
};

OutputDriver::~OutputDriver() {
	logger->debug("OutputDriver object destructing...");
	if(websocketServerEnabled && serverThread) {
		SDL_WaitThread(serverThread, NULL);
	}
	SDL_DestroyMutex(streamFlagsMutex);
	SDL_DestroyMutex(connectionListMutex);
	if(writerMutex) {
		SDL_DestroyMutex(writerMutex);
	}
	if(outputBufMutex) {
		SDL_DestroyMutex(outputBufMutex);
	}
	if(writerCond) {
		SDL_DestroyCond(writerCond);
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
		return this->handleReplayBasisEvent(sourcePacket);
	};
	eventLogger->registerEventType(basisEvent);
}

void OutputDriver::handleNewBasisEvent(void) {
	this->logger->verbose("Got a Basis Flag event. Handling...");
	YerFace_MutexLock(streamFlagsMutex);
	basisFlagged = true;
	YerFace_MutexUnlock(streamFlagsMutex);
}

bool OutputDriver::handleReplayBasisEvent(json sourcePacket) {
	this->logger->verbose("Received replayed Basis Flag event. Rebroadcasting...");
	if((double)sourcePacket["meta"]["startTime"] < 0.0) {
		sourcePacket["meta"]["frameNumber"] = -1;
		sourcePacket["meta"]["startTime"] = 0.0;
		YerFace_MutexLock(streamFlagsMutex);
		autoBasisTransmitted = true;
		outputNewFrame(sourcePacket, true);
		YerFace_MutexUnlock(streamFlagsMutex);
		return false;
	}
	handleNewBasisEvent();
	return true;
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

int OutputDriver::writeOutputBufferToFile(void *data) {
	OutputDriver *self = (OutputDriver *)data;
	self->logger->verbose("File Writer Thread Alive!");

	YerFace_MutexLock(self->writerMutex);
	while(self->writerThreadRunning) {
		if(SDL_CondWait(self->writerCond, self->writerMutex) < 0) {
			throw runtime_error("Failed waiting on condition.");
		}

		YerFace_MutexLock(self->outputBufMutex);
		while(self->outputBuf.size() && self->outputBuf.back().isReady()) {
			self->outputFilestream << self->outputBuf.back().frame.dump(-1, ' ', true) << "\n";
			self->outputBuf.pop_back();
		}
		YerFace_MutexUnlock(self->outputBufMutex);
	}

	YerFace_MutexLock(self->outputBufMutex);
	if(self->outputBuf.size()) {
		self->logger->error("About to terminate file writer thread, but there are still non-flushed frames in the output buffer!!!");
	}
	YerFace_MutexUnlock(self->outputBufMutex);
	YerFace_MutexUnlock(self->writerMutex);

	self->logger->verbose("File Writer Thread Terminating.");
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
		sdlDriver->setIsRunning(false);
		server.stop();
		return;
	}
	if(!this->sdlDriver->getIsRunning()) {
		server.stop();
	}
	serverSetQuitPollTimer();
}

void OutputDriver::handleCompletedFrame(void) {
	FrameTimestamps frameTimestamps = frameServer->getCompletedFrameTimestamps();

	json frame = extraFrameData;
	frame["meta"] = nullptr;
	frame["meta"]["frameNumber"] = frameTimestamps.frameNumber;
	frame["meta"]["startTime"] = frameTimestamps.startTimestamp;
	frame["meta"]["basis"] = false; //Default to no basis. Will override below as necessary.

	bool allPropsSet = true;
	FacialPose facialPose = faceTracker->getCompletedFacialPose();
	if(facialPose.set) {
		frame["pose"] = nullptr;
		Vec3d angles = Utilities::rotationMatrixToEulerAngles(facialPose.rotationMatrix);
		frame["pose"]["rotation"] = { {"x", angles[0]}, {"y", angles[1]}, {"z", angles[2]} };
		frame["pose"]["translation"] = { {"x", facialPose.translationVector.at<double>(0)}, {"y", facialPose.translationVector.at<double>(1)}, {"z", facialPose.translationVector.at<double>(2)} };
	} else {
		allPropsSet = false;
	}

	json trackers;
	auto markerTrackers = MarkerTracker::getMarkerTrackers();
	for(auto markerTracker : markerTrackers) {
		MarkerPoint markerPoint = markerTracker->getCompletedMarkerPoint();
		if(markerPoint.set) {
			string trackerName = markerTracker->getMarkerType().toString();
			trackers[trackerName.c_str()]["position"] = { {"x", markerPoint.point3d.x}, {"y", markerPoint.point3d.y}, {"z", markerPoint.point3d.z} };
		} else {
			allPropsSet = false;
		}
	}
	if(trackers.size()) {
		frame["trackers"] = trackers;
	}

	if(allPropsSet && !autoBasisTransmitted) {
		autoBasisTransmitted = true;
		frame["meta"]["basis"] = true;
		logger->info("All properties set. Transmitting initial basis flag automatically.");
	}

	YerFace_MutexLock(this->streamFlagsMutex);
	if(basisFlagged) {
		autoBasisTransmitted = true;
		basisFlagged = false;
		frame["meta"]["basis"] = true;
		logger->info("Transmitting basis flag based on received basis event.");
	}
	outputNewFrame(frame);
	YerFace_MutexUnlock(this->streamFlagsMutex);

	extraFrameData = json::object();
}

void OutputDriver::registerLateFrameData(string key) {
	lateFrameWaitOn.push_back(key);
}

void OutputDriver::updateLateFrameData(FrameNumber frameNumber, string key, json value) {
	if(!writerThread) {
		return;
	}
	YerFace_MutexLock(outputBufMutex);
	bool success = false;
	for(auto iter = outputBuf.begin(); iter != outputBuf.end(); ++iter) {
		if((FrameNumber)iter->frame["meta"]["frameNumber"] == frameNumber) {
			iter->frame[key] = value;
			iter->waitingOn[key] = false;
			success = true;
		}
	}
	if(!success) {
		throw runtime_error("could not update desired frame! buffer slippage or something goofy is going on");
	}
	YerFace_MutexUnlock(outputBufMutex);
}

void OutputDriver::insertCompletedFrameData(string key, json value) {
	extraFrameData[key] = value;
}

void OutputDriver::drainPipelineDataNow(void) {
	if(writerThread) {
		YerFace_MutexLock(writerMutex);
		writerThreadRunning = false;
		YerFace_MutexUnlock(writerMutex);
		SDL_CondSignal(writerCond);
		SDL_WaitThread(writerThread, NULL);
	}
	if(outputFilename.length() > 0 && outputFilestream.is_open()) {
		outputFilestream.close();
	}
}

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

	if(writerThread) {
		OutputFrameContainer container;
		if(!replay) {
			for(string waitOn : lateFrameWaitOn) {
				container.waitingOn[waitOn] = true;
			}
		}
		container.frame = frame;
		YerFace_MutexLock(writerMutex);
		YerFace_MutexLock(outputBufMutex);
		outputBuf.push_front(container);
		YerFace_MutexUnlock(outputBufMutex);
		YerFace_MutexUnlock(writerMutex);
		SDL_CondSignal(writerCond);
	}
}

}; //namespace YerFace
