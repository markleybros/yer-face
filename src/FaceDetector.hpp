#pragma once

#include "Logger.hpp"
#include "Utilities.hpp"
#include "FrameServer.hpp"
#include "Metrics.hpp"

#include <list>

#include "dlib/opencv.h"
#include "dlib/dnn.h"
#include "dlib/image_processing/frontal_face_detector.h"
#include "dlib/image_processing/render_face_detections.h"
#include "dlib/image_processing.h"

using namespace std;

namespace YerFace {

class FaceDetector;

template <long num_filters, typename SUBNET> using con5d = dlib::con<num_filters,5,5,2,2,SUBNET>;
template <long num_filters, typename SUBNET> using con5  = dlib::con<num_filters,5,5,1,1,SUBNET>;

template <typename SUBNET> using downsampler  = dlib::relu<dlib::affine<con5d<32, dlib::relu<dlib::affine<con5d<32, dlib::relu<dlib::affine<con5d<16,SUBNET>>>>>>>>>;
template <typename SUBNET> using rcon5  = dlib::relu<dlib::affine<con5<45,SUBNET>>>;

using FaceDetectionModel = dlib::loss_mmod<dlib::con<1,9,9,1,1,rcon5<rcon5<rcon5<downsampler<dlib::input_rgb_image_pyramid<dlib::pyramid_down<6>>>>>>>>;

class FaceDetectorWorker {
public:
	int num;
	SDL_Thread *thread;
	FaceDetector *self;

	dlib::frontal_face_detector frontalFaceDetector;
	FaceDetectionModel faceDetectionModel;
};

class FacialDetectionBox {
public:
	Rect2d box;
	Rect2d boxNormalSize; //This is the scaled-up version to fit the native resolution of the frame.
	FrameTimestamps timestamps; //The timestamp (including frame number) to which this detection belongs.
	bool run; //Did the detector run?
	bool set; //Is the box valid?
};

class FaceDetector {
public:
	FaceDetector(json config, Status *myStatus, FrameServer *myFrameServer);
	~FaceDetector() noexcept(false);
	FacialDetectionBox getFacialDetection(FrameNumber frameNumber);
	void renderPreviewHUD(FrameNumber frameNumber, int density);
private:
	void doDetectFace(FaceDetectorWorker *worker, WorkingFrame *frame);
	static void handleFrameServerDrainedEvent(void *userdata);
	static void handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameNumber frameNumber);
	static int workerLoop(void *ptr);

	string faceDetectionModelFileName;
	double resultGoodForSeconds;
	double numWorkersPerCPU;

	bool usingDNNFaceDetection;

	int numWorkers;

	Status *status;
	FrameServer *frameServer;
	bool frameServerDrained;

	Metrics *metrics;

	string outputPrefix;

	Logger *logger;
	SDL_mutex *myMutex;
	SDL_cond *myCond;
	list<FrameNumber> workingFrameNumbers;

	SDL_mutex *detectionsMutex;
	unordered_map<FrameNumber, FacialDetectionBox> detections;
	FacialDetectionBox latestDetection;

	std::list<FaceDetectorWorker *> workers;
};

}; //namespace YerFace
