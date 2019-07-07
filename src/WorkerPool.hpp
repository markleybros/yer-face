#pragma once

#include "Logger.hpp"
#include "Status.hpp"
#include "FrameServer.hpp"
#include "Utilities.hpp"

#include "SDL.h"

using namespace std;

namespace YerFace {

class WorkerPool;

class WorkerPoolWorker {
public:
	int num;
	SDL_Thread *thread;
	void *ptr;
	WorkerPool *pool;
};

typedef function<void(WorkerPoolWorker *worker, void *ptr)> WorkerPoolWorkerInitializer;
typedef function<bool(WorkerPoolWorker *worker)> WorkerPoolWorkerHandler;
typedef function<void(WorkerPoolWorker *worker, void *ptr)> WorkerPoolWorkerDeinitializer;

class WorkerPoolParameters {
public:
	string name;
	double numWorkersPerCPU;
	int numWorkers;

	WorkerPoolWorkerInitializer initializer;
	WorkerPoolWorkerDeinitializer deinitializer;
	void *usrPtr;

	WorkerPoolWorkerHandler handler;
};

class WorkerPool {
public:
	WorkerPool(json config, Status *myStatus, FrameServer *myFrameServer, WorkerPoolParameters myParameters);
	~WorkerPool() noexcept(false);
	void sendWorkerSignal(void);
	void stopWorkerNow(void);
private:
	static void handleFrameServerDrainedEvent(void *userdata);
	static int outerWorkerLoop(void *ptr);

	Status *status;
	FrameServer *frameServer;

	WorkerPoolParameters parameters;

	Logger *logger;
	SDL_mutex *myMutex;
	SDL_cond *myCond;

	bool frameServerDrained, running;

	std::list<WorkerPoolWorker *> workers;
};

}; //namespace YerFace
