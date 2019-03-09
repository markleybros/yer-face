#pragma once

#include "Logger.hpp"
#include "Utilities.hpp"
#include "FrameServer.hpp"
#include "Status.hpp"
#include "Metrics.hpp"
#include "WorkerPool.hpp"

#include <list>

using namespace std;

namespace YerFace {

class ImageSequence;

class ImageSequenceWorker {
public:
	int num;
	SDL_Thread *thread;
	ImageSequence *self;
};

class ImageSequence {
public:
	ImageSequence(json config, Status *myStatus, FrameServer *myFrameServer, string myOutputPrefix);
	~ImageSequence() noexcept(false);
private:
	static void workerInitializer(WorkerPoolWorker *worker, void *ptr);
	static bool workerHandler(WorkerPoolWorker *worker);
	static void handleFrameStatusLateProcessing(void *userdata, WorkingFrameStatus newStatus, FrameNumber frameNumber);
	static int frameWriterLoop(void *ptr);

	Status *status;
	FrameServer *frameServer;

	Logger *logger;
	Metrics *metrics;
	WorkerPool *workerPool;

	string outputPrefix;

	SDL_mutex *myMutex;
	list<FrameNumber> outputFrameNumbers;
};

}; //namespace YerFace
