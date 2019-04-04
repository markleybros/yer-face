
#include "Logger.hpp"
#include "Status.hpp"
#include "SDLDriver.hpp"
#include "FFmpegDriver.hpp"
#include "FrameServer.hpp"
#include "FaceDetector.hpp"
#include "FaceTracker.hpp"
#include "FaceMapper.hpp"
#include "Metrics.hpp"
#include "Utilities.hpp"
#include "OutputDriver.hpp"
#include "SphinxDriver.hpp"
#include "EventLogger.hpp"
#include "ImageSequence.hpp"
#include "PreviewHUD.hpp"
#include "WorkerPool.hpp"

#include <iostream>
#include <sstream>
#include <cstdio>
#include <cstdlib>

using namespace std;
using namespace cv;
using namespace YerFace;

String configFile;

String inVideo;
String inVideoFormat;
String inVideoSize;
String inVideoRate;
String inVideoCodec;

String inAudio;
String inAudioFormat;
String inAudioChannels;
String inAudioRate;
String inAudioCodec;

String outAudioChannelMap;

String inEvents;
String outData;
String previewImgSeq;
bool lowLatency = false;
bool headless = false;
bool audioPreview = false;
bool tryAudioInVideo = false;
bool openInputAudio = false;

json config = NULL;

SDLWindowRenderer sdlWindowRenderer;
bool windowInitializationFailed;
WorkerPool *videoCaptureWorkerPool;

Status *status = NULL;
Logger *logger = NULL;
SDLDriver *sdlDriver = NULL;
FFmpegDriver *ffmpegDriver = NULL;
FrameServer *frameServer = NULL;
FaceDetector *faceDetector = NULL;
FaceTracker *faceTracker = NULL;
FaceMapper *faceMapper = NULL;
Metrics *metrics = NULL;
OutputDriver *outputDriver = NULL;
SphinxDriver *sphinxDriver = NULL;
EventLogger *eventLogger = NULL;
ImageSequence *imageSequence = NULL;
PreviewHUD *previewHUD = NULL;

//VARIABLES PROTECTED BY frameSizeMutex
Size frameSize;
bool frameSizeValid = false;
SDL_mutex *frameSizeMutex;
//END VARIABLES PROTECTED BY frameSizeMutex

//VARIABLES PROTECTED BY previewDisplayMutex
list<FrameNumber> previewDisplayFrameNumbers;
SDL_mutex *previewDisplayMutex;
//END VARIABLES PROTECTED BY previewDisplayMutex

//VARIABLES PROTECTED BY frameServerDrainedMutex
bool frameServerDrained = false;
SDL_mutex *frameServerDrainedMutex;
//END VARIABLES PROTECTED BY frameServerDrainedMutex

//VARIABLES PROTECTED BY frameMetricsMutex
unordered_map<FrameNumber, MetricsTick> frameMetricsTicks;
SDL_mutex *frameMetricsMutex;
//END VARIABLES PROTECTED BY frameMetricsMutex

void videoCaptureInitializer(WorkerPoolWorker *worker, void *ptr);
bool videoCaptureHandler(WorkerPoolWorker *worker);
void videoCaptureDeinitializer(WorkerPoolWorker *worker, void *ptr);
void parseConfigFile(void);
void handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps);
void handleFrameServerDrainedEvent(void *userdata);
void renderPreviewHUD(Mat previewFrame, FrameNumber frameNumber, int density);

int main(int argc, const char** argv) {
	Logger::setLoggingFilter(SDL_LOG_PRIORITY_VERBOSE, SDL_LOG_CATEGORY_APPLICATION);
	logger = new Logger("YerFace");

	//Command line options.
	CommandLineParser parser(argc, argv,
		"{help h||Usage message.}"
		"{configFile C|search|Required configuration file. (Indicate the full or relative path to your 'yer-face-config.json' file, or 'search' to search common locations.)}"
		"{lowLatency||If true, will tweak behavior across the system to minimize latency. (Don't use this if the input is pre-recorded!)}"
		"{inVideo|/dev/video0|Video file, URL, or device to open. (Or '-' for STDIN.)}"
		"{inVideoFormat||Tell libav to use a specific format to interpret the inVideo. Leave blank for auto-detection.}"
		"{inVideoSize||Tell libav to attempt a specific resolution when interpreting inVideo. Leave blank for auto-detection.}"
		"{inVideoRate||Tell libav to attempt a specific framerate when interpreting inVideo. Leave blank for auto-detection.}"
		"{inVideoCodec||Tell libav to attempt a specific codec when interpreting inVideo. Leave blank for auto-detection.}"
		"{inAudio||Audio file, URL, or device to open. Alternatively: '' (blank string, the default) we will try to read the audio from inVideo. '-' we will try to read the audio from STDIN. 'ignore' we will ignore all audio from all sources.}"
		"{inAudioFormat||Tell libav to use a specific format to interpret the inAudio. Leave blank for auto-detection.}"
		"{inAudioChannels||Tell libav to attempt a specific number of channels when interpreting inAudio. Leave blank for auto-detection.}"
		"{inAudioRate||Tell libav to attempt a specific sample rate when interpreting inAudio. Leave blank for auto-detection.}"
		"{inAudioCodec||Tell libav to attempt a specific codec when interpreting inAudio. Leave blank for auto-detection.}"
		"{outAudioChannelMap||Alter the audio channel mapping. Set to \"left\" to take only the left channel, \"right\" to take only the right channel, and leave blank for the default.}"
		"{inEvents||Event replay file. (Previously generated outData, for re-processing recorded sessions.)}"
		"{outData||Output file for generated performance capture data.}"
		"{audioPreview||If true, will preview processed audio out the computer's sound device.}"
		"{previewImgSeq||If set, is presumed to be the file name prefix of the output preview image sequence.}"
		"{headless||If set, all video and audio output is disabled. Intended to be suitable for jobs running in the terminal.}"
		"{version||Emit the version string to STDOUT and exit.}"
		);
	parser.about("YerFace! A stupid facial performance capture engine for cartoon animation. (" YERFACE_VERSION ")");
	if(parser.get<bool>("version")) {
		fprintf(stdout, "%s\n", YERFACE_VERSION);
		return 0;
	}
	if(parser.get<bool>("help")) {
		parser.printMessage();
		return 1;
	}
	if(parser.check()) {
		parser.printMessage();
		parser.printErrors();
		return 1;
	}
	configFile = parser.get<string>("configFile");
	parseConfigFile();
	inVideo = parser.get<string>("inVideo");
	if(inVideo == "-") {
		inVideo = "pipe:0";
	}
	inVideoFormat = parser.get<string>("inVideoFormat");
	inVideoSize = parser.get<string>("inVideoSize");
	inVideoRate = parser.get<string>("inVideoRate");
	inVideoCodec = parser.get<string>("inVideoCodec");
	inAudio = parser.get<string>("inAudio");
	if(inAudio == "-") {
		inAudio = "pipe:0";
	}
	if(inAudio.length() > 0) {
		if(inAudio == "ignore") {
			openInputAudio = false;
			tryAudioInVideo = false;
		} else {
			openInputAudio = true;
			tryAudioInVideo = false;
		}
	} else {
		openInputAudio = false;
		tryAudioInVideo = true;
	}
	inAudioFormat = parser.get<string>("inAudioFormat");
	inAudioChannels = parser.get<string>("inAudioChannels");
	inAudioRate = parser.get<string>("inAudioRate");
	inAudioCodec = parser.get<string>("inAudioCodec");
	outAudioChannelMap = parser.get<string>("outAudioChannelMap");
	inEvents = parser.get<string>("inEvents");
	outData = parser.get<string>("outData");
	previewImgSeq = parser.get<string>("previewImgSeq");
	lowLatency = parser.get<bool>("lowLatency");
	headless = parser.get<bool>("headless");
	audioPreview = parser.get<bool>("audioPreview");

	sdlWindowRenderer.window = NULL;
	sdlWindowRenderer.renderer = NULL;
	windowInitializationFailed = false;

	//Create locks.
	if((frameSizeMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((previewDisplayMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((frameServerDrainedMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((frameMetricsMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	//Instantiate our classes.
	status = new Status(lowLatency);
	metrics = new Metrics(config, "YerFace", true);
	frameServer = new FrameServer(config, status, lowLatency);
	previewHUD = new PreviewHUD(config, status, frameServer);
	ffmpegDriver = new FFmpegDriver(status, frameServer, lowLatency, false);
	ffmpegDriver->openInputMedia(inVideo, AVMEDIA_TYPE_VIDEO, inVideoFormat, inVideoSize, "", inVideoRate, inVideoCodec, outAudioChannelMap, tryAudioInVideo);
	if(openInputAudio) {
		ffmpegDriver->openInputMedia(inAudio, AVMEDIA_TYPE_AUDIO, inAudioFormat, "", inAudioChannels, inAudioRate, inAudioCodec, outAudioChannelMap, true);
	}
	sdlDriver = new SDLDriver(config, status, frameServer, ffmpegDriver, headless, audioPreview && ffmpegDriver->getIsAudioInputPresent());
	faceDetector = new FaceDetector(config, status, frameServer);
	faceTracker = new FaceTracker(config, status, sdlDriver, frameServer, faceDetector);
	faceMapper = new FaceMapper(config, status, frameServer, faceTracker, previewHUD);
	outputDriver = new OutputDriver(config, outData, status, frameServer, faceTracker, sdlDriver);
	if(ffmpegDriver->getIsAudioInputPresent()) {
		sphinxDriver = new SphinxDriver(config, status, frameServer, ffmpegDriver, sdlDriver, outputDriver, previewHUD, lowLatency);
	}
	eventLogger = new EventLogger(config, inEvents, status, outputDriver, frameServer);
	if(previewImgSeq.length() > 0) {
		imageSequence = new ImageSequence(config, status, frameServer, previewImgSeq);
	}

	outputDriver->setEventLogger(eventLogger);

	//Register preview renderers.
	previewHUD->registerPreviewHUDRenderer(renderPreviewHUD);

	//Hook into the frame lifecycle.
	FrameServerDrainedEventCallback frameServerDrainedCallback;
	frameServerDrainedCallback.userdata = NULL;
	frameServerDrainedCallback.callback = handleFrameServerDrainedEvent;
	frameServer->onFrameServerDrainedEvent(frameServerDrainedCallback);

	FrameStatusChangeEventCallback frameStatusChangeCallback;
	frameStatusChangeCallback.userdata = NULL;
	frameStatusChangeCallback.callback = handleFrameStatusChange;
	frameStatusChangeCallback.newStatus = FRAME_STATUS_NEW;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	if(!headless) {
		frameStatusChangeCallback.newStatus = FRAME_STATUS_PREVIEW_DISPLAY;
		frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
		frameServer->registerFrameStatusCheckpoint(FRAME_STATUS_PREVIEW_DISPLAY, "main.PreviewDisplayed");
	}
	frameStatusChangeCallback.newStatus = FRAME_STATUS_GONE;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);

	//Create worker thread.
	WorkerPoolParameters workerPoolParameters;
	workerPoolParameters.name = "Main.VideoCapture";
	workerPoolParameters.numWorkers = 1;
	workerPoolParameters.numWorkersPerCPU = 0.0;
	workerPoolParameters.initializer = videoCaptureInitializer;
	workerPoolParameters.deinitializer = videoCaptureDeinitializer;
	workerPoolParameters.usrPtr = NULL;
	workerPoolParameters.handler = videoCaptureHandler;
	videoCaptureWorkerPool = new WorkerPool(config, status, frameServer, workerPoolParameters);

	//Launch event / rendering loop.
	bool myDrained = false;
	while(status->getIsRunning() || !myDrained) {
		// Window initialization.
		if(!headless && sdlWindowRenderer.window == NULL && !windowInitializationFailed) {
			YerFace_MutexLock(frameSizeMutex);
			if(!frameSizeValid) {
				YerFace_MutexUnlock(frameSizeMutex);
				continue;
			}
			try {
				sdlWindowRenderer = sdlDriver->createPreviewWindow(frameSize.width, frameSize.height, "YerFace! Preview Window");
			} catch(exception &e) {
				windowInitializationFailed = true;
				logger->error("Uh oh, failed to create a preview window! Got exception: %s", e.what());
				logger->warn("Continuing despite the lack of a preview window.");
			}
			YerFace_MutexUnlock(frameSizeMutex);
		}

		//Preview frame display.
		if(!headless) {
			std::list<FrameNumber> previewFrames;
			YerFace_MutexLock(previewDisplayMutex);
			//If there are preview frames waiting to be displayed, handle them.
			while(previewDisplayFrameNumbers.size() > 0) {
				previewFrames.push_front(previewDisplayFrameNumbers.back());
				previewDisplayFrameNumbers.pop_back();
			}
			YerFace_MutexUnlock(previewDisplayMutex);

			//Note that we can't call frameServer->setWorkingFrameStatusCheckpoint() inside the above loop, because we would deadlock.
			while(previewFrames.size() > 0) {
				FrameNumber previewFrameNumber = previewFrames.back();
				previewFrames.pop_back();

				//Display the last preview frame to the screen.
				if(previewFrames.size() == 0 && sdlWindowRenderer.window != NULL) {
					WorkingFrame *previewFrame = frameServer->getWorkingFrame(previewFrameNumber);
					YerFace_MutexLock(previewFrame->previewFrameMutex);
					Mat previewFrameCopy = previewFrame->previewFrame.clone();
					YerFace_MutexUnlock(previewFrame->previewFrameMutex);
					frameServer->setWorkingFrameStatusCheckpoint(previewFrameNumber, FRAME_STATUS_PREVIEW_DISPLAY, "main.PreviewDisplayed");
					sdlDriver->doRenderPreviewFrame(previewFrameCopy);
				} else {
					frameServer->setWorkingFrameStatusCheckpoint(previewFrameNumber, FRAME_STATUS_PREVIEW_DISPLAY, "main.PreviewDisplayed");
				}
			}
		}

		//SDL bookkeeping.
		sdlDriver->doHandleEvents();

		//Be a good neighbor.
		SDL_Delay(10); //FIXME - better timing?

		YerFace_MutexLock(frameServerDrainedMutex);
		myDrained = frameServerDrained;
		YerFace_MutexUnlock(frameServerDrainedMutex);
	}

	//Join worker thread.
	delete videoCaptureWorkerPool;

	//Cleanup.
	if(imageSequence != NULL) {
		delete imageSequence;
	}
	delete eventLogger;
	if(sphinxDriver != NULL) {
		delete sphinxDriver;
	}
	delete outputDriver;
	delete faceMapper;
	delete faceTracker;
	delete faceDetector;
	delete previewHUD;
	delete frameServer;
	delete ffmpegDriver;
	delete sdlDriver;
	delete metrics;
	delete status;
	delete logger;

	SDL_DestroyMutex(frameSizeMutex);
	SDL_DestroyMutex(previewDisplayMutex);
	SDL_DestroyMutex(frameServerDrainedMutex);
	SDL_DestroyMutex(frameMetricsMutex);

	return 0;
}

void videoCaptureInitializer(WorkerPoolWorker *worker, void *ptr) {
	ffmpegDriver->setVideoCaptureWorkerPool(videoCaptureWorkerPool);
	ffmpegDriver->rollDemuxerThreads();
}

bool videoCaptureHandler(WorkerPoolWorker *worker) {
	VideoFrame videoFrame;
	bool didWork = false;
	static bool didSetFrameSizeValid = false;
	
	if(!ffmpegDriver->waitForNextVideoFrame(&videoFrame)) {
		logger->info("FFmpeg Demuxer thread finished.");
		status->setIsRunning(false);
	} else {
		if(!didSetFrameSizeValid) {
			YerFace_MutexLock(frameSizeMutex);
			if(!frameSizeValid) {
				frameSize = videoFrame.frameCV.size();
				frameSizeValid = true;
				didSetFrameSizeValid = true;
			}
			YerFace_MutexUnlock(frameSizeMutex);
		}
		frameServer->insertNewFrame(&videoFrame);
		ffmpegDriver->releaseVideoFrame(videoFrame);
		didWork = true;
	}

	if(!status->getIsRunning()) {
		videoCaptureWorkerPool->stopWorkerNow();
		didWork = true;
	}

	return didWork;
}

void videoCaptureDeinitializer(WorkerPoolWorker *worker, void *ptr) {
	frameServer->setDraining();

	bool myDrained = false;
	do {
		SDL_Delay(100);
		YerFace_MutexLock(frameServerDrainedMutex);
		myDrained = frameServerDrained;
		YerFace_MutexUnlock(frameServerDrainedMutex);
	} while(!myDrained);

	sdlDriver->stopAudioDriverNow();
	ffmpegDriver->stopAudioCallbacksNow();
}

void parseConfigFile(void) {
	if(configFile == "search") {
		configFile = Utilities::fileValidPathOrDie("yer-face-config.json", true);
	} else {
		configFile = Utilities::fileValidPathOrDie(configFile);
	}
	try {
		logger->verbose("Opening and parsing config file: \"%s\"", configFile.c_str());
		std::ifstream fileStream = std::ifstream(configFile);
		if(fileStream.fail()) {
			throw invalid_argument("Specified config file failed to open.");
		}
		std::stringstream ssBuffer;
		ssBuffer << fileStream.rdbuf();
		config = json::parse(ssBuffer.str());
	} catch(exception &e) {
		logger->error("Failed to parse configuration file \"%s\". Got exception: %s", configFile.c_str(), e.what());
		throw;
	}
}

void handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps) {
	FrameNumber frameNumber = frameTimestamps.frameNumber;
	switch(newStatus) {
		default:
			throw logic_error("Handler passed unsupported frame status change event!");
		case FRAME_STATUS_NEW:
			YerFace_MutexLock(frameMetricsMutex);
			frameMetricsTicks[frameNumber] = metrics->startClock();
			YerFace_MutexUnlock(frameMetricsMutex);
			break;
		case FRAME_STATUS_PREVIEW_DISPLAY:
			YerFace_MutexLock(previewDisplayMutex);
			previewDisplayFrameNumbers.push_front(frameNumber);
			YerFace_MutexUnlock(previewDisplayMutex);
			break;
		case FRAME_STATUS_GONE:
			YerFace_MutexLock(frameMetricsMutex);
			metrics->endClock(frameMetricsTicks[frameNumber]);
			frameMetricsTicks.erase(frameNumber);
			YerFace_MutexUnlock(frameMetricsMutex);
			break;
	}
}

void handleFrameServerDrainedEvent(void *userdata) {
	YerFace_MutexLock(frameServerDrainedMutex);
	frameServerDrained = true;
	YerFace_MutexUnlock(frameServerDrainedMutex);
}

void renderPreviewHUD(Mat previewFrame, FrameNumber frameNumber, int density) {
	faceDetector->renderPreviewHUD(previewFrame, frameNumber, density);
	faceTracker->renderPreviewHUD(previewFrame, frameNumber, density);
	faceMapper->renderPreviewHUD(previewFrame, frameNumber, density);
	if(sphinxDriver != NULL) {
		sphinxDriver->renderPreviewHUD(previewFrame, frameNumber, density);
	}
	putText(previewFrame, metrics->getTimesString().c_str(), Point(25,50), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);
	putText(previewFrame, metrics->getFPSString().c_str(), Point(25,75), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);
}
