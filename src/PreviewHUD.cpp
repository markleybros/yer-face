
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
	metrics = new Metrics(config, "PreviewHUD", true);

	status->setPreviewDebugDensity(config["YerFace"]["PreviewHUD"]["initialPreviewDisplayDensity"]);
	previewRatio = config["YerFace"]["PreviewHUD"]["previewRatio"];
	previewWidthPercentage = config["YerFace"]["PreviewHUD"]["previewWidthPercentage"];
	previewCenterHeightPercentage = config["YerFace"]["PreviewHUD"]["previewCenterHeightPercentage"];

	//We want to know when any frame has entered our status.
	FrameStatusChangeEventCallback frameStatusChangeCallback;
	frameStatusChangeCallback.userdata = (void *)this;
	frameStatusChangeCallback.callback = handleFrameStatusChange;
	frameStatusChangeCallback.newStatus = FRAME_STATUS_PREVIEW_RENDER;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);

	//We also want to introduce a checkpoint so that frames cannot TRANSITION AWAY from FRAME_STATUS_PREVIEW_RENDER without our blessing.
	frameServer->registerFrameStatusCheckpoint(FRAME_STATUS_PREVIEW_RENDER, "previewHUD.ran");

	WorkerPoolParameters workerPoolParameters;
	workerPoolParameters.name = "PreviewHUD";
	workerPoolParameters.numWorkers = config["YerFace"]["PreviewHUD"]["numWorkers"];
	workerPoolParameters.numWorkersPerCPU = config["YerFace"]["PreviewHUD"]["numWorkersPerCPU"];
	workerPoolParameters.initializer = NULL;
	workerPoolParameters.deinitializer = NULL;
	workerPoolParameters.usrPtr = (void *)this;
	workerPoolParameters.handler = workerHandler;
	workerPool = new WorkerPool(config, status, frameServer, workerPoolParameters);

	logger->debug("PreviewHUD object constructed and ready to go.");
}

PreviewHUD::~PreviewHUD() noexcept(false) {
	logger->debug("PreviewHUD object destructing...");

	delete workerPool;

	YerFace_MutexLock(myMutex);
	if(pendingFrameNumbers.size() > 0) {
		logger->error("Frames are still pending! Woe is me!");
	}
	YerFace_MutexUnlock(myMutex);

	SDL_DestroyMutex(myMutex);
	delete logger;
	delete metrics;
}

void PreviewHUD::registerPreviewHUDRenderer(PreviewHUDRenderer renderer) {
	YerFace_MutexLock(myMutex);
	renderers.push_back(renderer);
	YerFace_MutexUnlock(myMutex);
}

void PreviewHUD::createPreviewHUDRectangle(Size frameSize, Rect2d *previewRect, Point2d *previewCenter) {
	previewRect->width = frameSize.width * previewWidthPercentage;
	previewRect->height = previewRect->width * previewRatio;
	PreviewPositionInFrame previewPosition = status->getPreviewPositionInFrame();
	if(previewPosition == BottomRight || previewPosition == TopRight) {
		previewRect->x = frameSize.width - previewRect->width;
	} else {
		previewRect->x = 0;
	}
	if(previewPosition == BottomLeft || previewPosition == BottomRight) {
		previewRect->y = frameSize.height - previewRect->height;
	} else {
		previewRect->y = 0;
	}
	*previewCenter = Utilities::centerRect(*previewRect);
	previewCenter->y -= previewRect->height * previewCenterHeightPercentage;
}

bool PreviewHUD::workerHandler(WorkerPoolWorker *worker) {
	PreviewHUD *self = (PreviewHUD *)worker->ptr;
	bool didWork = false;

	YerFace_MutexLock(self->myMutex);

	//// CHECK FOR WORK ////
	FrameNumber frameNumber = -1;
	//If there are preview frames waiting to be displayed, handle them.
	if(self->pendingFrameNumbers.size() > 0) {
		frameNumber = self->pendingFrameNumbers.front();
		self->pendingFrameNumbers.pop_front();
	}
	std::list<PreviewHUDRenderer> myRenderers = self->renderers; //Make a copy of this so we can operate on it while myMutex is unlocked.

	//Do not squat on myMutex while doing time-consuming work.
	YerFace_MutexUnlock(self->myMutex);

	//// DO THE WORK ////
	if(frameNumber > 0) {
		// self->logger->verbose("Thread #%d handling frame #" YERFACE_FRAMENUMBER_FORMAT, worker->num, frameNumber);
		MetricsTick tick = self->metrics->startClock();

		int density = self->status->getPreviewDebugDensity();

		WorkingFrame *previewFrame = self->frameServer->getWorkingFrame(frameNumber);
		YerFace_MutexLock(previewFrame->previewFrameMutex);
		for(auto renderer : myRenderers) {
			renderer(previewFrame->previewFrame, frameNumber, density);
		}
		YerFace_MutexUnlock(previewFrame->previewFrameMutex);

		self->frameServer->setWorkingFrameStatusCheckpoint(frameNumber, FRAME_STATUS_PREVIEW_RENDER, "previewHUD.ran");
		self->metrics->endClock(tick);

		didWork = true;
	}

	return didWork;
}

void PreviewHUD::handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps) {
	FrameNumber frameNumber = frameTimestamps.frameNumber;
	PreviewHUD *self = (PreviewHUD *)userdata;
	// self->logger->verbose("Handling Frame Status Change for Frame Number " YERFACE_FRAMENUMBER_FORMAT " to Status %d", frameNumber, newStatus);
	switch(newStatus) {
		default:
			throw logic_error("Handler passed unsupported frame status change event!");
		case FRAME_STATUS_PREVIEW_RENDER:
			YerFace_MutexLock(self->myMutex);
			self->pendingFrameNumbers.push_back(frameNumber);
			YerFace_MutexUnlock(self->myMutex);
			self->workerPool->sendWorkerSignal();
			break;
	}
}

} //namespace YerFace
