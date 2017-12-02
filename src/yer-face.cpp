
#include "opencv2/objdetect.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/tracking.hpp"

// best available resolution / rate: ffplay -pixel_format mjpeg -video_size 1920x1080 /dev/video1
// best recording solution: ffmpeg -framerate 30 -y -f video4linux2 -pixel_format mjpeg -video_size 1920x1080 -i /dev/video1 -f pulse -i default -acodec copy -vcodec copy /tmp/output.mkv
// alternate: mencoder tv:// -tv driver=v4l2:width=1920:height=1080:device=/dev/video1:fps=30:outfmt=mjpeg:forceaudio:alsa=1:adevice=default -ovc copy -oac copy -o /tmp/output.mkv

#include "Logger.hpp"
#include "SDLDriver.hpp"
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

String capture_file;
String dlib_shape_predictor;
String prev_imgseq;
String window_name = "Performance Capture Tests";

SDLWindowRenderer sdlWindowRenderer;
SDL_Thread *workerThread;

Logger *logger;
SDLDriver *sdlDriver;
FrameDerivatives *frameDerivatives;
FaceTracker *faceTracker;
FaceMapper *faceMapper;
Metrics *metrics;

//VARIABLES PROTECTED BY frameMetadataMutex
unsigned long workingFrameNumber = 0, completedFrameNumber = 0;
Size frameSize;
bool frameSizeValid = false;
SDL_mutex *frameMetadataMutex, *flipWorkingCompletedMutex;

static int runCaptureLoop(void *ptr);
void doRenderPreviewFrame(void);

int main(int argc, const char** argv) {
	Logger::setLoggingFilter(SDL_LOG_PRIORITY_VERBOSE, SDL_LOG_CATEGORY_APPLICATION);
	logger = new Logger("YerFace");

	//Command line options.
	CommandLineParser parser(argc, argv,
		"{help h||Usage message.}"
		"{dlib_shape_predictor|data/dlib-shape-predictor/shape_predictor_68_face_landmarks.dat|Model for dlib's facial landmark detector.}"
		"{capture_file|/dev/video0|Video file or video capture device file to open.}"
		"{prev_imgseq||If set, is presumed to be the file name prefix of the output preview image sequence.}");

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
	capture_file = parser.get<string>("capture_file");
	dlib_shape_predictor = parser.get<string>("dlib_shape_predictor");
	prev_imgseq = parser.get<string>("prev_imgseq");

	//Instantiate our classes.
	frameDerivatives = new FrameDerivatives();
	sdlDriver = new SDLDriver(frameDerivatives);
	faceTracker = new FaceTracker(dlib_shape_predictor, sdlDriver, frameDerivatives);
	faceMapper = new FaceMapper(sdlDriver, frameDerivatives, faceTracker);
	metrics = new Metrics("YerFace", true);

	sdlWindowRenderer.window = NULL;
	sdlWindowRenderer.renderer = NULL;

	//Create locks.
	if((frameMetadataMutex = SDL_CreateMutex()) == NULL) {
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
		if(sdlWindowRenderer.window == NULL) {
			YerFace_MutexLock(frameMetadataMutex);
			if(!frameSizeValid) {
				YerFace_MutexUnlock(frameMetadataMutex);
				continue;
			}
			sdlWindowRenderer = sdlDriver->createPreviewWindow(frameSize.width, frameSize.height);
			YerFace_MutexUnlock(frameMetadataMutex);
		}

		doRenderPreviewFrame();

		sdlDriver->doHandleEvents();

		SDL_Delay(10);
	}

	//Join worker thread.
	SDL_WaitThread(workerThread, NULL);

	//Cleanup.
	SDL_DestroyMutex(frameMetadataMutex);
	SDL_DestroyMutex(flipWorkingCompletedMutex);

	delete metrics;
	delete faceMapper;
	delete faceTracker;
	delete frameDerivatives;
	delete sdlDriver;
	delete logger;
	return 0;
}

int runCaptureLoop(void *ptr) {
	//A couple of OpenCV classes.
	VideoCapture capture;
	Mat frame;

	//Open the video stream.
	capture.open(capture_file);
	if(!capture.isOpened()) {
		logger->error("Failed opening video stream");
		sdlDriver->setIsRunning(false);
		return -1;
	}

	while(sdlDriver->getIsRunning()) {
		if(!sdlDriver->getIsPaused()) {

			if(!capture.read(frame)) {
				logger->info("Capture is finished?");
				sdlDriver->setIsRunning(false);
				continue;
			}

			YerFace_MutexLock(frameMetadataMutex);
			workingFrameNumber++;
			if(!frameSizeValid) {
				frameSize = frame.size();
				frameSizeValid = true;
			}
			if(frame.empty()) {
				logger->error("Breaking on no frame ready...");
				YerFace_MutexUnlock(frameMetadataMutex);
				sdlDriver->setIsRunning(false);
				return -1;
			}
			YerFace_MutexUnlock(frameMetadataMutex);

			// Start timer
			metrics->startClock();

			frameDerivatives->setWorkingFrame(frame, workingFrameNumber);
			faceTracker->processCurrentFrame();
			faceMapper->processCurrentFrame();

			metrics->endClock();

			YerFace_MutexLock(flipWorkingCompletedMutex);

			YerFace_MutexLock(frameMetadataMutex);
			completedFrameNumber = workingFrameNumber;
			YerFace_MutexUnlock(frameMetadataMutex);
			
			frameDerivatives->advanceWorkingFrameToCompleted();
			faceTracker->advanceWorkingToCompleted();
			faceMapper->advanceWorkingToCompleted();

			YerFace_MutexUnlock(flipWorkingCompletedMutex);

			//If requested, write image sequence.
			if(prev_imgseq.length() > 0) {
				int filenameLength = prev_imgseq.length() + 32;
				char filename[filenameLength];
				snprintf(filename, filenameLength, "%s-%06lu.png", prev_imgseq.c_str(), completedFrameNumber);
				logger->debug("YerFace writing preview frame to %s ...", filename);
				imwrite(filename, frameDerivatives->getPreviewFrame());
			}
		}
	}
	capture.release();

	return 0;
}

void doRenderPreviewFrame(void) {
	YerFace_MutexLock(flipWorkingCompletedMutex);

	YerFace_MutexLock(frameMetadataMutex);
	unsigned long frameNumber = completedFrameNumber;
	YerFace_MutexUnlock(frameMetadataMutex);

	if(frameNumber < 1) {
		YerFace_MutexUnlock(flipWorkingCompletedMutex);
		return;
	}

	frameDerivatives->resetPreviewFrame();

	faceTracker->renderPreviewHUD();
	faceMapper->renderPreviewHUD();

	Mat previewFrame = frameDerivatives->getPreviewFrame();

	YerFace_MutexUnlock(flipWorkingCompletedMutex);

	putText(previewFrame, metrics->getTimesString().c_str(), Point(25,50), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);
	putText(previewFrame, metrics->getFPSString().c_str(), Point(25,75), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);

	sdlDriver->doRenderPreviewFrame();
}
