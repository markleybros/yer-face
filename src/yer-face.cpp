#include "opencv2/objdetect.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/tracking.hpp"

// best available resolution / rate: ffplay -pixel_format mjpeg -video_size 1920x1080 /dev/video1
// best recording solution: ffmpeg -framerate 30 -y -f video4linux2 -pixel_format mjpeg -video_size 1920x1080 -i /dev/video0 -f pulse -i default -acodec copy -vcodec copy /tmp/output.mkv

#include "FaceTracker.hpp"
#include "FrameDerivatives.hpp"


#include <iostream>
#include <stdio.h>
#include <list>

using namespace std;
using namespace cv;
using namespace YerFace;

String face_cascade_name;
String eyes_cascade_name;
String capture_file;
String window_name = "Performance Capture Tests";

double timer = -1;
list<double> frameTimes;

double imageScaleFactor = 0.5;

FrameDerivatives *frameDerivatives;
FaceTracker *faceTracker;

int main( int argc, const char** argv ) {
	//Command line options.
	CommandLineParser parser(argc, argv,
		"{help h||}"
		"{face_cascade|/usr/local/share/OpenCV/haarcascades/haarcascade_frontalface_default.xml|}"
		"{eyes_cascade|/usr/local/share/OpenCV/haarcascades/haarcascade_eye.xml|}"
		"{capture_file|/dev/video0|}");

	parser.about("Yer Face: The butt of all the jokes. (A stupid facial performance capture engine for cartoon animation.)");
	if(parser.get<bool>("help")) {
		parser.printMessage();
		return 1;
	}

	//A couple of OpenCV classes.
	face_cascade_name = parser.get<string>("face_cascade");
	eyes_cascade_name = parser.get<string>("eyes_cascade");
	capture_file = parser.get<string>("capture_file");
	VideoCapture capture;
	Mat frame;

	//Instantiate our classes.
	frameDerivatives = new FrameDerivatives(imageScaleFactor);
	faceTracker = new FaceTracker(face_cascade_name, frameDerivatives);

	//Open the video stream.
	capture.open(capture_file);
	if(!capture.isOpened()) {
		fprintf(stderr, "Failed opening video stream\n");
		return 1;
	}

	while(capture.read(frame)) {
		// Start timer
		timer = (double)getTickCount();
		if(frame.empty()) {
			fprintf(stderr, "Breaking on no frame ready...");
			break;
		}

		frameDerivatives->setCurrentFrame(frame);
		faceTracker->processCurrentFrame();
		faceTracker->renderPreviewHUD();

		// Calculate Frames per second (FPS)
		timer = ((double)getTickCount() - timer) / getTickFrequency();
		frameTimes.push_front(timer);
		double averageTime = 0.0;
		double worstTime = 0.0;
		int numTimes = 0;
		for(double frameTime : frameTimes) {
			averageTime = averageTime + frameTime;
			if(frameTime > worstTime) {
				worstTime = frameTime;
			}
			numTimes++;
		}
		averageTime = averageTime / (double)numTimes;
		while(frameTimes.size() > 30) {
			frameTimes.pop_back();
		}
		Mat previewFrame = frameDerivatives->getPreviewFrame();

		//Display some metrics on frame.
		char metrics[256];
		sprintf(metrics, "Times: <Avg %.02fms, Worst %.02fms>", averageTime * 1000.0, worstTime * 1000.0);
		putText(previewFrame, metrics, Point(25,50), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);

		//Display preview frame.
		imshow(window_name, previewFrame);
		char c = (char)waitKey(1);
		if(c == 27) {
			fprintf(stderr, "Breaking on user escape...");
			break;
		}
	}
	return 0;
}
