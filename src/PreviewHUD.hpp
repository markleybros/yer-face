#pragma once

#include "Logger.hpp"
#include "Utilities.hpp"
#include "FrameServer.hpp"
#include "Metrics.hpp"

#include <list>

using namespace std;

namespace YerFace {

typedef function<void(Mat previewFrame, FrameNumber frameNumber, int density)> PreviewHUDRenderer;

class PreviewHUD;

class PreviewHUDWorker {
public:
	int num;
	SDL_Thread *thread;
	PreviewHUD *self;
};

class PreviewHUD {
public:
	PreviewHUD(json config, Status *myStatus, FrameServer *myFrameServer);
	~PreviewHUD() noexcept(false);
	void registerPreviewHUDRenderer(PreviewHUDRenderer renderer);
	void createPreviewHUDRectangle(Size frameSize, Rect2d *previewRect, Point2d *previewCenter);
private:
	static void handleFrameServerDrainedEvent(void *userdata);
	static void handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameNumber frameNumber);
	static int workerLoop(void *ptr);

	double numWorkersPerCPU;
	int numWorkers;

	Status *status;
	FrameServer *frameServer;
	bool frameServerDrained;

	Metrics *metrics;
	Logger *logger;
	SDL_mutex *myMutex;
	SDL_cond *myCond;
	list<FrameNumber> pendingFrameNumbers;

	double previewRatio, previewWidthPercentage, previewCenterHeightPercentage;

	std::list<PreviewHUDWorker *> workers;

	std::list<PreviewHUDRenderer> renderers;
};

}; //namespace YerFace
