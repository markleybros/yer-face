
#include "FaceDetector.hpp"
#include "Utilities.hpp"

#include "dlib/opencv.h"
#include "dlib/dnn.h"
#include "dlib/image_processing/frontal_face_detector.h"
#include "dlib/image_processing/render_face_detections.h"
#include "dlib/image_processing.h"

#include <math.h>

using namespace std;
using namespace dlib;
using namespace cv;

namespace YerFace {

template <long num_filters, typename SUBNET> using con5d = dlib::con<num_filters,5,5,2,2,SUBNET>;
template <long num_filters, typename SUBNET> using con5  = dlib::con<num_filters,5,5,1,1,SUBNET>;

template <typename SUBNET> using downsampler  = dlib::relu<dlib::affine<con5d<32, dlib::relu<dlib::affine<con5d<32, dlib::relu<dlib::affine<con5d<16,SUBNET>>>>>>>>>;
template <typename SUBNET> using rcon5  = dlib::relu<dlib::affine<con5<45,SUBNET>>>;

using FaceDetectionModel = dlib::loss_mmod<dlib::con<1,9,9,1,1,rcon5<rcon5<rcon5<downsampler<dlib::input_rgb_image_pyramid<dlib::pyramid_down<6>>>>>>>>;

class FaceDetectorWorker {
public:
	FaceDetector *self;

	dlib::frontal_face_detector frontalFaceDetector;
	FaceDetectionModel faceDetectionModel;
};

FaceDetector::FaceDetector(json config, Status *myStatus, FrameServer *myFrameServer) {
	detectionWorkerPool = NULL;
	assignmentWorkerPool = NULL;
	status = myStatus;
	if(status == NULL) {
		throw invalid_argument("status cannot be NULL");
	}
	frameServer = myFrameServer;
	if(frameServer == NULL) {
		throw invalid_argument("frameServer cannot be NULL");
	}
	logger = new Logger("FaceDetector");
	if((detectionsMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((myAssignmentMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	metrics = new Metrics(config, "FaceDetector.Detections");
	assignmentMetrics = new Metrics(config, "FaceDetector.Assignments");
	resultGoodForSeconds = config["YerFace"]["FaceDetector"]["resultGoodForSeconds"];
	if(resultGoodForSeconds < 0.0) {
		throw invalid_argument("resultGoodForSeconds cannot be less than zero.");
	}
	faceDetectionModelFileName = Utilities::fileValidPathOrDie(config["YerFace"]["FaceDetector"]["dlibFaceDetector"]);
	faceBoxSizeAdjustment = config["YerFace"]["FaceDetector"]["faceBoxSizeAdjustment"];
	if(faceBoxSizeAdjustment < 0.0) {
		throw invalid_argument("faceBoxSizeAdjustment cannot be less than zero.");
	}

	if(faceDetectionModelFileName.length() > 0) {
		usingDNNFaceDetection = true;
	} else {
		usingDNNFaceDetection = false;
	}
	latestDetection.run = false;
	latestDetection.set = false;
	latestDetectionLostWarning = false;

	//Hook into the frame lifecycle.

	//We want to know when any frame has entered various statuses.
	FrameStatusChangeEventCallback frameStatusChangeCallback;
	frameStatusChangeCallback.userdata = (void *)this;
	frameStatusChangeCallback.callback = handleFrameStatusChange;
	frameStatusChangeCallback.newStatus = FRAME_STATUS_NEW;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_DETECTION;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_GONE;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);

	//We also want to introduce a checkpoint so that frames cannot TRANSITION AWAY from FRAME_STATUS_DETECTION without our blessing.
	frameServer->registerFrameStatusCheckpoint(FRAME_STATUS_DETECTION, "faceDetector.ran");

	WorkerPoolParameters workerPoolParameters;
	workerPoolParameters.name = "FaceDetector.Detect";
	workerPoolParameters.numWorkers = config["YerFace"]["FaceDetector"]["numWorkers"];
	workerPoolParameters.numWorkersPerCPU = config["YerFace"]["FaceDetector"]["numWorkersPerCPU"];
	workerPoolParameters.initializer = detectionWorkerInitializer;
	workerPoolParameters.deinitializer = NULL;
	workerPoolParameters.usrPtr = (void *)this;
	workerPoolParameters.handler = detectionWorkerHandler;
	detectionWorkerPool = new WorkerPool(config, status, frameServer, workerPoolParameters);

	workerPoolParameters.name = "FaceDetector.Assign";
	workerPoolParameters.numWorkers = 1;
	workerPoolParameters.numWorkersPerCPU = 0.0;
	workerPoolParameters.initializer = NULL;
	workerPoolParameters.deinitializer = NULL;
	workerPoolParameters.usrPtr = (void *)this;
	workerPoolParameters.handler = assignmentWorkerHandler;
	assignmentWorkerPool = new WorkerPool(config, status, frameServer, workerPoolParameters);

	logger->debug1("FaceDetector object constructed with Face Detection Method: %s", usingDNNFaceDetection ? "DNN" : "HOG");
}

FaceDetector::~FaceDetector() noexcept(false) {
	logger->debug1("FaceDetector object destructing...");

	delete detectionWorkerPool;
	delete assignmentWorkerPool;

	if(assignmentFrameNumbers.size() > 0) {
		logger->err("Assignment Frames are still pending! Woe is me!");
	}
	if(detectionTasks.size() > 0) {
		logger->err("Detection Tasks are still pending! Woe is me!");
	}

	SDL_DestroyMutex(myMutex);
	SDL_DestroyMutex(myAssignmentMutex);
	SDL_DestroyMutex(detectionsMutex);
	delete logger;
	delete assignmentMetrics;
	delete metrics;
}

FacialDetectionBox FaceDetector::getFacialDetection(FrameNumber frameNumber) {
	FacialDetectionBox detection;
	YerFace_MutexLock(detectionsMutex);
	detection = detections[frameNumber];
	YerFace_MutexUnlock(detectionsMutex);
	return detection;
}

void FaceDetector::renderPreviewHUD(Mat previewFrame, FrameNumber frameNumber, int density) {
	YerFace_MutexLock(detectionsMutex);
	FacialDetectionBox detection = detections[frameNumber];
	YerFace_MutexUnlock(detectionsMutex);

	if(density > 1) {
		if(detection.set) {
			cv::rectangle(previewFrame, detection.boxNormalSize, Scalar(255, 255, 0), 1);
		}
	}
}

void FaceDetector::doDetectFace(WorkerPoolWorker *workerPoolWorker, FaceDetectionTask task) {
	FaceDetectorWorker *worker = (FaceDetectorWorker *)workerPoolWorker->ptr;
	dlib::cv_image<dlib::bgr_pixel> dlibDetectionFrame = cv_image<bgr_pixel>(task.detectionFrame);
	std::vector<dlib::rectangle> faces;

	if(usingDNNFaceDetection) {
		//Using dlib's CNN-based face detector which can (optimistically) be pushed out to the GPU
		dlib::matrix<dlib::rgb_pixel> imageMatrix;
		dlib::assign_image(imageMatrix, dlibDetectionFrame);
		std::vector<dlib::mmod_rect> detections = worker->faceDetectionModel(imageMatrix);
		for(dlib::mmod_rect detection : detections) {
			faces.push_back(detection.rect);
		}
	} else {
		//Using dlib's built-in HOG face detector instead of a CNN-based detector
		faces = worker->frontalFaceDetector(dlibDetectionFrame);
	}

	bool bestFaceSet = false;
	int bestFaceArea = -1;
	Size2d detectionFrameSize = task.detectionFrame.size();
	Rect2d detectionFrameBox = Rect2d(0.0, 0.0, detectionFrameSize.width, detectionFrameSize.height);
	Rect2d bestFaceBox, bestFaceBoxNormalSize;
	for(dlib::rectangle face : faces) {
		if((int)face.area() > bestFaceArea) {
			bestFaceSet = true;
			bestFaceArea = face.area();
			bestFaceBox.x = face.left();
			bestFaceBox.y = face.top();
			bestFaceBox.width = face.right() - bestFaceBox.x;
			bestFaceBox.height = face.bottom() - bestFaceBox.y;
			bestFaceBox = Utilities::insetBox(bestFaceBox, faceBoxSizeAdjustment) & detectionFrameBox;
			bestFaceBoxNormalSize = Utilities::scaleRect(bestFaceBox, 1.0 / task.myDetectionScaleFactor);
		}
	}
	FacialDetectionBox detection;
	detection.timestamps = task.myFrameTimestamps;
	detection.run = true;
	detection.set = false;
	if(bestFaceSet) {
		detection.box = bestFaceBox;
		detection.boxNormalSize = bestFaceBoxNormalSize;
		detection.set = true;
	}

	logger->debug4("==== WORKER #%d FINISHED DETECTION FOR FRAME #" YERFACE_FRAMENUMBER_FORMAT, workerPoolWorker->num, detection.timestamps.frameNumber);

	bool resultUsed = false;
	YerFace_MutexLock(detectionsMutex);
	if(!latestDetection.run || latestDetection.timestamps.startTimestamp < detection.timestamps.startTimestamp) {
		latestDetection = detection;
		resultUsed = true;
		if(!latestDetection.set) {
			if(!latestDetectionLostWarning) {
				logger->notice("Lost face completely! Will keep searching...");
				latestDetectionLostWarning = true;
			}
		} else {
			latestDetectionLostWarning = false;
		}
	}
	YerFace_MutexUnlock(detectionsMutex);

	if(resultUsed && assignmentWorkerPool != NULL) {
		assignmentWorkerPool->sendWorkerSignal();
	} else {
		logger->notice("Detection performed, but it was of no use!");
	}
}

void FaceDetector::handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps) {
	FrameNumber frameNumber = frameTimestamps.frameNumber;
	FaceDetector *self = (FaceDetector *)userdata;
	self->logger->debug4("Handling Frame Status Change for Frame Number " YERFACE_FRAMENUMBER_FORMAT " to Status %d", frameNumber, newStatus);
	FacialDetectionBox detection;
	FaceDetectorAssignmentTask assignment;
	switch(newStatus) {
		default:
			throw logic_error("Handler passed unsupported frame status change event!");
		case FRAME_STATUS_NEW:
			detection.run = false;
			detection.set = false;
			YerFace_MutexLock(self->detectionsMutex);
			self->detections[frameNumber] = detection;
			YerFace_MutexUnlock(self->detectionsMutex);
			assignment.frameNumber = frameNumber;
			assignment.readyForAssignment = false;
			YerFace_MutexLock(self->myAssignmentMutex);
			self->assignmentFrameNumbers[frameNumber] = assignment;
			YerFace_MutexUnlock(self->myAssignmentMutex);
			break;
		case FRAME_STATUS_DETECTION:
			YerFace_MutexLock(self->myAssignmentMutex);
			self->assignmentFrameNumbers[frameNumber].readyForAssignment = true;
			self->logger->debug4("handleFrameStatusChange() Frame #" YERFACE_FRAMENUMBER_FORMAT " waiting on me. Queue depth is now %lu", frameNumber, self->assignmentFrameNumbers.size());
			YerFace_MutexUnlock(self->myAssignmentMutex);
			if(self->assignmentWorkerPool != NULL) {
				self->assignmentWorkerPool->sendWorkerSignal();
			}
			break;
		case FRAME_STATUS_GONE:
			YerFace_MutexLock(self->detectionsMutex);
			self->detections.erase(frameNumber);
			YerFace_MutexUnlock(self->detectionsMutex);
			break;
	}
}

void FaceDetector::detectionWorkerInitializer(WorkerPoolWorker *worker, void *ptr) {
	FaceDetector *self = (FaceDetector *)ptr;
	FaceDetectorWorker *innerWorker = new FaceDetectorWorker();
	innerWorker->self = self;
	if(self->usingDNNFaceDetection) {
		deserialize(self->faceDetectionModelFileName.c_str()) >> innerWorker->faceDetectionModel;
	} else {
		innerWorker->frontalFaceDetector = get_frontal_face_detector();
	}
	worker->ptr = (void *)innerWorker;
}

bool FaceDetector::detectionWorkerHandler(WorkerPoolWorker *worker) {
	FaceDetectorWorker *innerWorker = (FaceDetectorWorker *)worker->ptr;
	FaceDetector *self = innerWorker->self;
	bool didWork = false;

	//// CHECK FOR WORK ////
	bool taskSet = false;
	FaceDetectionTask task;
	YerFace_MutexLock(self->myMutex);
	if(self->detectionTasks.size() > 0) {
		taskSet = true;
		//Operate on the back of detectionTasks (not a FIFO queue!) because the most recent detection task is always the most urgent.
		task = self->detectionTasks.back();
		self->detectionTasks.clear();
	}
	YerFace_MutexUnlock(self->myMutex);

	//// DO THE WORK ////
	if(taskSet) {
		self->logger->debug4("Thread #%d handling frame #" YERFACE_FRAMENUMBER_FORMAT, worker->num, task.myFrameNumber);
		MetricsTick tick = self->metrics->startClock();

		// self->logger->verbose("Thread #%d, Frame #" YERFACE_FRAMENUMBER_FORMAT " - RUNNING Detection", worker->num, task.myFrameNumber);
		self->doDetectFace(worker, task);
		// self->logger->verbose("Thread #%d, Frame #" YERFACE_FRAMENUMBER_FORMAT " - FINISHED Detection", worker->num, task.myFrameNumber);

		self->metrics->endClock(tick);
		didWork = true;
	}
	return didWork;
}

bool FaceDetector::assignmentWorkerHandler(WorkerPoolWorker *worker) {
	FaceDetector *self = (FaceDetector *)worker->ptr;
	bool didWork = false;

	static FrameNumber lastFrameNumber = -1;
	static FrameNumber myFrameNumber = -1;
	static FrameNumber lastDetectionRequested = -1;
	static FrameNumber lastFrameBlockedWarning = -1;
	static MetricsTick tick;

	YerFace_MutexLock(self->myAssignmentMutex);
	//// CHECK FOR WORK ////
	if(myFrameNumber < 0) {
		for(auto pendingAssignmentPair : self->assignmentFrameNumbers) {
			if(myFrameNumber < 0 || pendingAssignmentPair.first < myFrameNumber) {
				myFrameNumber = pendingAssignmentPair.first;
			}
		}
		if(myFrameNumber > 0 && !self->assignmentFrameNumbers[myFrameNumber].readyForAssignment) {
			self->logger->debug4("BLOCKED on frame " YERFACE_FRAMENUMBER_FORMAT " because it is not ready!", myFrameNumber);
			myFrameNumber = -1;
		}
		if(myFrameNumber > 0) {
			tick = self->assignmentMetrics->startClock();
			self->assignmentFrameNumbers.erase(myFrameNumber);
		}
	}
	YerFace_MutexUnlock(self->myAssignmentMutex);

	//// DO THE WORK ////
	if(myFrameNumber > 0) {
		self->logger->debug4("Assignment Thread handling frame #" YERFACE_FRAMENUMBER_FORMAT, myFrameNumber);
		if(myFrameNumber <= lastFrameNumber) {
			throw logic_error("FaceDetector handling frames out of order!");
		}

		WorkingFrame *workingFrame = self->frameServer->getWorkingFrame(myFrameNumber);
		FrameTimestamps myFrameTimestamps = workingFrame->frameTimestamps;

		bool frameAssigned = false;
		YerFace_MutexLock(self->detectionsMutex);
		if(self->latestDetection.run) {
			double latestDetectionUsableUntil = self->latestDetection.timestamps.startTimestamp + self->resultGoodForSeconds;
			if(myFrameTimestamps.startTimestamp <= latestDetectionUsableUntil) {
				self->detections[myFrameNumber] = self->latestDetection;
				frameAssigned = true;
				// self->logger->verbose("==== SUCCESSFUL ASSIGNMENT ON FRAME #" YERFACE_FRAMENUMBER_FORMAT " (LD Frame #" YERFACE_FRAMENUMBER_FORMAT ")", myFrameNumber, self->latestDetection.timestamps.frameNumber);
			}
		}
		YerFace_MutexUnlock(self->detectionsMutex);

		if(myFrameNumber != lastDetectionRequested) {
			// self->logger->verbose("==== REQUESTING A DETECTION ON FRAME #" YERFACE_FRAMENUMBER_FORMAT, myFrameNumber);
			lastDetectionRequested = myFrameNumber;
			FaceDetectionTask task;
			task.myFrameNumber = myFrameNumber;
			task.myFrameTimestamps = myFrameTimestamps;
			task.myDetectionScaleFactor = workingFrame->detectionScaleFactor;
			task.detectionFrame = workingFrame->detectionFrame.clone();
			YerFace_MutexLock(self->myMutex);
			self->detectionTasks.push_back(task);
			YerFace_MutexUnlock(self->myMutex);
			if(self->detectionWorkerPool != NULL) {
				self->detectionWorkerPool->sendWorkerSignal();
			}
		}

		if(frameAssigned) {
			lastFrameNumber = myFrameNumber;
			self->frameServer->setWorkingFrameStatusCheckpoint(myFrameNumber, FRAME_STATUS_DETECTION, "faceDetector.ran");
			self->assignmentMetrics->endClock(tick);
			myFrameNumber = -1;
			didWork = true;
		} else {
			if(lastFrameBlockedWarning != myFrameNumber) {
				self->logger->warning("Uh-oh! We are blocked on a Face Detection Task for frame #" YERFACE_FRAMENUMBER_FORMAT ". If this happens a lot, consider some tuning.", myFrameNumber);
				lastFrameBlockedWarning = myFrameNumber;
			}
		}
	}
	return didWork;
}

} //namespace YerFace
