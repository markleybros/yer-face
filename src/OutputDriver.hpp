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

#include <fstream>

using namespace std;

namespace YerFace {

class OutputFrameContainer {
public:
	bool ready;
	json frame;
};

class OutputDriver {
public:
	OutputDriver(json config, bool mySphinxEnabled, String myOutputFilename, FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, SDLDriver *mySDLDriver);
	~OutputDriver();
	void handleCompletedFrame(void);
	void drainPipelineDataNow(void);
	void updateLateFrameData(signed long frameNumber, string key, json value);
private:
	static int launchWebSocketServer(void* data);
	static int writeOutputBufferToFile(void *data);
	void writeFrameToOutputStream(OutputFrameContainer *container);
	void serverOnOpen(websocketpp::connection_hdl handle);
	void serverOnClose(websocketpp::connection_hdl handle);
	void serverOnTimer(websocketpp::lib::error_code const &ec);
	void serverSetQuitPollTimer(void);

	bool sphinxEnabled;
	String outputFilename;
	FrameDerivatives *frameDerivatives;
	FaceTracker *faceTracker;
	SDLDriver *sdlDriver;
	Logger *logger;

	ofstream outputFilestream;

	SDL_mutex *connectionListMutex;
	int websocketServerPort;
	bool websocketServerEnabled;
	websocketpp::server<websocketpp::config::asio> server;
	std::set<websocketpp::connection_hdl,std::owner_less<websocketpp::connection_hdl>> connectionList;

	SDL_Thread *serverThread;

	SDL_mutex *writerMutex;
	SDL_cond *writerCond;
	SDL_Thread *writerThread;
	bool writerThreadRunning;

	SDL_mutex *streamFlagsMutex;
	bool autoBasisTransmitted, basisFlagged;
	json lastBasisFrame;

	list<OutputFrameContainer *> outputFrameBuffer;
};

}; //namespace YerFace
