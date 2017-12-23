
#include "opencv2/objdetect.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/tracking.hpp"

// best available resolution / rate: ffplay -pixel_format mjpeg -video_size 1920x1080 /dev/video1
// best recording solution: ffmpeg -framerate 30 -y -f video4linux2 -pixel_format mjpeg -video_size 1920x1080 -i /dev/video1 -f pulse -i default -acodec copy -vcodec copy /tmp/output.mkv
// alternate: mencoder tv:// -tv driver=v4l2:width=1920:height=1080:device=/dev/video1:fps=30:outfmt=mjpeg:forceaudio:alsa=1:adevice=default -ovc copy -oac copy -o /tmp/output.mkv

// an example pipe: ffmpeg -framerate 30 -f video4linux2 -pixel_format mjpeg -video_size 1280x720 -i /dev/video0 -vcodec copy -f avi pipe:1 | build/bin/yer-face --captureFile=- --frameDrop

#include "Logger.hpp"
#include "SDLDriver.hpp"
#include "FFmpegDriver.hpp"
#include "FaceTracker.hpp"
#include "FrameDerivatives.hpp"
#include "FaceMapper.hpp"
#include "Metrics.hpp"
#include "Utilities.hpp"

#include <iostream>
#include <cstdio>
#include <cstdlib>

using namespace std;
using namespace cv;
using namespace YerFace;

String captureFile;
String dlibFaceLandmarks;
String dlibFaceDetector;
String previewImgSeq;
bool frameDrop;
String window_name = "Yer Face: A Stupid Facial Performance Capture Engine";

SDLWindowRenderer sdlWindowRenderer;
SDL_Thread *workerThread;

Logger *logger;
SDLDriver *sdlDriver;
FFmpegDriver *ffmpegDriver;
FrameDerivatives *frameDerivatives;
FaceTracker *faceTracker;
FaceMapper *faceMapper;
Metrics *metrics;

unsigned long workingFrameNumber = 0;
SDL_mutex *flipWorkingCompletedMutex;

//VARIABLES PROTECTED BY frameSizeMutex
Size frameSize;
bool frameSizeValid = false;
SDL_mutex *frameSizeMutex;
//END VARIABLES PROTECTED BY frameSizeMutex

static int runCaptureLoop(void *ptr);
void doRenderPreviewFrame(void);

int main(int argc, const char** argv) {
	Logger::setLoggingFilter(SDL_LOG_PRIORITY_VERBOSE, SDL_LOG_CATEGORY_APPLICATION);
	logger = new Logger("YerFace");

	//Command line options.
	CommandLineParser parser(argc, argv,
		"{help h||Usage message.}"
		"{dlibFaceLandmarks|data/dlib-models/shape_predictor_68_face_landmarks.dat|Model for dlib's facial landmark detector.}"
		"{dlibFaceDetector|data/dlib-models/mmod_human_face_detector.dat|Model for dlib's DNN facial landmark detector or empty string (\"\") to default to the older HOG detector.}"
		"{captureFile|/dev/video0|Video file, URL, or device to open. (Or '-' for STDIN.)}"
		"{previewImgSeq||If set, is presumed to be the file name prefix of the output preview image sequence.}"
		"{frameDrop||If true, will drop frames as necessary to keep up with frames coming from the input device. (Don't use this if the input is a file!)}");

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
	captureFile = parser.get<string>("captureFile");
	if(captureFile == "-") {
		captureFile = "pipe:0";
	}
	previewImgSeq = parser.get<string>("previewImgSeq");
	dlibFaceLandmarks = parser.get<string>("dlibFaceLandmarks");
	dlibFaceDetector = parser.get<string>("dlibFaceDetector");
	frameDrop = parser.get<bool>("frameDrop");

	//Instantiate our classes.
	frameDerivatives = new FrameDerivatives();
	sdlDriver = new SDLDriver(frameDerivatives);
	ffmpegDriver = new FFmpegDriver(frameDerivatives, captureFile, frameDrop);
	faceTracker = new FaceTracker(dlibFaceLandmarks, dlibFaceDetector, sdlDriver, frameDerivatives, false);
	faceMapper = new FaceMapper(sdlDriver, frameDerivatives, faceTracker, false);
	metrics = new Metrics("YerFace", true);

	sdlWindowRenderer.window = NULL;
	sdlWindowRenderer.renderer = NULL;

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

		if(sdlWindowRenderer.window == NULL) {
			YerFace_MutexLock(frameSizeMutex);
			if(!frameSizeValid) {
				YerFace_MutexUnlock(frameSizeMutex);
				continue;
			}
			sdlWindowRenderer = sdlDriver->createPreviewWindow(frameSize.width, frameSize.height);
			YerFace_MutexUnlock(frameSizeMutex);
		}

		if(frameDerivatives->getCompletedFrameSet()) {
			doRenderPreviewFrame();
			sdlDriver->doRenderPreviewFrame();
		}

		sdlDriver->doHandleEvents();
	}

	//Join worker thread.
	SDL_WaitThread(workerThread, NULL);

	//Cleanup.
	SDL_DestroyMutex(frameSizeMutex);
	SDL_DestroyMutex(flipWorkingCompletedMutex);

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

			frameDerivatives->setWorkingFrame(videoFrame.frameCV);
			ffmpegDriver->releaseVideoFrame(videoFrame);

			faceTracker->processCurrentFrame();
			faceMapper->processCurrentFrame();

			metrics->endClock();

			YerFace_MutexLock(flipWorkingCompletedMutex);

			frameDerivatives->advanceWorkingFrameToCompleted();
			faceTracker->advanceWorkingToCompleted();
			faceMapper->advanceWorkingToCompleted();

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

	Mat previewFrame = frameDerivatives->getCompletedPreviewFrame();

	putText(previewFrame, metrics->getTimesString().c_str(), Point(25,50), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);
	putText(previewFrame, metrics->getFPSString().c_str(), Point(25,75), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);

	YerFace_MutexUnlock(flipWorkingCompletedMutex);
}
