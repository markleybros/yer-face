
#include "opencv2/objdetect.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/tracking.hpp"

// FIXME - these notes don't belong here... they should be reorganized and moved into README.md
// best available resolution / rate: ffplay -pixel_format mjpeg -video_size 1920x1080 /dev/video1
// best recording solution: ffmpeg -framerate 30 -y -f video4linux2 -pixel_format mjpeg -video_size 1920x1080 -i /dev/video1 -f pulse -i default -acodec copy -vcodec copy /tmp/output.mkv
// alternate: mencoder tv:// -tv driver=v4l2:width=1920:height=1080:device=/dev/video1:fps=30:outfmt=mjpeg:forceaudio:alsa=1:adevice=default -ovc copy -oac copy -o /tmp/output.mkv

// On a sufficiently fast system (with CUDA hardware) and which has a sufficiently well-endowed camera, you can do something like this:
// ffmpeg -framerate 60 -f video4linux2 -pixel_format mjpeg -video_size 1920x1080 -i /dev/video0 -vcodec copy -f avi pipe:1 | build/bin/yer-face --captureFile=-

// On my laptop, however, which is not so well-endowed, we need to do something more like this:
// ffmpeg -framerate 30 -f video4linux2 -pixel_format mjpeg -video_size 1280x720 -i /dev/video0 -vcodec copy -f avi pipe:1 | build/bin/yer-face --captureFile=- --frameDrop

// Colors for yellow florescent paint:
// INFO: MarkerSeparator: doEyedropper: Updated HSV color range to: <56.00, 104.00, 65.00> - <96.00, 255.00, 239.00>

#include "Logger.hpp"
#include "SDLDriver.hpp"
#include "FFmpegDriver.hpp"
#include "FaceTracker.hpp"
#include "FrameDerivatives.hpp"
#include "FaceMapper.hpp"
#include "Metrics.hpp"
#include "Utilities.hpp"
#include "OutputDriver.hpp"
#include "SphinxDriver.hpp"

#include <iostream>
#include <sstream>
#include <cstdio>
#include <cstdlib>

using namespace std;
using namespace cv;
using namespace YerFace;

String configFile;
String captureFile;
String previewImgSeq;
bool frameDrop;
bool audioPreview;
String window_name = "Yer Face: A Stupid Facial Performance Capture Engine";

json config = NULL;

SDLWindowRenderer sdlWindowRenderer;
bool windowInitializationFailed;
SDL_Thread *workerThread;

Logger *logger = NULL;
SDLDriver *sdlDriver = NULL;
FFmpegDriver *ffmpegDriver = NULL;
FrameDerivatives *frameDerivatives = NULL;
FaceTracker *faceTracker = NULL;
FaceMapper *faceMapper = NULL;
Metrics *metrics = NULL;
OutputDriver *outputDriver = NULL;
SphinxDriver *sphinxDriver = NULL;

unsigned long workingFrameNumber = 0;
SDL_mutex *flipWorkingCompletedMutex;

//VARIABLES PROTECTED BY frameSizeMutex
Size frameSize;
bool frameSizeValid = false;
SDL_mutex *frameSizeMutex;
//END VARIABLES PROTECTED BY frameSizeMutex

static int runCaptureLoop(void *ptr);
void doRenderPreviewFrame(void);
void parseConfigFile(void);

int main(int argc, const char** argv) {
	Logger::setLoggingFilter(SDL_LOG_PRIORITY_VERBOSE, SDL_LOG_CATEGORY_APPLICATION);
	logger = new Logger("YerFace");

	//Command line options.
	CommandLineParser parser(argc, argv,
		"{help h||Usage message.}"
		"{configFile C|data/config.json|Required configuration file.}"
		"{captureFile|/dev/video0|Video file, URL, or device to open. (Or '-' for STDIN.)}"
		"{previewImgSeq||If set, is presumed to be the file name prefix of the output preview image sequence.}"
		"{frameDrop||If true, will drop frames as necessary to keep up with frames coming from the input device. (Don't use this if the input is a file!)}"
		"{audioPreview||If true, will preview processed audio out the computer's sound device.}"
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
	captureFile = parser.get<string>("captureFile");
	if(captureFile == "-") {
		captureFile = "pipe:0";
	}
	previewImgSeq = parser.get<string>("previewImgSeq");
	frameDrop = parser.get<bool>("frameDrop");
	audioPreview = parser.get<bool>("audioPreview");

	//Instantiate our classes.
	frameDerivatives = new FrameDerivatives(config);
	ffmpegDriver = new FFmpegDriver(frameDerivatives, captureFile, frameDrop);
	sdlDriver = new SDLDriver(frameDerivatives, ffmpegDriver, audioPreview && ffmpegDriver->getIsAudioInputPresent());
	faceTracker = new FaceTracker(config, sdlDriver, frameDerivatives);
	faceMapper = new FaceMapper(config, sdlDriver, frameDerivatives, faceTracker);
	metrics = new Metrics(config, "YerFace", frameDerivatives, true);
	outputDriver = new OutputDriver(config, frameDerivatives, faceTracker, sdlDriver);
	if(ffmpegDriver->getIsAudioInputPresent()) {
		sphinxDriver = new SphinxDriver(config, frameDerivatives, ffmpegDriver);
	}
	ffmpegDriver->rollDemuxerThread();

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
		SDL_Delay(10);

		if(sdlWindowRenderer.window == NULL && !windowInitializationFailed) {
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

		if(frameDerivatives->getCompletedFrameSet()) {
			doRenderPreviewFrame();
			if(sdlWindowRenderer.window != NULL) {
				sdlDriver->doRenderPreviewFrame();
			}
		}

		sdlDriver->doHandleEvents();
	}

	//Join worker thread.
	SDL_WaitThread(workerThread, NULL);

	//Cleanup.
	SDL_DestroyMutex(frameSizeMutex);
	SDL_DestroyMutex(flipWorkingCompletedMutex);

	if(sphinxDriver != NULL) {
		delete sphinxDriver;
	}
	delete outputDriver;
	delete metrics;
	delete faceMapper;
	delete faceTracker;
	delete frameDerivatives;
	delete ffmpegDriver;
	delete sdlDriver;
	delete logger;
	return 0;
}

int runCaptureLoop(void *ptr) {
	VideoFrame videoFrame;

	bool didSetFrameSizeValid = false;
	while(sdlDriver->getIsRunning()) {
		if(!sdlDriver->getIsPaused()) {
			if(!ffmpegDriver->waitForNextVideoFrame(&videoFrame)) {
				logger->info("FFmpeg Demuxer thread finished.");
				sdlDriver->setIsRunning(false);
				continue;
			}
			workingFrameNumber++;

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
			metrics->startClock();

			frameDerivatives->setWorkingFrame(videoFrame.frameCV, videoFrame.timestamp);
			ffmpegDriver->releaseVideoFrame(videoFrame);

			faceTracker->processCurrentFrame();
			faceMapper->processCurrentFrame();

			metrics->endClock();

			while(sdlDriver->getIsPaused() && sdlDriver->getIsRunning()) {
				SDL_Delay(100);
			}

			YerFace_MutexLock(flipWorkingCompletedMutex);

			frameDerivatives->advanceWorkingFrameToCompleted();
			faceTracker->advanceWorkingToCompleted();
			faceMapper->advanceWorkingToCompleted();
			if(sphinxDriver != NULL) {
				sphinxDriver->advanceWorkingToCompleted();
			}

			outputDriver->handleCompletedFrame();

			//If requested, write image sequence.
			if(previewImgSeq.length() > 0) {
				doRenderPreviewFrame();

				int filenameLength = previewImgSeq.length() + 32;
				char filename[filenameLength];
				snprintf(filename, filenameLength, "%s-%06lu.png", previewImgSeq.c_str(), workingFrameNumber);
				logger->debug("YerFace writing preview frame to %s ...", filename);
				imwrite(filename, frameDerivatives->getCompletedPreviewFrame());
			}

			YerFace_MutexUnlock(flipWorkingCompletedMutex);
		}
	}

	return 0;
}

void doRenderPreviewFrame(void) {
	YerFace_MutexLock(flipWorkingCompletedMutex);

	frameDerivatives->resetCompletedPreviewFrame();

	faceTracker->renderPreviewHUD();
	faceMapper->renderPreviewHUD();
	if(sphinxDriver != NULL) {
		sphinxDriver->renderPreviewHUD();
	}

	Mat previewFrame = frameDerivatives->getCompletedPreviewFrame();

	putText(previewFrame, metrics->getTimesString().c_str(), Point(25,50), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);
	putText(previewFrame, metrics->getFPSString().c_str(), Point(25,75), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);

	YerFace_MutexUnlock(flipWorkingCompletedMutex);
}

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
