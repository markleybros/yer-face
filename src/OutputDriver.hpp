#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"
#include "FaceTracker.hpp"
#include "MarkerTracker.hpp"
#include "SDLDriver.hpp"
#include "Utilities.hpp"

#define ASIO_STANDALONE
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <set>

using namespace std;

namespace YerFace {

class OutputDriver {
public:
	OutputDriver(json config, FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, SDLDriver *mySDLDriver);
	~OutputDriver();
	static int launchWebSocketServer(void* data);
	void serverOnOpen(websocketpp::connection_hdl handle);
	void serverOnClose(websocketpp::connection_hdl handle);
	void handleCompletedFrame(void);
private:
	FrameDerivatives *frameDerivatives;
	FaceTracker *faceTracker;
	SDLDriver *sdlDriver;
	Logger *logger;

	SDL_mutex *serverMutex;
	int serverPort;
	websocketpp::server<websocketpp::config::asio> server;
	std::set<websocketpp::connection_hdl,std::owner_less<websocketpp::connection_hdl>> connectionList;

	SDL_Thread *serverThread;

	SDL_mutex *basisFlagMutex;
	bool autoBasisTransmitted, basisFlagged;
};

}; //namespace YerFace
