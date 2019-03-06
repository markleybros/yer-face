
#include "ImageSequence.hpp"
#include "Utilities.hpp"

#include <math.h>

#include <opencv2/imgcodecs.hpp>

using namespace std;
using namespace cv;

namespace YerFace {

ImageSequence::ImageSequence(json config, FrameServer *myFrameServer, string myOutputPrefix) {
	outputPrefix = myOutputPrefix;
	frameServer = myFrameServer;
	if(frameServer == NULL) {
		throw invalid_argument("frameServer cannot be NULL");
	}
	logger = new Logger("ImageSequence");
	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((myCond = SDL_CreateCond()) == NULL) {
		throw runtime_error("Failed creating condition!");
	}
	metrics = new Metrics(config, "ImageSequence", true);
	numWorkers = config["YerFace"]["FaceDetector"]["numWorkers"];
	if(numWorkers < 0.0) {
		throw invalid_argument("numWorkers is nonsense.");
	}
	numWorkersPerCPU = config["YerFace"]["ImageSequence"]["numWorkersPerCPU"];
	if(numWorkersPerCPU < 0.0) {
		throw invalid_argument("numWorkersPerCPU is nonsense.");
	}

	//Hook into the frame lifecycle.

	//We need to know when the frame server has drained.
	frameServerDrained = false;
	FrameServerDrainedEventCallback frameServerDrainedCallback;
	frameServerDrainedCallback.userdata = (void *)this;
	frameServerDrainedCallback.callback = handleFrameServerDrainedEvent;
	frameServer->onFrameServerDrainedEvent(frameServerDrainedCallback);

	//We want to know when any frame has entered LATE_PROCESSING.
	FrameStatusChangeEventCallback frameStatusLateProcessingCallback;
	frameStatusLateProcessingCallback.userdata = (void *)this;
	frameStatusLateProcessingCallback.newStatus = FRAME_STATUS_LATE_PROCESSING;
	frameStatusLateProcessingCallback.callback = handleFrameStatusLateProcessing;
	frameServer->onFrameStatusChangeEvent(frameStatusLateProcessingCallback);

	//We also want to introduce a checkpoint so that frames cannot TRANSITION AWAY from FRAME_STATUS_LATE_PROCESSING without our blessing.
	frameServer->registerFrameStatusCheckpoint(FRAME_STATUS_LATE_PROCESSING, "imageSequence.written");

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
		ImageSequenceWorker *worker = new ImageSequenceWorker();
		worker->num = i;
		worker->self = this;
		if((worker->thread = SDL_CreateThread(frameWriterLoop, "ImageSequence", (void *)worker)) == NULL) {
			throw runtime_error("Failed starting thread!");
		}
		workers.push_back(worker);
	}

	logger->debug("ImageSequence object constructed with OutputPrefix: \"%s\"; NumWorkers: %d", outputPrefix.c_str(), numWorkers);
}

ImageSequence::~ImageSequence() noexcept(false) {
	logger->debug("ImageSequence object destructing...");

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
	if(outputFrameNumbers.size() > 0) {
		logger->error("Frames are still pending! Woe is me!");
	}
	YerFace_MutexUnlock(myMutex);

	SDL_DestroyCond(myCond);
	SDL_DestroyMutex(myMutex);
	delete logger;
	delete metrics;
}

void ImageSequence::handleFrameServerDrainedEvent(void *userdata) {
	ImageSequence *self = (ImageSequence *)userdata;
	// self->logger->verbose("Got notification that FrameServer has drained!");
	YerFace_MutexLock(self->myMutex);
	self->frameServerDrained = true;
	SDL_CondSignal(self->myCond);
	YerFace_MutexUnlock(self->myMutex);
}

void ImageSequence::handleFrameStatusLateProcessing(void *userdata, WorkingFrameStatus newStatus, FrameNumber frameNumber) {
	ImageSequence *self = (ImageSequence *)userdata;
	// self->logger->verbose("Got notification that Frame #%lld has transitioned to LATE_PROCESSING.", frameNumber);
	//It is not safe or recommended to perform ANY work during a FrameStatusChangeEventCallback.
	//(Notably, any attempt to operate on the frame is very likely to deadlock.)
	//The only thing we can (and should) do is record the frame number in the list of frames we care about.
	YerFace_MutexLock(self->myMutex);
	self->outputFrameNumbers.push_back(frameNumber);
	SDL_CondSignal(self->myCond);
	YerFace_MutexUnlock(self->myMutex);
}

int ImageSequence::frameWriterLoop(void *ptr) {
	ImageSequenceWorker *worker = (ImageSequenceWorker *)ptr;
	ImageSequence *self = worker->self;
	self->logger->verbose("ImageSequence Worker Thread #%d Alive!", worker->num);

	YerFace_MutexLock(self->myMutex);
	while(!self->frameServerDrained) {
		// self->logger->verbose("Thread #%d Top of Loop", worker->num);

		//// CHECK FOR WORK ////
		FrameNumber outputFrameNumber = -1;
		//If there are preview frames waiting to be displayed, handle them.
		if(self->outputFrameNumbers.size() > 0) {
			//Only display the latest one.
			outputFrameNumber = self->outputFrameNumbers.front();
			self->outputFrameNumbers.pop_front();
		}

		//// DO THE WORK ////
		if(outputFrameNumber > 0) {
			//Do not squat on myMutex while doing time-consuming work.
			YerFace_MutexUnlock(self->myMutex);

			// self->logger->verbose("Thread #%d handling frame #%lld", worker->num, outputFrameNumber);

			MetricsTick tick = self->metrics->startClock();

			WorkingFrame *previewFrame = self->frameServer->getWorkingFrame(outputFrameNumber);

			YerFace_MutexLock(previewFrame->previewFrameMutex);
			Mat previewFrameCopy = previewFrame->previewFrame.clone();
			YerFace_MutexUnlock(previewFrame->previewFrameMutex);

			self->frameServer->setWorkingFrameStatusCheckpoint(outputFrameNumber, FRAME_STATUS_LATE_PROCESSING, "imageSequence.written");

			int filenameLength = self->outputPrefix.length() + 32;
			char filename[filenameLength];
			snprintf(filename, filenameLength, "%s-%06lld.png", self->outputPrefix.c_str(), outputFrameNumber);
			self->logger->verbose("Thread #%d writing preview frame #%lld to file: %s ", worker->num, outputFrameNumber, filename);
			imwrite(filename, previewFrameCopy);

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
				self->logger->warn("Thread #%d timed out waiting for Condition signal!", worker->num);
			}
			// self->logger->verbose("Thread #%d left CondWait!", worker->num);
		}
	}
	YerFace_MutexUnlock(self->myMutex);

	self->logger->verbose("Thread #%d Done.", worker->num);
	return 0;
}

} //namespace YerFace
