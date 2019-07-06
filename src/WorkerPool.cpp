
#include "WorkerPool.hpp"
#include "Utilities.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

WorkerPool::WorkerPool(json config, Status *myStatus, FrameServer *myFrameServer, WorkerPoolParameters myParameters) {
	status = myStatus;
	if(status == NULL) {
		throw invalid_argument("status cannot be NULL");
	}
	frameServer = myFrameServer;
	if(frameServer == NULL) {
		throw invalid_argument("frameServer cannot be NULL");
	}
	parameters = myParameters;

	string loggerName = "WorkerPool<" + parameters.name + ">";
	logger = new Logger(loggerName.c_str());
	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((myCond = SDL_CreateCond()) == NULL) {
		throw runtime_error("Failed creating condition!");
	}

	if(parameters.numWorkers < 0.0) {
		throw invalid_argument("numWorkers is nonsense.");
	}
	if(parameters.numWorkersPerCPU < 0.0) {
		throw invalid_argument("numWorkersPerCPU is nonsense.");
	}

	running = true;

	//Hook into the frame lifecycle.

	//We need to know when the frame server has drained.
	frameServerDrained = false;
	FrameServerDrainedEventCallback frameServerDrainedCallback;
	frameServerDrainedCallback.userdata = (void *)this;
	frameServerDrainedCallback.callback = handleFrameServerDrainedEvent;
	frameServer->onFrameServerDrainedEvent(frameServerDrainedCallback);

	//Start worker threads.
	if(parameters.numWorkers == 0) {
		int numCPUs = SDL_GetCPUCount();
		parameters.numWorkers = (int)ceil((double)numCPUs * (double)parameters.numWorkersPerCPU);
		logger->debug1("Calculating NumWorkers: System has %d CPUs, at %.02lf Workers per CPU that's %d NumWorkers.", numCPUs, parameters.numWorkersPerCPU, parameters.numWorkers);
	} else {
		logger->debug1("NumWorkers explicitly set to %d.", parameters.numWorkers);
	}
	if(parameters.numWorkers < 1) {
		throw invalid_argument("NumWorkers can't be zero!");
	}
	for(int i = 1; i <= parameters.numWorkers; i++) {
		WorkerPoolWorker *worker = new WorkerPoolWorker();
		worker->num = i;
		worker->ptr = parameters.usrPtr;
		worker->pool = this;
		if((worker->thread = SDL_CreateThread(outerWorkerLoop, parameters.name.c_str(), (void *)worker)) == NULL) {
			throw runtime_error("Failed starting thread!");
		}
		workers.push_back(worker);
	}

	logger->debug1("WorkerPool object constructed with NumWorkers: %d", parameters.numWorkers);
}

WorkerPool::~WorkerPool() noexcept(false) {
	logger->debug1("WorkerPool object destructing...");

	YerFace_MutexLock(myMutex);
	if(!frameServerDrained && running) {
		logger->crit("Frame server has not finished draining and nobody explicitly told us to stop! Here be dragons!");
		running = false;
		SDL_CondBroadcast(myCond);
	}
	YerFace_MutexUnlock(myMutex);

	for(auto worker : workers) {
		SDL_WaitThread(worker->thread, NULL);
		delete worker;
	}

	SDL_DestroyCond(myCond);
	SDL_DestroyMutex(myMutex);
	delete logger;
}

void WorkerPool::sendWorkerSignal(void) {
	YerFace_MutexLock(myMutex);
	SDL_CondSignal(myCond);
	YerFace_MutexUnlock(myMutex);
}

void WorkerPool::stopWorkerNow(void) {
	YerFace_MutexLock(myMutex);
	running = false;
	SDL_CondBroadcast(myCond);
	YerFace_MutexUnlock(myMutex);
}

void WorkerPool::handleFrameServerDrainedEvent(void *userdata) {
	WorkerPool *self = (WorkerPool *)userdata;
	self->logger->debug2("Got notification that FrameServer has drained!");
	if(!self->running) {
		self->logger->debug2("frameServerDrainedEvent came in too late! We are no longer running...");
		return;
	}
	YerFace_MutexLock(self->myMutex);
	self->frameServerDrained = true;
	SDL_CondBroadcast(self->myCond);
	YerFace_MutexUnlock(self->myMutex);
}

int WorkerPool::outerWorkerLoop(void *ptr) {
	WorkerPoolWorker *worker = (WorkerPoolWorker *)ptr;
	WorkerPool *self = worker->pool;
	try {
		self->logger->debug1("Worker Thread #%d Alive!", worker->num);

		if(self->parameters.initializer != NULL) {
			self->parameters.initializer(worker, self->parameters.usrPtr);
		}

		YerFace_MutexLock(self->myMutex);
		while(!self->frameServerDrained && self->running) {
			// self->logger->debug4("Thread #%d Top of Loop", worker->num);

			if(self->status->getIsPaused() && self->status->getIsRunning()) {
				YerFace_MutexUnlock(self->myMutex);
				SDL_Delay(100);
				YerFace_MutexLock(self->myMutex);
				continue;
			}

			YerFace_MutexUnlock(self->myMutex);
			bool didWork = self->parameters.handler(worker);
			YerFace_MutexLock(self->myMutex);

			//If there is no work available, go to sleep and wait.
			if(!didWork) {
				// self->logger->verbose("Thread #%d entering CondWait...", worker->num);
				int result = SDL_CondWaitTimeout(self->myCond, self->myMutex, 1000);
				if(result < 0) {
					throw runtime_error("CondWaitTimeout() failed!");
				} else if(result == SDL_MUTEX_TIMEDOUT) {
					if(!self->status->getIsPaused() && !self->frameServerDrained) {
						self->logger->warning("Thread #%d timed out waiting for Condition signal!", worker->num);
					}
				}
				// self->logger->verbose("Thread #%d left CondWait!", worker->num);
			}
			if(self->status->getEmergency()) {
				self->logger->debug1("Thread #%d honoring emergency stop.", worker->num);
				self->running = false;
			}
		}
		YerFace_MutexUnlock(self->myMutex);

		if(self->parameters.deinitializer != NULL) {
			self->parameters.deinitializer(worker, self->parameters.usrPtr);
		}

		self->logger->debug1("Thread #%d Done.", worker->num);
		return 0;
	} catch(exception &e) {
		self->logger->emerg("Uncaught exception in worker thread: %s\n", e.what());
		self->status->setEmergency();
	}
	return 1;
}

} //namespace YerFace
