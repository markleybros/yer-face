
#include "ImageSequence.hpp"
#include "Utilities.hpp"

#include <math.h>

#include <opencv2/imgcodecs.hpp>

using namespace std;
using namespace cv;

namespace YerFace {

ImageSequence::ImageSequence(json config, Status *myStatus, FrameServer *myFrameServer, string myOutputPrefix) {
	outputPrefix = myOutputPrefix;
	status = myStatus;
	if(status == NULL) {
		throw invalid_argument("status cannot be NULL");
	}
	frameServer = myFrameServer;
	if(frameServer == NULL) {
		throw invalid_argument("frameServer cannot be NULL");
	}
	logger = new Logger("ImageSequence");
	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	metrics = new Metrics(config, "ImageSequence", true);

	//Hook into the frame lifecycle.

	//We want to know when any frame has entered LATE_PROCESSING.
	FrameStatusChangeEventCallback frameStatusLateProcessingCallback;
	frameStatusLateProcessingCallback.userdata = (void *)this;
	frameStatusLateProcessingCallback.newStatus = FRAME_STATUS_PREVIEW_DISPLAY;
	frameStatusLateProcessingCallback.callback = handleFrameStatusLateProcessing;
	frameServer->onFrameStatusChangeEvent(frameStatusLateProcessingCallback);

	//We also want to introduce a checkpoint so that frames cannot TRANSITION AWAY from FRAME_STATUS_PREVIEW_DISPLAY without our blessing.
	frameServer->registerFrameStatusCheckpoint(FRAME_STATUS_PREVIEW_DISPLAY, "imageSequence.written");

	WorkerPoolParameters workerPoolParameters;
	workerPoolParameters.name = "ImageSequence";
	workerPoolParameters.numWorkers = config["YerFace"]["ImageSequence"]["numWorkers"];
	workerPoolParameters.numWorkersPerCPU = config["YerFace"]["ImageSequence"]["numWorkersPerCPU"];
	workerPoolParameters.initializer = NULL;
	workerPoolParameters.deinitializer = NULL;
	workerPoolParameters.usrPtr = (void *)this;
	workerPoolParameters.handler = workerHandler;
	workerPool = new WorkerPool(config, status, frameServer, workerPoolParameters);

	logger->debug("ImageSequence object constructed with OutputPrefix: %s", outputPrefix.c_str());
}

ImageSequence::~ImageSequence() noexcept(false) {
	logger->debug("ImageSequence object destructing...");

	delete workerPool;

	YerFace_MutexLock(myMutex);
	if(outputFrameNumbers.size() > 0) {
		logger->error("Frames are still pending! Woe is me!");
	}
	YerFace_MutexUnlock(myMutex);

	SDL_DestroyMutex(myMutex);
	delete logger;
	delete metrics;
}

bool ImageSequence::workerHandler(WorkerPoolWorker *worker) {
	ImageSequence *self = (ImageSequence *)worker->ptr;
	bool didWork = false;

	YerFace_MutexLock(self->myMutex);

	//// CHECK FOR WORK ////
	FrameNumber outputFrameNumber = -1;
	//If there are preview frames waiting to be displayed, handle them.
	if(self->outputFrameNumbers.size() > 0) {
		//Only display the latest one.
		outputFrameNumber = self->outputFrameNumbers.front();
		self->outputFrameNumbers.pop_front();
	}

	//Do not squat on myMutex while doing time-consuming work.
	YerFace_MutexUnlock(self->myMutex);

	//// DO THE WORK ////
	if(outputFrameNumber > 0) {
		// self->logger->verbose("Thread #%d handling frame #" YERFACE_FRAMENUMBER_FORMAT, worker->num, outputFrameNumber);

		MetricsTick tick = self->metrics->startClock();

		WorkingFrame *previewFrame = self->frameServer->getWorkingFrame(outputFrameNumber);

		YerFace_MutexLock(previewFrame->previewFrameMutex);
		Mat previewFrameCopy = previewFrame->previewFrame.clone();
		YerFace_MutexUnlock(previewFrame->previewFrameMutex);

		self->frameServer->setWorkingFrameStatusCheckpoint(outputFrameNumber, FRAME_STATUS_PREVIEW_DISPLAY, "imageSequence.written");

		int filenameLength = self->outputPrefix.length() + 32;
		char filename[filenameLength];
		snprintf(filename, filenameLength, "%s-%06" YERFACE_FRAMENUMBER_FORMATINNER ".png", self->outputPrefix.c_str(), outputFrameNumber);
		self->logger->verbose("Thread #%d writing preview frame #" YERFACE_FRAMENUMBER_FORMAT " to file: %s ", worker->num, outputFrameNumber, filename);
		imwrite(filename, previewFrameCopy);

		self->metrics->endClock(tick);

		didWork = true;
	}
	return didWork;
}

void ImageSequence::handleFrameStatusLateProcessing(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps) {
	FrameNumber frameNumber = frameTimestamps.frameNumber;
	ImageSequence *self = (ImageSequence *)userdata;
	// self->logger->verbose("Got notification that Frame #" YERFACE_FRAMENUMBER_FORMAT " has transitioned to LATE_PROCESSING.", frameNumber);
	//It is not safe or recommended to perform ANY work during a FrameStatusChangeEventCallback.
	//(Notably, any attempt to operate on the frame is very likely to deadlock.)
	//The only thing we can (and should) do is record the frame number in the list of frames we care about.
	YerFace_MutexLock(self->myMutex);
	self->outputFrameNumbers.push_back(frameNumber);
	YerFace_MutexUnlock(self->myMutex);
	self->workerPool->sendWorkerSignal();
}

} //namespace YerFace
