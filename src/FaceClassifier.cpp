
#include "FaceClassifier.hpp"
#include "Utilities.hpp"

#include <math.h>

using namespace std;
using namespace dlib;

namespace YerFace {

FaceClassifier::FaceClassifier(json config, Status *myStatus, FrameServer *myFrameServer) {
	status = myStatus;
	if(status == NULL) {
		throw invalid_argument("status cannot be NULL");
	}
	frameServer = myFrameServer;
	if(frameServer == NULL) {
		throw invalid_argument("frameServer cannot be NULL");
	}
	logger = new Logger("FaceClassifier");
	if((classificationsMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((myCond = SDL_CreateCond()) == NULL) {
		throw runtime_error("Failed creating condition!");
	}
	metrics = new Metrics(config, "FaceClassifier", true);
	numWorkers = config["YerFace"]["FaceClassifier"]["numWorkers"];
	if(numWorkers < 0.0) {
		throw invalid_argument("numWorkers is nonsense.");
	}
	numWorkersPerCPU = config["YerFace"]["FaceClassifier"]["numWorkersPerCPU"];
	if(numWorkersPerCPU < 0.0) {
		throw invalid_argument("numWorkersPerCPU is nonsense.");
	}
	resultGoodForSeconds = config["YerFace"]["FaceClassifier"]["resultGoodForSeconds"];
	if(resultGoodForSeconds < 0.0) {
		throw invalid_argument("resultGoodForSeconds cannot be less than zero.");
	}
	faceDetectionModelFileName = config["YerFace"]["FaceClassifier"]["dlibFaceDetector"];

	if(faceDetectionModelFileName.length() > 0) {
		usingDNNFaceDetection = true;
	} else {
		usingDNNFaceDetection = false;
	}
	latestClassification.run = false;
	latestClassification.set = false;

	//Hook into the frame lifecycle.

	//We need to know when the frame server has drained.
	frameServerDrained = false;
	FrameServerDrainedEventCallback frameServerDrainedCallback;
	frameServerDrainedCallback.userdata = (void *)this;
	frameServerDrainedCallback.callback = handleFrameServerDrainedEvent;
	frameServer->onFrameServerDrainedEvent(frameServerDrainedCallback);

	//We want to know when any frame has entered PROCESSING.
	FrameStatusChangeEventCallback frameStatusChangeCallback;
	frameStatusChangeCallback.userdata = (void *)this;
	frameStatusChangeCallback.callback = handleFrameStatusChange;
	frameStatusChangeCallback.newStatus = FRAME_STATUS_NEW;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_PROCESSING;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_GONE;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);

	//We also want to introduce a checkpoint so that frames cannot TRANSITION AWAY from FRAME_STATUS_PROCESSING without our blessing.
	frameServer->registerFrameStatusCheckpoint(FRAME_STATUS_PROCESSING, "faceClassifier.ran");

	//Start worker threads.
	if(numWorkers == 0) {
		int numCPUs = SDL_GetCPUCount();
		numWorkers = (int)ceil((double)numCPUs * (double)numWorkersPerCPU);
		logger->debug("Calculating NumWorkers: System has %d CPUs, at %.02lf Workers per CPU that's %d NumWorkers.", numCPUs, numWorkersPerCPU, numWorkers);
	} else {
		logger->debug("NumWorkers explicitly set to %d.", numWorkers);
	}
	if(numWorkers < 1) {
		throw invalid_argument("NumWorkers can't be zero!");
	}
	for(int i = 1; i <= numWorkers; i++) {
		FaceClassifierWorker *worker = new FaceClassifierWorker();
		worker->num = i;
		worker->self = this;
		if(usingDNNFaceDetection) {
			deserialize(faceDetectionModelFileName.c_str()) >> worker->faceDetectionModel;
		} else {
			worker->frontalFaceDetector = get_frontal_face_detector();
		}
		if((worker->thread = SDL_CreateThread(workerLoop, "FaceClassifier", (void *)worker)) == NULL) {
			throw runtime_error("Failed starting thread!");
		}
		workers.push_back(worker);
	}

	logger->debug("FaceClassifier object constructed with Face Detection Method: %s, NumWorkers: %d", usingDNNFaceDetection ? "DNN" : "HOG", numWorkers);
}

FaceClassifier::~FaceClassifier() noexcept(false) {
	logger->debug("FaceClassifier object destructing...");

	YerFace_MutexLock(myMutex);
	if(!frameServerDrained) {
		logger->error("Frame server has not finished draining! Here be dragons!");
	}
	YerFace_MutexUnlock(myMutex);

	for(auto worker : workers) {
		SDL_WaitThread(worker->thread, NULL);
		delete worker;
	}

	YerFace_MutexLock(myMutex);
	if(workingFrameNumbers.size() > 0) {
		logger->error("Frames are still pending! Woe is me!");
	}
	YerFace_MutexUnlock(myMutex);

	SDL_DestroyCond(myCond);
	SDL_DestroyMutex(myMutex);
	SDL_DestroyMutex(classificationsMutex);
	delete logger;
	delete metrics;
}

FacialClassificationBox FaceClassifier::getFacialClassification(FrameNumber frameNumber) {
	FacialClassificationBox classification;
	YerFace_MutexLock(classificationsMutex);
	classification = classifications[frameNumber];
	YerFace_MutexUnlock(classificationsMutex);
	return classification;
}

void FaceClassifier::renderPreviewHUD(FrameNumber frameNumber, int density) {
	YerFace_MutexLock(classificationsMutex);
	FacialClassificationBox classification = classifications[frameNumber];
	YerFace_MutexUnlock(classificationsMutex);

	WorkingFrame *previewFrame = frameServer->getWorkingFrame(frameNumber);
	YerFace_MutexLock(previewFrame->previewFrameMutex);
	if(density > 1) {
		if(classification.set) {
			logger->verbose("Classification for Frame #%lld was generated against Frame #%lld. Surprise!", frameNumber, classification.timestamps.frameNumber);
			cv::rectangle(previewFrame->previewFrame, classification.boxNormalSize, Scalar(255, 255, 0), 1);
		}
	}
	YerFace_MutexUnlock(previewFrame->previewFrameMutex);
}

void FaceClassifier::doClassifyFace(FaceClassifierWorker *worker, WorkingFrame *frame) {
	dlib::cv_image<dlib::bgr_pixel> dlibClassificationFrame = cv_image<bgr_pixel>(frame->classificationFrame);
	std::vector<dlib::rectangle> faces;

	if(usingDNNFaceDetection) {
		//Using dlib's CNN-based face detector which can (optimistically) be pushed out to the GPU
		dlib::matrix<dlib::rgb_pixel> imageMatrix;
		dlib::assign_image(imageMatrix, dlibClassificationFrame);
		std::vector<dlib::mmod_rect> detections = worker->faceDetectionModel(imageMatrix);
		for(dlib::mmod_rect detection : detections) {
			faces.push_back(detection.rect);
		}
	} else {
		//Using dlib's built-in HOG face detector instead of a CNN-based detector
		faces = worker->frontalFaceDetector(dlibClassificationFrame);
	}

	int bestFace = -1;
	int bestFaceArea = -1;
	Rect2d tempBox, tempBoxNormalSize, bestFaceBox, bestFaceBoxNormalSize;
	int i = -1;
	for(dlib::rectangle face : faces) {
		i++;
		tempBox.x = face.left();
		tempBox.y = face.top();
		tempBox.width = face.right() - tempBox.x;
		tempBox.height = face.bottom() - tempBox.y;
		tempBoxNormalSize = Utilities::scaleRect(tempBox, 1.0 / frame->classificationScaleFactor);
		if((int)face.area() > bestFaceArea) {
			bestFace = i;
			bestFaceArea = face.area();
			bestFaceBox = tempBox;
			bestFaceBoxNormalSize = tempBoxNormalSize;
		}
	}
	FacialClassificationBox classification;
	classification.timestamps = frame->frameTimestamps;
	classification.run = true;
	classification.set = false;
	if(bestFace >= 0) {
		classification.box = bestFaceBox;
		classification.boxNormalSize = bestFaceBoxNormalSize;
		classification.set = true;
	}

	bool resultUsed = false;
	YerFace_MutexLock(classificationsMutex);
	if(!classifications[frame->frameTimestamps.frameNumber].set) {
		classifications[frame->frameTimestamps.frameNumber] = classification;
		resultUsed = true;
	}
	if(classification.set && (!latestClassification.set || latestClassification.timestamps.startTimestamp < classification.timestamps.startTimestamp)) {
		latestClassification = classification;
		resultUsed = true;
	}
	YerFace_MutexUnlock(classificationsMutex);
	if(!resultUsed) {
		logger->warn("Classification performed, but it was of no use!");
	}
}

void FaceClassifier::handleFrameServerDrainedEvent(void *userdata) {
	FaceClassifier *self = (FaceClassifier *)userdata;
	// self->logger->verbose("Got notification that FrameServer has drained!");
	YerFace_MutexLock(self->myMutex);
	self->frameServerDrained = true;
	SDL_CondSignal(self->myCond);
	YerFace_MutexUnlock(self->myMutex);
}

void FaceClassifier::handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameNumber frameNumber) {
	FaceClassifier *self = (FaceClassifier *)userdata;
	// self->logger->verbose("Handling Frame Status Change for Frame Number %lld to Status %d", frameNumber, newStatus);
	FacialClassificationBox classification;
	switch(newStatus) {
		default:
			throw logic_error("Handler passed unsupported frame status change event!");
		case FRAME_STATUS_NEW:
			classification.run = false;
			classification.set = false;
			YerFace_MutexLock(self->classificationsMutex);
			self->classifications[frameNumber] = classification;
			YerFace_MutexUnlock(self->classificationsMutex);
			break;
		case FRAME_STATUS_PROCESSING:
			YerFace_MutexLock(self->myMutex);
			self->workingFrameNumbers.push_back(frameNumber);
			SDL_CondSignal(self->myCond);
			YerFace_MutexUnlock(self->myMutex);
			break;
		case FRAME_STATUS_GONE:
			YerFace_MutexLock(self->classificationsMutex);
			self->classifications.erase(frameNumber);
			YerFace_MutexUnlock(self->classificationsMutex);
			break;
	}
}

int FaceClassifier::workerLoop(void *ptr) {
	FaceClassifierWorker *worker = (FaceClassifierWorker *)ptr;
	FaceClassifier *self = worker->self;
	self->logger->verbose("FaceClassifier Worker Thread #%d Alive!", worker->num);

	YerFace_MutexLock(self->myMutex);
	while(!self->frameServerDrained) {
		// self->logger->verbose("Thread #%d Top of Loop", worker->num);

		if(self->status->getIsPaused() && self->status->getIsRunning()) {
			YerFace_MutexUnlock(self->myMutex);
			SDL_Delay(100);
			YerFace_MutexLock(self->myMutex);
			continue;
		}

		//// CHECK FOR WORK ////
		FrameNumber workingFrameNumber = -1;
		//If there are preview frames waiting to be displayed, handle them.
		if(self->workingFrameNumbers.size() > 0) {
			workingFrameNumber = self->workingFrameNumbers.front();
			self->workingFrameNumbers.pop_front();
		}

		//// DO THE WORK ////
		if(workingFrameNumber > 0) {
			//Do not squat on myMutex while doing time-consuming work.
			YerFace_MutexUnlock(self->myMutex);

			self->logger->verbose("Thread #%d handling frame #%lld", worker->num, workingFrameNumber);
			MetricsTick tick = self->metrics->startClock();

			WorkingFrame *workingFrame = self->frameServer->getWorkingFrame(workingFrameNumber);
			FrameTimestamps workingFrameTimestamps = workingFrame->frameTimestamps;

			bool runClassificationOnThisFrame = true;
			YerFace_MutexLock(self->classificationsMutex);
			if(self->latestClassification.set) {
				double latestClassificationGoodUntil = self->latestClassification.timestamps.startTimestamp + self->resultGoodForSeconds;
				self->logger->verbose("Thread #%d, Frame #%lld - FrameStart: %.03lf, LCFrame: %lld, LCGoodUntil: %.03lf", worker->num, workingFrameNumber, workingFrameTimestamps.startTimestamp, self->latestClassification.timestamps.frameNumber, latestClassificationGoodUntil);
				if(workingFrameTimestamps.startTimestamp <= latestClassificationGoodUntil) {
					self->classifications[workingFrameNumber] = self->latestClassification;
					self->logger->verbose("Thread #%d, Frame #%lld - Using LC!", worker->num, workingFrameNumber);
					if(workingFrameTimestamps.estimatedEndTimestamp <= latestClassificationGoodUntil) {
						self->logger->verbose("Thread #%d, Frame #%lld - Don't Run Classification", worker->num, workingFrameNumber);
						runClassificationOnThisFrame = false;
					}
				}
			} else {
				self->logger->verbose("Thread #%d, Frame #%lld - Seems like latestClassification is not set!", worker->num, workingFrameNumber);
			}
			YerFace_MutexUnlock(self->classificationsMutex);

			if(runClassificationOnThisFrame) {
				self->logger->verbose("Thread #%d, Frame #%lld - Running Classification", worker->num, workingFrameNumber);
				self->doClassifyFace(worker, workingFrame);
			}

			self->frameServer->setWorkingFrameStatusCheckpoint(workingFrameNumber, FRAME_STATUS_PROCESSING, "faceClassifier.ran");
			self->metrics->endClock(tick);

			//Need to re-lock while spinning.
			YerFace_MutexLock(self->myMutex);
		} else {
			//If there is no work available, go to sleep and wait.
			// self->logger->verbose("Thread #%d entering CondWait...", worker->num);
			int result = SDL_CondWaitTimeout(self->myCond, self->myMutex, 1000);
			if(result < 0) {
				throw runtime_error("CondWaitTimeout() failed!");
			} else if(result == SDL_MUTEX_TIMEDOUT) {
				if(!self->status->getIsPaused()) {
					self->logger->warn("Thread #%d timed out waiting for Condition signal!", worker->num);
				}
			}
			// self->logger->verbose("Thread #%d left CondWait!", worker->num);
		}
	}
	YerFace_MutexUnlock(self->myMutex);

	self->logger->verbose("Thread #%d Done.", worker->num);
	return 0;
}

} //namespace YerFace
