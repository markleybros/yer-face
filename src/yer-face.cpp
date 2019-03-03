
#include "opencv2/objdetect.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/tracking.hpp"

#include "Logger.hpp"
#include "SDLDriver.hpp"
#include "FFmpegDriver.hpp"
// #include "FaceTracker.hpp"
#include "FrameServer.hpp"
// #include "FaceMapper.hpp"
#include "Metrics.hpp"
#include "Utilities.hpp"
// #include "OutputDriver.hpp"
// #include "SphinxDriver.hpp"
// #include "EventLogger.hpp"

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

double from, until;

String window_name = "Yer Face: A Stupid Facial Performance Capture Engine";

json config = NULL;

SDLWindowRenderer sdlWindowRenderer;
bool windowInitializationFailed;
SDL_Thread *workerThread;

Logger *logger = NULL;
SDLDriver *sdlDriver = NULL;
FFmpegDriver *ffmpegDriver = NULL;
FrameServer *frameServer = NULL;
// FaceTracker *faceTracker = NULL;
// FaceMapper *faceMapper = NULL;
Metrics *metrics = NULL;
// OutputDriver *outputDriver = NULL;
// SphinxDriver *sphinxDriver = NULL;
// EventLogger *eventLogger = NULL;

// unsigned long workingFrameNumber = 0;
SDL_mutex *flipWorkingCompletedMutex;

//VARIABLES PROTECTED BY frameSizeMutex
Size frameSize;
bool frameSizeValid = false;
SDL_mutex *frameSizeMutex;
//END VARIABLES PROTECTED BY frameSizeMutex

static int runCaptureLoop(void *ptr);
// Mat doRenderPreviewFrame(void);
void parseConfigFile(void);

void tempeventhandler(unsigned long frameNumber, WorkingFrameStatus newStatus) {
	logger->verbose("Got a status change event for frame %ld! New status is %d", frameNumber, newStatus);
}

int main(int argc, const char** argv) {
	Logger::setLoggingFilter(SDL_LOG_PRIORITY_VERBOSE, SDL_LOG_CATEGORY_APPLICATION);
	logger = new Logger("YerFace");

	//Command line options.
	CommandLineParser parser(argc, argv,
		"{help h||Usage message.}"
		"{configFile C|data/config.json|Required configuration file.}"
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
		"{from|-1.0|Start processing at \"from\" seconds into the input video. Unit is seconds. Minus one means the beginning of the input.}"
		"{until|-1.0|Stop processing at \"until\" seconds into the input video. Unit is seconds. Minus one means the end of the input.}"
		"{inEvents||Event replay file. (Previously generated outData, for re-processing recorded sessions.)}"
		"{outData||Output file for generated performance capture data.}"
		"{audioPreview||If true, will preview processed audio out the computer's sound device.}"
		"{previewImgSeq||If set, is presumed to be the file name prefix of the output preview image sequence.}"
		"{headless||If set, all video and audio output is disabled. Intended to be suitable for jobs running in the terminal.}"
		);

	parser.about("Yer Face: The butt of all the jokes. (A stupid facial performance capture engine for cartoon animation.)");
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
	try {
		parseConfigFile();
	} catch(exception &e) {
		logger->error("Failed to parse configuration file \"%s\". Got exception: %s", configFile.c_str(), e.what());
		return 1;
	}
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
	from = parser.get<double>("from");
	until = parser.get<double>("until");
	inEvents = parser.get<string>("inEvents");
	outData = parser.get<string>("outData");
	previewImgSeq = parser.get<string>("previewImgSeq");
	lowLatency = parser.get<bool>("lowLatency");
	headless = parser.get<bool>("headless");
	audioPreview = parser.get<bool>("audioPreview");

	if(from != -1.0 || until != -1.0) {
		if(lowLatency) {
			throw invalid_argument("--lowLatency is incompatible with --from and --until (can't seek in a real time stream)");
		}
		if(from != -1.0 && from < 0.0) {
			throw invalid_argument("If --from is enabled, it can't be negative.");
		}
		if(until != -1.0 && until < 0.0) {
			throw invalid_argument("If --until is enabled, it can't be negative.");
		}
		if(from != -1.0 && until != -1.0 && until <= from) {
			throw invalid_argument("When both are enabled, --from must be less than --until.");
		}
	}

	//Instantiate our classes.
	frameServer = new FrameServer(config, lowLatency);
	ffmpegDriver = new FFmpegDriver(frameServer, lowLatency, from, until, false);
	ffmpegDriver->openInputMedia(inVideo, AVMEDIA_TYPE_VIDEO, inVideoFormat, inVideoSize, "", inVideoRate, inVideoCodec, outAudioChannelMap, tryAudioInVideo);
	if(openInputAudio) {
		ffmpegDriver->openInputMedia(inAudio, AVMEDIA_TYPE_AUDIO, inAudioFormat, "", inAudioChannels, inAudioRate, inAudioCodec, outAudioChannelMap, true);
	}
	sdlDriver = new SDLDriver(config, frameServer, ffmpegDriver, headless, audioPreview && ffmpegDriver->getIsAudioInputPresent());
	// faceTracker = new FaceTracker(config, sdlDriver, frameServer, lowLatency);
	// faceMapper = new FaceMapper(config, sdlDriver, frameServer, faceTracker);
	metrics = new Metrics(config, "YerFace", true);
	// outputDriver = new OutputDriver(config, outData, frameServer, faceTracker, sdlDriver);
	// if(ffmpegDriver->getIsAudioInputPresent()) {
	// 	sphinxDriver = new SphinxDriver(config, frameServer, ffmpegDriver, sdlDriver, outputDriver, lowLatency);
	// }
	// eventLogger = new EventLogger(config, inEvents, outputDriver, frameServer, from);

	// outputDriver->setEventLogger(eventLogger);

	frameServer->onFrameStatusChangeEvent(FRAME_STATUS_NEW, tempeventhandler);
	frameServer->onFrameStatusChangeEvent(FRAME_STATUS_PROCESSING, tempeventhandler);
	frameServer->onFrameStatusChangeEvent(FRAME_STATUS_PREVIEWING, tempeventhandler);
	frameServer->onFrameStatusChangeEvent(FRAME_STATUS_LATE_PROCESSING, tempeventhandler);
	frameServer->onFrameStatusChangeEvent(FRAME_STATUS_DRAINING, tempeventhandler);
	frameServer->onFrameStatusChangeEvent(FRAME_STATUS_GONE, tempeventhandler);

	sdlWindowRenderer.window = NULL;
	sdlWindowRenderer.renderer = NULL;
	windowInitializationFailed = false;

	//Create locks.
	if((frameSizeMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((flipWorkingCompletedMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	//Create worker thread.
	if((workerThread = SDL_CreateThread(runCaptureLoop, "CaptureLoop", (void *)NULL)) == NULL) {
		throw runtime_error("Failed spawning worker thread!");
	}

	//Launch event / rendering loop.
	while(sdlDriver->getIsRunning()) {
		SDL_Delay(10); //FIXME - This doesn't have much of an impact on performance, but a better timing mechanism would be preferable.

		if(!headless && sdlWindowRenderer.window == NULL && !windowInitializationFailed) {
			YerFace_MutexLock(frameSizeMutex);
			if(!frameSizeValid) {
				YerFace_MutexUnlock(frameSizeMutex);
				continue;
			}
			try {
				sdlWindowRenderer = sdlDriver->createPreviewWindow(frameSize.width, frameSize.height);
			} catch(exception &e) {
				windowInitializationFailed = true;
				logger->error("Uh oh, failed to create a preview window! Got exception: %s", e.what());
				logger->warn("Continuing despite the lack of a preview window.");
			}
			YerFace_MutexUnlock(frameSizeMutex);
		}

		// if(!headless) {
		// 	bool previewFrameValid = false;
		// 	Mat previewFrame;
		// 	YerFace_MutexLock(flipWorkingCompletedMutex);
		// 	if(frameServer->getCompletedFrameSet()) {
		// 		previewFrame = doRenderPreviewFrame();
		// 		previewFrameValid = true;
		// 	}
		// 	YerFace_MutexUnlock(flipWorkingCompletedMutex);
		// 	if(previewFrameValid && sdlWindowRenderer.window != NULL) {
		// 		sdlDriver->doRenderPreviewFrame(previewFrame);
		// 	}
		// }

		sdlDriver->doHandleEvents();
	}

	//Join worker thread.
	SDL_WaitThread(workerThread, NULL);

	//Cleanup.
	SDL_DestroyMutex(frameSizeMutex);
	SDL_DestroyMutex(flipWorkingCompletedMutex);

	// delete eventLogger;
	// if(sphinxDriver != NULL) {
	// 	delete sphinxDriver;
	// }
	// delete outputDriver;
	delete metrics;
	// delete faceMapper;
	// delete faceTracker;
	delete frameServer;
	delete ffmpegDriver;
	delete sdlDriver;
	delete logger;
	return 0;
}

int runCaptureLoop(void *ptr) {
	VideoFrame videoFrame;

	ffmpegDriver->rollDemuxerThreads();

	bool didSetFrameSizeValid = false;
	while(sdlDriver->getIsRunning()) {
		if(!sdlDriver->getIsPaused()) {
			if(!ffmpegDriver->waitForNextVideoFrame(&videoFrame)) {
				logger->info("FFmpeg Demuxer thread finished.");
				sdlDriver->setIsRunning(false);
				frameServer->setDraining();
				continue;
			}

			if(!didSetFrameSizeValid) {
				YerFace_MutexLock(frameSizeMutex);
				if(!frameSizeValid) {
					frameSize = videoFrame.frameCV.size();
					frameSizeValid = true;
					didSetFrameSizeValid = true;
				}
				YerFace_MutexUnlock(frameSizeMutex);
			}

			// Start timer
			MetricsTick tick = metrics->startClock();

			frameServer->insertNewFrame(&videoFrame);
			ffmpegDriver->releaseVideoFrame(videoFrame);

			// eventLogger->startNewFrame();

			// faceTracker->processCurrentFrame();
			// faceMapper->processCurrentFrame();

			metrics->endClock(tick);

			while(sdlDriver->getIsPaused() && sdlDriver->getIsRunning()) {
				SDL_Delay(100);
			}

			YerFace_MutexLock(flipWorkingCompletedMutex);

			// frameServer->advanceWorkingFrameToCompleted();
			// faceTracker->advanceWorkingToCompleted();
			// faceMapper->advanceWorkingToCompleted();
			// if(sphinxDriver != NULL) {
			// 	sphinxDriver->advanceWorkingToCompleted();
			// }

			// eventLogger->handleCompletedFrame();
			// outputDriver->handleCompletedFrame();

			//If requested, write image sequence. Note this is VERY EXPENSIVE because it's blocking on the capture loop.
			// if(previewImgSeq.length() > 0) {
			// 	Mat previewFrame = doRenderPreviewFrame();

			// 	int filenameLength = previewImgSeq.length() + 32;
			// 	char filename[filenameLength];
			// 	snprintf(filename, filenameLength, "%s-%06lu.png", previewImgSeq.c_str(), workingFrameNumber);
			// 	logger->debug("YerFace writing preview frame to %s ...", filename);
			// 	imwrite(filename, previewFrame);
			// }

			YerFace_MutexUnlock(flipWorkingCompletedMutex);
		}
		SDL_Delay(0); //Be a good neighbor.
	}

	while(!frameServer->isDrained()) {
		SDL_Delay(10);
	}

	sdlDriver->stopAudioDriverNow();
	ffmpegDriver->stopAudioCallbacksNow();

	// if(sphinxDriver != NULL) {
	// 	sphinxDriver->drainPipelineDataNow();
	// }
	// outputDriver->drainPipelineDataNow();

	return 0;
}

// Mat doRenderPreviewFrame(void) {
// 	YerFace_MutexLock(flipWorkingCompletedMutex);

// 	// frameServer->resetCompletedPreviewFrame();

// 	// faceTracker->renderPreviewHUD();
// 	// faceMapper->renderPreviewHUD();
// 	// if(sphinxDriver != NULL) {
// 	// 	sphinxDriver->renderPreviewHUD();
// 	// }

// 	// Mat previewFrame = frameServer->getCompletedPreviewFrame().clone();

// 	// putText(previewFrame, metrics->getTimesString().c_str(), Point(25,50), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);
// 	// putText(previewFrame, metrics->getFPSString().c_str(), Point(25,75), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);

// 	YerFace_MutexUnlock(flipWorkingCompletedMutex);

// 	return previewFrame;
// }

void parseConfigFile(void) {
	logger->verbose("Opening and parsing config file: \"%s\"", configFile.c_str());
	std::ifstream fileStream = std::ifstream(configFile);
	if(fileStream.fail()) {
		throw invalid_argument("Specified config file failed to open.");
	}
	std::stringstream ssBuffer;
	ssBuffer << fileStream.rdbuf();
	config = json::parse(ssBuffer.str());
}
