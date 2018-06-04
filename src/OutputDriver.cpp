
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

OutputDriver::OutputDriver(json config, FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, SDLDriver *mySDLDriver) {
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	faceTracker = myFaceTracker;
	if(faceTracker == NULL) {
		throw invalid_argument("faceTracker cannot be NULL");
	}
	sdlDriver = mySDLDriver;
	if(sdlDriver == NULL) {
		throw invalid_argument("sdlDriver cannot be NULL");
	}
	if((streamFlagsMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((connectionListMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	serverPort = config["YerFace"]["OutputDriver"]["serverPort"];
	if(serverPort < 1 || serverPort > 65535) {
		throw runtime_error("Server port is invalid");
	}

	autoBasisTransmitted = false;
	basisFlagged = false;
	sdlDriver->onBasisFlagEvent([this] (void) -> void {
		this->logger->info("Got a Basis Flag event. Handling...");
		YerFace_MutexLock(this->streamFlagsMutex);
		this->basisFlagged = true;
		YerFace_MutexUnlock(this->streamFlagsMutex);
	});
	logger = new Logger("OutputDriver");

	//Create worker thread.
	if((serverThread = SDL_CreateThread(OutputDriver::launchWebSocketServer, "HTTPServer", (void *)this)) == NULL) {
		throw runtime_error("Failed spawning worker thread!");
	}

	//Constrain websocket server logs a bit for sanity.
	server.get_alog().clear_channels(log::alevel::all);
	server.get_alog().set_channels(log::alevel::connect | log::alevel::disconnect | log::alevel::app | log::alevel::http | log::alevel::fail);
	server.get_elog().clear_channels(log::elevel::all);
	server.get_elog().set_channels(log::elevel::info | log::elevel::warn | log::elevel::rerror | log::elevel::fatal);

	logger->debug("OutputDriver object constructed and ready to go!");
};

OutputDriver::~OutputDriver() {
	logger->debug("OutputDriver object destructing...");
	SDL_WaitThread(serverThread, NULL);
	SDL_DestroyMutex(streamFlagsMutex);
	SDL_DestroyMutex(connectionListMutex);
	delete logger;
}

int OutputDriver::launchWebSocketServer(void *data) {
	OutputDriver *self = (OutputDriver *)data;
	self->logger->verbose("WebSocket Server Thread Alive!");

	self->server.init_asio();
	self->server.set_reuse_addr(true);
	self->server.set_open_handler(bind(&OutputDriver::serverOnOpen,self,::_1));
	self->server.set_close_handler(bind(&OutputDriver::serverOnClose,self,::_1));
	self->serverSetQuitPollTimer();

	self->server.listen(self->serverPort);
	self->server.start_accept();
	self->server.run();

	self->logger->verbose("WebSocket Server Thread Terminating.");
	return 0;
}

void OutputDriver::serverOnOpen(websocketpp::connection_hdl handle) {
	YerFace_MutexLock(this->streamFlagsMutex);
	YerFace_MutexLock(this->connectionListMutex);
	logger->verbose("WebSocket Connection Opened: 0x%X", handle);
	std::stringstream jsonString;
	jsonString << lastBasisFrame.dump(-1, ' ', true);
	server.send(handle, jsonString.str(), websocketpp::frame::opcode::text);
	connectionList.insert(handle);
	YerFace_MutexUnlock(this->connectionListMutex);
	YerFace_MutexUnlock(this->streamFlagsMutex);
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
	FrameTimestamps frameTimestamps = frameDerivatives->getCompletedFrameTimestamps();

	json frame;
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

	if(frame["meta"]["basis"]) {
		lastBasisFrame = frame;
	}

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
	YerFace_MutexUnlock(this->streamFlagsMutex);
}

void OutputDriver::serverSetQuitPollTimer(void) {
	server.set_timer(100, websocketpp::lib::bind(&OutputDriver::serverOnTimer,this,::_1));
}

}; //namespace YerFace
