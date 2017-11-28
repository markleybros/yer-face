
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

Logger *logger;
SDLDriver *sdlDriver;
FrameDerivatives *frameDerivatives;
FaceTracker *faceTracker;
FaceMapper *faceMapper;
Metrics *metrics;
unsigned long frameNum = 0;

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

	//A couple of OpenCV classes.
	VideoCapture capture;
	Mat frame;

	//Instantiate our classes.
	sdlDriver = new SDLDriver();
	frameDerivatives = new FrameDerivatives();
	faceTracker = new FaceTracker(dlib_shape_predictor, frameDerivatives);
	faceMapper = new FaceMapper(frameDerivatives, faceTracker);
	metrics = new Metrics("YerFace", true);

	//Open the video stream.
	capture.open(capture_file);
	if(!capture.isOpened()) {
		logger->error("Failed opening video stream");
		return 1;
	}

	while(capture.read(frame)) {
		// Start timer
		metrics->startClock();
		frameNum++;

		if(frame.empty()) {
			logger->error("Breaking on no frame ready...");
			break;
		}

		frameDerivatives->setCurrentFrame(frame);
		faceTracker->processCurrentFrame();
		faceMapper->processCurrentFrame();

		faceTracker->renderPreviewHUD(false);
		faceMapper->renderPreviewHUD(false);

		metrics->endClock();

		Mat previewFrame = frameDerivatives->getPreviewFrame();

		putText(previewFrame, metrics->getTimesString(), Point(25,50), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);
		putText(previewFrame, metrics->getFPSString(), Point(25,75), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);

		//Display preview frame.
		imshow(window_name, previewFrame);
		char c = (char)waitKey(1);
		if(c == 27) {
			logger->info("Breaking on user escape...");
			break;
		}

		//If requested, write image sequence.
		if(prev_imgseq.length() > 0) {
			int filenameLength = prev_imgseq.length() + 32;
			char filename[filenameLength];
			snprintf(filename, filenameLength, "%s-%06lu.png", prev_imgseq.c_str(), frameNum);
			logger->debug("YerFace writing preview frame to %s ...", filename);
			imwrite(filename, previewFrame);
		}
	}

	delete metrics;
	delete faceMapper;
	delete faceTracker;
	delete frameDerivatives;
	delete sdlDriver;
	delete logger;
	return 0;
}
