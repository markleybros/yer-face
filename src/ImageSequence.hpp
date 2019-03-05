#pragma once

#include "Logger.hpp"
#include "Utilities.hpp"
#include "FrameServer.hpp"
#include "Metrics.hpp"

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
	ImageSequence(json config, FrameServer *myFrameServer, string myOutputPrefix);
	~ImageSequence() noexcept(false);
private:
	static void handleFrameServerDrainedEvent(void *userdata);
	static void handleFrameStatusLateProcessing(void *userdata, WorkingFrameStatus newStatus, FrameNumber frameNumber);
	static int frameWriterLoop(void *ptr);

	double numWorkersPerCPU;
	int numWorkers;

	FrameServer *frameServer;
	bool frameServerDrained;

	Metrics *metrics;

	string outputPrefix;

	Logger *logger;
	SDL_mutex *myMutex;
	SDL_cond *myCond;
	list<FrameNumber> outputFrameNumbers;

	std::list<ImageSequenceWorker *> workers;
};

}; //namespace YerFace
