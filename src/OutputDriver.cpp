
#include "OutputDriver.hpp"

#include <string>
#include <iostream>
#include <cstring>
#include <streambuf>

#define _WEBSOCKETPP_CPP11_STRICT_
#define ASIO_STANDALONE
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

using namespace cv;

using namespace websocketpp;
using namespace websocketpp::log;
using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

namespace YerFace {

Logger *wsppLogger = new Logger("WebSockets");

enum CustomWebsocketServerLoggerType: unsigned int {
	CUSTOMER_LOGGER_TYPE_ELOG = 1,
	CUSTOMER_LOGGER_TYPE_ALOG = 2
};

//// Custom logging class for websocketpp which sends log messages to our logger.
template <typename concurrency, typename names, CustomWebsocketServerLoggerType loggerType> class CustomWebsocketServerLogger : public basic<concurrency, names> {
public:
	typedef basic<concurrency, names> base;
	using base::base; //Inherit parent's constructors.

	// Write a message in std::string format to the given channel
	void write(level channel, std::string const &msg) {
		write(channel, msg.c_str());
	}

	// Write a message in c_str() format to the given channel
	void write(level channel, char const *msg) {
		//Filter messages which are disabled at the websocket logger level.
		if(!this->dynamic_test(channel)) {
			return;
		}

		//Map message severity.
		LogMessageSeverity severity = LOG_SEVERITY_INFO;
		if(loggerType == CUSTOMER_LOGGER_TYPE_ELOG) {
			switch(channel) {
				case elevel::devel:
					severity = LOG_SEVERITY_DEBUG4;
					break;
				case elevel::library:
					severity = LOG_SEVERITY_DEBUG1;
					break;
				case elevel::info:
					severity = LOG_SEVERITY_INFO;
					break;
				case elevel::warn:
					severity = LOG_SEVERITY_WARNING;
					break;
				case elevel::rerror:
					severity = LOG_SEVERITY_ERR;
					break;
				case elevel::fatal:
					severity = LOG_SEVERITY_CRIT;
					break;
			}
		} else if(loggerType == CUSTOMER_LOGGER_TYPE_ALOG) {
			switch(channel) {
				default:
					severity = LOG_SEVERITY_DEBUG4;
					break;
				case alevel::connect:
					severity = LOG_SEVERITY_INFO;
					break;
				case alevel::disconnect:
					severity = LOG_SEVERITY_INFO;
					break;
				case alevel::http:
					severity = LOG_SEVERITY_INFO;
					break;
				case alevel::fail:
					severity = LOG_SEVERITY_WARNING;
					break;
			}
		}

		//Output log line.
		std::string msgTrim = Utilities::stringTrim((string)msg);
		wsppLogger->log(severity, "[%s] %s", names::channel_name(channel), msgTrim.c_str());
	}
};

//// Extend websocketpp::config::asio to create our own custom config.
struct CustomWebsocketServerConfig : public websocketpp::config::asio {
	typedef asio type;
	typedef websocketpp::config::asio base;

	typedef base::concurrency_type concurrency_type;

	typedef base::request_type request_type;
	typedef base::response_type response_type;

	typedef base::message_type message_type;
	typedef base::con_msg_manager_type con_msg_manager_type;
	typedef base::endpoint_msg_manager_type endpoint_msg_manager_type;

	typedef CustomWebsocketServerLogger<base::concurrency_type, websocketpp::log::alevel, CUSTOMER_LOGGER_TYPE_ALOG> alog_type;
	typedef CustomWebsocketServerLogger<base::concurrency_type, websocketpp::log::elevel, CUSTOMER_LOGGER_TYPE_ELOG> elog_type;

	typedef base::rng_type rng_type;

	struct transport_config : public base::transport_config {
		typedef type::concurrency_type concurrency_type;
		typedef type::alog_type alog_type;
		typedef type::elog_type elog_type;
		typedef type::request_type request_type;
		typedef type::response_type response_type;
		typedef websocketpp::transport::asio::basic_socket::endpoint socket_type;
	};

	typedef websocketpp::transport::asio::endpoint<transport_config>
	transport_type;
};

class OutputDriverWebSocketServer {
public:
	static int launchWebSocketServer(void* data);
	void serverOnOpen(websocketpp::connection_hdl handle);
	void serverOnClose(websocketpp::connection_hdl handle);
	void serverOnTimer(websocketpp::lib::error_code const &ec);
	void serverSetQuitPollTimer(void);

	OutputDriver *parent;

	SDL_mutex *websocketMutex;
	int websocketServerPort;
	bool websocketServerEnabled;
	websocketpp::server<CustomWebsocketServerConfig> server;
	std::set<websocketpp::connection_hdl,std::owner_less<websocketpp::connection_hdl>> connectionList;
	bool websocketServerRunning;

	SDL_Thread *serverThread;
};

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

OutputDriver::OutputDriver(json config, string myOutputFilename, Status *myStatus, FrameServer *myFrameServer, FaceTracker *myFaceTracker, SDLDriver *mySDLDriver) {
	workerPool = NULL;
	outputFilename = myOutputFilename;
	rawEventsPending.clear();
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
	if((workerMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((rawEventsMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	//We need to know when the frame server has drained.
	frameServerDrained = false;
	FrameServerDrainedEventCallback frameServerDrainedCallback;
	frameServerDrainedCallback.userdata = (void *)this;
	frameServerDrainedCallback.callback = handleFrameServerDrainedEvent;
	frameServer->onFrameServerDrainedEvent(frameServerDrainedCallback);

	autoBasisTransmitted = false;
	sdlDriver->onBasisFlagEvent([this] (void) -> void {
		// Log the user-generated basis event, but don't try to assign it to a frame because we might not have one in our pipeline right now.
		YerFace_MutexLock(this->rawEventsMutex);
		OutputRawEvent rawEvent;
		rawEvent.eventName = "basis";
		rawEvent.payload = (json)true;
		this->rawEventsPending.push_back(rawEvent);
		YerFace_MutexUnlock(this->rawEventsMutex);
	});
	sdlDriver->onJoystickButtonEvent([this] (Uint32 relativeTimestamp, int deviceId, int button, bool pressed, double heldSeconds) -> void {
		YerFace_MutexLock(this->rawEventsMutex);
		OutputRawEvent rawEvent;
		rawEvent.eventName = "controller";
		rawEvent.payload = {
			{ "relativeTimestamp", relativeTimestamp },
			{ "deviceId", deviceId },
			{ "actionType", "button" },
			{ "buttonIndex", button },
			{ "buttonPressed", pressed },
			{ "heldSeconds", heldSeconds }
		};
		if(heldSeconds < 0.0) {
			rawEvent.payload["heldSeconds"] = nullptr;
		}
		this->rawEventsPending.push_back(rawEvent);
		YerFace_MutexUnlock(this->rawEventsMutex);
	});
	sdlDriver->onJoystickAxisEvent([this] (Uint32 relativeTimestamp, int deviceId, int axis, double value) -> void {
		YerFace_MutexLock(this->rawEventsMutex);
		OutputRawEvent rawEvent;
		rawEvent.eventName = "controller";
		rawEvent.payload = {
			{ "relativeTimestamp", relativeTimestamp },
			{ "deviceId", deviceId },
			{ "actionType", "axis" },
			{ "axisIndex", axis },
			{ "axisValue", value }
		};
		this->rawEventsPending.push_back(rawEvent);
		YerFace_MutexUnlock(this->rawEventsMutex);
	});
	sdlDriver->onJoystickHatEvent([this] (Uint32 relativeTimestamp, int deviceId, int hat, int x, int y) -> void {
		YerFace_MutexLock(this->rawEventsMutex);
		OutputRawEvent rawEvent;
		rawEvent.eventName = "controller";
		rawEvent.payload = {
			{ "relativeTimestamp", relativeTimestamp },
			{ "deviceId", deviceId },
			{ "actionType", "hat" },
			{ "hatIndex", hat },
			{ "hatX", x },
			{ "hatY", y }
		};
		this->rawEventsPending.push_back(rawEvent);
		YerFace_MutexUnlock(this->rawEventsMutex);
	});
	logger = new Logger("OutputDriver");

	webSocketServer = new OutputDriverWebSocketServer();
	webSocketServer->parent = this;

	webSocketServer->serverThread = NULL;
	webSocketServer->websocketServerRunning = false;
	if((webSocketServer->websocketMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	webSocketServer->websocketServerPort = config["YerFace"]["OutputDriver"]["websocketServerPort"];
	if(webSocketServer->websocketServerPort < 1 || webSocketServer->websocketServerPort > 65535) {
		throw runtime_error("Server port is invalid");
	}
	webSocketServer->websocketServerEnabled = config["YerFace"]["OutputDriver"]["websocketServerEnabled"];

	//Constrain websocket server logs a bit for sanity.
	webSocketServer->server.get_alog().clear_channels(log::alevel::all);
	webSocketServer->server.get_alog().set_channels(log::alevel::connect | log::alevel::disconnect | log::alevel::app | log::alevel::http | log::alevel::fail);
	webSocketServer->server.get_elog().set_channels(log::elevel::all);

	if(webSocketServer->websocketServerEnabled) {
		webSocketServer->websocketServerRunning = true;
		//Create worker thread.
		if((webSocketServer->serverThread = SDL_CreateThread(OutputDriverWebSocketServer::launchWebSocketServer, "HTTPServer", (void *)webSocketServer)) == NULL) {
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
	frameStatusChangeCallback.newStatus = FRAME_STATUS_PREVIEW_DISPLAY;
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

	logger->debug1("OutputDriver object constructed and ready to go!");
};

OutputDriver::~OutputDriver() noexcept(false) {
	logger->debug1("OutputDriver object destructing...");

	delete workerPool;

	YerFace_MutexLock(workerMutex);
	if(pendingFrames.size() > 0) {
		logger->err("Frames are still pending! Woe is me!");
	}
	YerFace_MutexUnlock(workerMutex);

	if(webSocketServer->websocketServerEnabled && webSocketServer->serverThread) {
		YerFace_MutexLock(webSocketServer->websocketMutex);
		webSocketServer->websocketServerRunning = false;
		YerFace_MutexUnlock(webSocketServer->websocketMutex);
		SDL_WaitThread(webSocketServer->serverThread, NULL);
	}

	SDL_DestroyMutex(rawEventsMutex);
	SDL_DestroyMutex(basisMutex);
	SDL_DestroyMutex(webSocketServer->websocketMutex);
	SDL_DestroyMutex(workerMutex);

	if(outputFilename.length() > 0 && outputFilestream.is_open()) {
		outputFilestream.close();
	}

	delete logger;
	delete webSocketServer;
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
			this->logger->err("Got an unsupported basis replay event!");
			return false;
		}
		this->logger->info("Received replayed Basis Flag event. Rebroadcasting...");
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

	EventType controllerEvent;
	controllerEvent.name = "controller";
	controllerEvent.replayCallback = [this] (string eventName, json eventPayload, json sourcePacket) -> bool {
		if(eventName != "controller" || !eventPayload.is_array()) {
			this->logger->err("Got an unsupported controller replay event!");
			return false;
		}
		FrameNumber frameNumber = (FrameNumber)sourcePacket["meta"]["frameNumber"];
		YerFace_MutexLock(workerMutex);
		if(!pendingFrames[frameNumber].frame.contains("controller")) {
			pendingFrames[frameNumber].frame["controller"] = eventPayload;
		} else {
			if(pendingFrames[frameNumber].frame["controller"].is_array() && eventPayload.is_array()) {
				for(json controllerEvent : eventPayload) {
					pendingFrames[frameNumber].frame["controller"].push_back(controllerEvent);
				}
			} else {
				throw logic_error("trying to apply controller data multiple times for the same frame, but not using arrays?!");
			}
		}
		YerFace_MutexUnlock(workerMutex);
		return true;
	};
	eventLogger->registerEventType(controllerEvent);
}

void OutputDriver::handleNewBasisEvent(FrameNumber frameNumber) {
	logger->debug1("Got a Basis Flag event for Frame #%lu. Handling...", frameNumber);
	YerFace_MutexLock(workerMutex);
	pendingFrames[frameNumber].frame["meta"]["basis"] = true;
	YerFace_MutexUnlock(workerMutex);
}

void OutputDriver::handleOutputFrame(OutputFrameContainer *outputFrame) {
	// outputFrame->frame["meta"] = json::object();
	outputFrame->frame["meta"]["frameNumber"] = outputFrame->frameTimestamps.frameNumber;
	outputFrame->frame["meta"]["startTime"] = outputFrame->frameTimestamps.startTimestamp;
	if(!outputFrame->frame["meta"].contains("basis")) {
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
	if(!pendingFrames[frameNumber].waitingOn.contains(key)) {
		throw runtime_error("Somebody is trying to insert frame data which was not previously registered!");
	}
	if(pendingFrames[frameNumber].waitingOn[key] != true) {
		throw runtime_error("Somebody is trying to insert frame data which was already inserted!");
	}
	pendingFrames[frameNumber].frame[key] = value;
	pendingFrames[frameNumber].waitingOn[key] = false;
	YerFace_MutexUnlock(workerMutex);
}

void OutputDriver::outputNewFrame(json frame) {
	if(frame["meta"]["basis"]) {
		YerFace_MutexLock(basisMutex);
		lastBasisFrame = frame;
		YerFace_MutexUnlock(basisMutex);
	}

	std::string jsonString;
	jsonString = frame.dump(-1, ' ', true);

	YerFace_MutexLock(webSocketServer->websocketMutex);
	try {
		for(auto handle : webSocketServer->connectionList) {
			webSocketServer->server.send(handle, jsonString, websocketpp::frame::opcode::text);
		}
	} catch (websocketpp::exception const &e) {
		logger->err("Got a websocket exception: %s", e.what());
	}
	YerFace_MutexUnlock(webSocketServer->websocketMutex);

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
		self->logger->debug4("BLOCKED on frame " YERFACE_FRAMENUMBER_FORMAT " because it is not ready!", myFrameNumber);
		myFrameNumber = -1;
		outputFrame = NULL;
	}
	YerFace_MutexUnlock(self->workerMutex);

	//// DO THE WORK ////
	if(outputFrame != NULL) {
		self->logger->debug4("Output Worker Thread handling frame #" YERFACE_FRAMENUMBER_FORMAT, outputFrame->frameTimestamps.frameNumber);

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
	OutputRawEvent logEvent;
	unordered_map<string, json> eventBuffer;
	unordered_map<string, json>::iterator eventBufferIter;
	static OutputFrameContainer newOutputFrame;
	switch(newStatus) {
		default:
			throw logic_error("Handler passed unsupported frame status change event!");
		case FRAME_STATUS_NEW:
			self->logger->debug4("handleFrameStatusChange() Frame #" YERFACE_FRAMENUMBER_FORMAT " appearing as new! Queue depth is now %lu", frameNumber, self->pendingFrames.size());
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
			YerFace_MutexUnlock(self->workerMutex);
			break;
		case FRAME_STATUS_PREVIEW_DISPLAY:
			// Handle any pending raw events. (Assign them to frames as close as possible to real time.)
			eventBuffer.clear();
			YerFace_MutexLock(self->rawEventsMutex);
			while(self->rawEventsPending.size() > 0) {
				OutputRawEvent rawEvent = self->rawEventsPending.front();
				self->rawEventsPending.pop_front();

				//Accumulate raw events into an array, for cases where multiple of the same event fired during a single frame.
				eventBufferIter = eventBuffer.find(rawEvent.eventName);
				if(eventBufferIter == eventBuffer.end()) {
					eventBuffer[rawEvent.eventName] = json::array();
				}

				eventBuffer[rawEvent.eventName].push_back(rawEvent.payload);
			}
			YerFace_MutexUnlock(self->rawEventsMutex);

			eventBufferIter = eventBuffer.begin();
			while(eventBufferIter != eventBuffer.end()) {
				logEvent.eventName = eventBufferIter->first;
				logEvent.payload = eventBufferIter->second;

				if(logEvent.eventName == "basis") {
					self->handleNewBasisEvent(frameNumber);
					logEvent.payload = (json)true;
				} else {
					YerFace_MutexLock(self->workerMutex);
					self->pendingFrames[frameNumber].frame[logEvent.eventName] = logEvent.payload;
					YerFace_MutexUnlock(self->workerMutex);
				}

				if(self->eventLogger != NULL) {
					self->eventLogger->logEvent(logEvent.eventName, logEvent.payload, frameTimestamps);
				}
				eventBufferIter++;
			}
			break;
		case FRAME_STATUS_DRAINING:
			YerFace_MutexLock(self->workerMutex);
			self->logger->debug4("handleFrameStatusChange() Frame #" YERFACE_FRAMENUMBER_FORMAT " waiting on me. Queue depth is now %lu", frameNumber, self->pendingFrames.size());
			self->pendingFrames[frameNumber].frameIsDraining = true;
			YerFace_MutexUnlock(self->workerMutex);
			if(self->workerPool != NULL) {
				self->workerPool->sendWorkerSignal();
			}
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
	self->logger->debug2("Got notification that FrameServer has drained!");
	YerFace_MutexLock(self->workerMutex);
	self->frameServerDrained = true;
	YerFace_MutexUnlock(self->workerMutex);
}

int OutputDriverWebSocketServer::launchWebSocketServer(void *data) {
	OutputDriverWebSocketServer *self = (OutputDriverWebSocketServer *)data;
	try {
		self->parent->logger->debug1("WebSocket Server Thread Alive!");

		self->server.init_asio();
		self->server.set_reuse_addr(true);
		self->server.set_open_handler(bind(&OutputDriverWebSocketServer::serverOnOpen,self,::_1));
		self->server.set_close_handler(bind(&OutputDriverWebSocketServer::serverOnClose,self,::_1));
		self->serverSetQuitPollTimer();

		self->server.listen(self->websocketServerPort);
		self->server.start_accept();
		self->server.run();

		self->parent->logger->debug1("WebSocket Server Thread Terminating.");
		return 0;
	} catch(std::exception &e) {
		self->parent->logger->emerg("Uncaught exception in web socket server thread: %s\n", e.what());
		self->parent->status->setEmergency();
	}
	return 1;
}

void OutputDriverWebSocketServer::serverOnOpen(websocketpp::connection_hdl handle) {
	YerFace_MutexLock(parent->basisMutex);
	string jsonString = parent->lastBasisFrame.dump(-1, ' ', true);
	YerFace_MutexUnlock(parent->basisMutex);

	YerFace_MutexLock(websocketMutex);
	parent->logger->debug1("WebSocket Connection Opened.");
	server.send(handle, jsonString, websocketpp::frame::opcode::text);
	connectionList.insert(handle);
	YerFace_MutexUnlock(websocketMutex);
}

void OutputDriverWebSocketServer::serverOnClose(websocketpp::connection_hdl handle) {
	YerFace_MutexLock(websocketMutex);
	connectionList.erase(handle);
	parent->logger->debug1("WebSocket Connection Closed.");
	YerFace_MutexUnlock(websocketMutex);
}

void OutputDriverWebSocketServer::serverOnTimer(websocketpp::lib::error_code const &ec) {
	if(ec) {
		parent->logger->err("WebSocket Library Reported an Error: %s", ec.message().c_str());
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

void OutputDriverWebSocketServer::serverSetQuitPollTimer(void) {
	server.set_timer(100, websocketpp::lib::bind(&OutputDriverWebSocketServer::serverOnTimer,this,::_1));
}

}; //namespace YerFace
