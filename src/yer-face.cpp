
#include "opencv2/objdetect.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/tracking.hpp"

#include "dlib/opencv.h"
#include "dlib/image_processing/frontal_face_detector.h"
#include "dlib/image_processing/render_face_detections.h"
#include "dlib/image_processing.h"


// best available resolution / rate: ffplay -pixel_format mjpeg -video_size 1920x1080 /dev/video1
// best recording solution: ffmpeg -framerate 30 -y -f video4linux2 -pixel_format mjpeg -video_size 1920x1080 -i /dev/video1 -f pulse -i default -acodec copy -vcodec copy /tmp/output.mkv
// alternate: mencoder tv:// -tv driver=v4l2:width=1920:height=1080:device=/dev/video1:fps=30:outfmt=mjpeg:forceaudio:alsa=1:adevice=default -ovc copy -oac copy -o /tmp/output.mkv

#include "FaceTracker.hpp"
#include "EyeTracker.hpp"
#include "FrameDerivatives.hpp"
#include "MarkerMapper.hpp"
#include "Metrics.hpp"

#include <iostream>
#include <cstdio>

using namespace std;
using namespace cv;
using namespace dlib;
using namespace YerFace;

String face_cascade_name;
String eyes_cascade_name;
String capture_file;
String dlib_shape_predictor;
String window_name = "Performance Capture Tests";

FrameDerivatives *frameDerivatives;
FaceTracker *faceTracker;
EyeTracker *eyeTrackerLeft;
EyeTracker *eyeTrackerRight;
MarkerMapper *markerMapper;
Metrics *metrics;
unsigned long frameNum = 0;

int main(int argc, const char** argv) {
	//Command line options.
	CommandLineParser parser(argc, argv,
		"{help h||Usage message.}"
		"{face_cascade|data/haarcascades/haarcascade_frontalface_default.xml|Model for detecting the face within the frame.}"
		"{eyes_cascade|data/haarcascades/haarcascade_eye.xml|Model for detecting eyes within the face.}"
		"{dlib_shape_predictor|data/dlib-shape-predictor/shape_predictor_5_face_landmarks.dat|Model for dlib's facial landmark detector.}"
		"{capture_file|/dev/video0|Video file or video capture device file to open.}");

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

	//A couple of OpenCV classes.
	face_cascade_name = parser.get<string>("face_cascade");
	eyes_cascade_name = parser.get<string>("eyes_cascade");
	capture_file = parser.get<string>("capture_file");
	dlib_shape_predictor = parser.get<string>("dlib_shape_predictor");
	VideoCapture capture;
	Mat frame;

	// ---DLIB---
	frontal_face_detector detector = get_frontal_face_detector();
	shape_predictor pose_model;
	deserialize(dlib_shape_predictor.c_str()) >> pose_model;

	//Instantiate our classes.
	frameDerivatives = new FrameDerivatives();
	faceTracker = new FaceTracker(face_cascade_name, frameDerivatives);
	eyeTrackerLeft = new EyeTracker(LeftEye, eyes_cascade_name, frameDerivatives, faceTracker);
	eyeTrackerRight = new EyeTracker(RightEye, eyes_cascade_name, frameDerivatives, faceTracker);
	markerMapper = new MarkerMapper(frameDerivatives, faceTracker, eyeTrackerLeft, eyeTrackerRight);
	metrics = new Metrics(30);

	//Open the video stream.
	capture.open(capture_file);
	if(!capture.isOpened()) {
		fprintf(stderr, "Failed opening video stream\n");
		return 1;
	}

	while(capture.read(frame)) {
		// Start timer
		metrics->startFrame();
		frameNum++;

		if(frame.empty()) {
			fprintf(stderr, "Breaking on no frame ready...\n");
			break;
		}

		frameDerivatives->setCurrentFrame(frame);

		Mat tempFrame;
		resize(frame, tempFrame, Size(), 0.25, 0.25);
		cv_image<bgr_pixel> dlibFrame(tempFrame);
		std::vector<dlib::rectangle> faces = detector(dlibFrame);
		std::vector<full_object_detection> shapes;
		for (unsigned long i = 0; i < faces.size(); ++i)
			shapes.push_back(pose_model(dlibFrame, faces[i]));
		
		faceTracker->processCurrentFrame();
		eyeTrackerLeft->processCurrentFrame();
		eyeTrackerRight->processCurrentFrame();
		markerMapper->processCurrentFrame();

		faceTracker->renderPreviewHUD();
		eyeTrackerLeft->renderPreviewHUD();
		eyeTrackerRight->renderPreviewHUD();
		markerMapper->renderPreviewHUD(false);

		metrics->endFrame();

		Mat previewFrame = frameDerivatives->getPreviewFrame();

		//fprintf(stderr, "Frame %lu %s, %s\n", frameNum, metricsStringA, metricsStringB);
		putText(previewFrame, metrics->getTimesString(), Point(25,50), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);
		putText(previewFrame, metrics->getFPSString(), Point(25,75), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);

		//Display preview frame.
		imshow(window_name, previewFrame);
		char c = (char)waitKey(1);
		if(c == 27) {
			fprintf(stderr, "Breaking on user escape...\n");
			break;
		}
	}

	delete metrics;
	delete markerMapper;
	delete eyeTrackerRight;
	delete eyeTrackerLeft;
	delete faceTracker;
	delete frameDerivatives;
	return 0;
}
