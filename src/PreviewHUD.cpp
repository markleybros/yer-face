
#include "PreviewHUD.hpp"
#include "Utilities.hpp"

#include <math.h>

using namespace std;

namespace YerFace {

PreviewHUD::PreviewHUD(json config, Status *myStatus, FrameServer *myFrameServer) {
	status = myStatus;
	if(status == NULL) {
		throw invalid_argument("status cannot be NULL");
	}
	frameServer = myFrameServer;
	if(frameServer == NULL) {
		throw invalid_argument("frameServer cannot be NULL");
	}
	logger = new Logger("PreviewHUD");
	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((myCond = SDL_CreateCond()) == NULL) {
		throw runtime_error("Failed creating condition!");
	}
	metrics = new Metrics(config, "PreviewHUD", true);
	numWorkers = config["YerFace"]["PreviewHUD"]["numWorkers"];
	if(numWorkers < 0.0) {
		throw invalid_argument("numWorkers is nonsense.");
	}
	numWorkersPerCPU = config["YerFace"]["PreviewHUD"]["numWorkersPerCPU"];
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

	//We want to know when any frame has entered PROCESSING.
	FrameStatusChangeEventCallback frameStatusChangeCallback;
	frameStatusChangeCallback.userdata = (void *)this;
	frameStatusChangeCallback.callback = handleFrameStatusChange;
	frameStatusChangeCallback.newStatus = FRAME_STATUS_PREVIEWING;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);

	//We also want to introduce a checkpoint so that frames cannot TRANSITION AWAY from FRAME_STATUS_PPREVIEWING without our blessing.
	frameServer->registerFrameStatusCheckpoint(FRAME_STATUS_PREVIEWING, "previewHUD.ran");

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
		PreviewHUDWorker *worker = new PreviewHUDWorker();
		worker->num = i;
		worker->self = this;
		if((worker->thread = SDL_CreateThread(workerLoop, "PreviewHUD", (void *)worker)) == NULL) {
			throw runtime_error("Failed starting thread!");
		}
		workers.push_back(worker);
	}

	logger->debug("PreviewHUD object constructed with NumWorkers: %d", numWorkers);
}

PreviewHUD::~PreviewHUD() noexcept(false) {
	logger->debug("PreviewHUD object destructing...");

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
	if(pendingFrameNumbers.size() > 0) {
		logger->error("Frames are still pending! Woe is me!");
	}
	YerFace_MutexUnlock(myMutex);

	SDL_DestroyCond(myCond);
	SDL_DestroyMutex(myMutex);
	delete logger;
	delete metrics;
}

void PreviewHUD::registerPreviewHUDRenderer(PreviewHUDRenderer renderer) {
	YerFace_MutexLock(myMutex);
	renderers.push_back(renderer);
	YerFace_MutexUnlock(myMutex);
}

void PreviewHUD::handleFrameServerDrainedEvent(void *userdata) {
	PreviewHUD *self = (PreviewHUD *)userdata;
	// self->logger->verbose("Got notification that FrameServer has drained!");
	YerFace_MutexLock(self->myMutex);
	self->frameServerDrained = true;
	SDL_CondSignal(self->myCond);
	YerFace_MutexUnlock(self->myMutex);
}

void PreviewHUD::handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameNumber frameNumber) {
	PreviewHUD *self = (PreviewHUD *)userdata;
	// self->logger->verbose("Handling Frame Status Change for Frame Number %lld to Status %d", frameNumber, newStatus);
	switch(newStatus) {
		default:
			throw logic_error("Handler passed unsupported frame status change event!");
		case FRAME_STATUS_PREVIEWING:
			YerFace_MutexLock(self->myMutex);
			self->pendingFrameNumbers.push_back(frameNumber);
			SDL_CondSignal(self->myCond);
			YerFace_MutexUnlock(self->myMutex);
			break;
	}
}

int PreviewHUD::workerLoop(void *ptr) {
	PreviewHUDWorker *worker = (PreviewHUDWorker *)ptr;
	PreviewHUD *self = worker->self;
	self->logger->verbose("PreviewHUD Worker Thread #%d Alive!", worker->num);

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
		FrameNumber frameNumber = -1;
		//If there are preview frames waiting to be displayed, handle them.
		if(self->pendingFrameNumbers.size() > 0) {
			frameNumber = self->pendingFrameNumbers.front();
			self->pendingFrameNumbers.pop_front();
		}

		//// DO THE WORK ////
		if(frameNumber > 0) {
			std::list<PreviewHUDRenderer> myRenderers = self->renderers; //Make a copy of this so we can operate on it while myMutex is unlocked.

			//Do not squat on myMutex while doing time-consuming work.
			YerFace_MutexUnlock(self->myMutex);

			// self->logger->verbose("Thread #%d handling frame #%lld", worker->num, frameNumber);
			MetricsTick tick = self->metrics->startClock();

			int density = 2; //FIXME

			WorkingFrame *previewFrame = self->frameServer->getWorkingFrame(frameNumber);
			YerFace_MutexLock(previewFrame->previewFrameMutex);

			for(auto renderer : myRenderers) {
				renderer(previewFrame->previewFrame, frameNumber, density);
			}

			YerFace_MutexUnlock(previewFrame->previewFrameMutex);

			self->frameServer->setWorkingFrameStatusCheckpoint(frameNumber, FRAME_STATUS_PREVIEWING, "previewHUD.ran");
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
				if(!self->status->getIsPaused() && !self->frameServerDrained) {
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
