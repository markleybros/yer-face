
#include "FaceMapper.hpp"
#include "Utilities.hpp"
#include "opencv2/calib3d.hpp"

#include <cstdlib>

using namespace std;
using namespace cv;

namespace YerFace {

FaceMapper::FaceMapper(json config, Status *myStatus, FrameServer *myFrameServer, FaceTracker *myFaceTracker, PreviewHUD *myPreviewHUD) {
	workerPool = NULL;
	status = myStatus;
	if(status == NULL) {
		throw invalid_argument("status cannot be NULL");
	}
	frameServer = myFrameServer;
	if(frameServer == NULL) {
		throw invalid_argument("frameServer cannot be NULL");
	}
	faceTracker = myFaceTracker;
	if(faceTracker == NULL) {
		throw invalid_argument("faceTracker cannot be NULL");
	}
	previewHUD = myPreviewHUD;
	if(previewHUD == NULL) {
		throw invalid_argument("previewHUD cannot be NULL");
	}

	logger = new Logger("FaceMapper");
	metrics = new Metrics(config, "FaceMapper", frameServer);

	markerEyelidLeftTop = new MarkerTracker(config, EyelidLeftTop, this);
	markerEyelidRightTop = new MarkerTracker(config, EyelidRightTop, this);
	markerEyelidLeftBottom = new MarkerTracker(config, EyelidLeftBottom, this);
	markerEyelidRightBottom = new MarkerTracker(config, EyelidRightBottom, this);

	markerEyebrowLeftInner = new MarkerTracker(config, EyebrowLeftInner, this);
	markerEyebrowRightInner = new MarkerTracker(config, EyebrowRightInner, this);
	markerEyebrowLeftMiddle = new MarkerTracker(config, EyebrowLeftMiddle, this);
	markerEyebrowRightMiddle = new MarkerTracker(config, EyebrowRightMiddle, this);
	markerEyebrowLeftOuter = new MarkerTracker(config, EyebrowLeftOuter, this);
	markerEyebrowRightOuter = new MarkerTracker(config, EyebrowRightOuter, this);

	markerJaw = new MarkerTracker(config, Jaw, this);

	markerLipsLeftCorner = new MarkerTracker(config, LipsLeftCorner, this);
	markerLipsRightCorner = new MarkerTracker(config, LipsRightCorner, this);

	markerLipsLeftTop = new MarkerTracker(config, LipsLeftTop, this);
	markerLipsRightTop = new MarkerTracker(config, LipsRightTop, this);

	markerLipsLeftBottom = new MarkerTracker(config, LipsLeftBottom, this);
	markerLipsRightBottom = new MarkerTracker(config, LipsRightBottom, this);
	
	trackers = {
		markerEyelidLeftTop,
		markerEyelidLeftBottom,
		markerEyelidRightTop,
		markerEyelidRightBottom,
		markerEyebrowLeftInner,
		markerEyebrowLeftMiddle,
		markerEyebrowLeftOuter,
		markerEyebrowRightInner,
		markerEyebrowRightMiddle,
		markerEyebrowRightOuter,
		markerJaw,
		markerLipsLeftCorner,
		markerLipsRightCorner,
		markerLipsLeftTop,
		markerLipsRightTop,
		markerLipsLeftBottom,
		markerLipsRightBottom
	};

	pendingFrames.clear();

	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	//Hook into the frame lifecycle.

	//We want to know when any frame has entered various statuses.
	FrameStatusChangeEventCallback frameStatusChangeCallback;
	frameStatusChangeCallback.userdata = (void *)this;
	frameStatusChangeCallback.callback = handleFrameStatusChange;
	frameStatusChangeCallback.newStatus = FRAME_STATUS_NEW;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_MAPPING;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_GONE;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);

	//We also want to introduce a checkpoint so that frames cannot TRANSITION AWAY from FRAME_STATUS_MAPPING without our blessing.
	frameServer->registerFrameStatusCheckpoint(FRAME_STATUS_MAPPING, "faceMapper.ran");

	WorkerPoolParameters workerPoolParameters;
	workerPoolParameters.name = "FaceMapper";
	workerPoolParameters.numWorkers = 1; //FaceMapper (and MarkerTracker) cannot handle out-of-order frame processing.
	workerPoolParameters.numWorkersPerCPU = 0.0;
	workerPoolParameters.initializer = NULL;
	workerPoolParameters.deinitializer = NULL;
	workerPoolParameters.usrPtr = (void *)this;
	workerPoolParameters.handler = workerHandler;
	workerPool = new WorkerPool(config, status, frameServer, workerPoolParameters);

	logger->debug1("FaceMapper object constructed and ready to go!");
}

FaceMapper::~FaceMapper() noexcept(false) {
	logger->debug1("FaceMapper object destructing...");

	delete workerPool;

	YerFace_MutexLock(myMutex);
	if(pendingFrames.size() > 0) {
		logger->err("Frames are still pending! Woe is me!");
	}
	YerFace_MutexUnlock(myMutex);

	for(MarkerTracker *markerTracker : trackers) {
		if(markerTracker != NULL) {
			delete markerTracker;
		}
	}
	SDL_DestroyMutex(myMutex);
	delete metrics;
	delete logger;
}

void FaceMapper::renderPreviewHUD(Mat frame, FrameNumber frameNumber, int density) {
	for(MarkerTracker *markerTracker : trackers) {
		markerTracker->renderPreviewHUD(frame, frameNumber, density);
	}
	if(density > 0) {
		int gridIncrement = 15; //FIXME - magic numbers
		Rect2d previewRect;
		Point2d previewCenter;
		previewHUD->createPreviewHUDRectangle(frame.size(), &previewRect, &previewCenter);
		double previewPointScale = previewRect.width / 200;
		rectangle(frame, previewRect, Scalar(20, 20, 20), FILLED);
		if(density > 4) {
			for(int x = (int)previewRect.x; x < (int)(previewRect.x + previewRect.width); x = x + gridIncrement) {
				cv::line(frame, Point2d(x, previewRect.y), Point2d(x, previewRect.y + previewRect.height), Scalar(75, 75, 75));
			}
			for(int y = (int)previewRect.y; y < (int)(previewRect.y + previewRect.height); y = y + gridIncrement) {
				cv::line(frame, Point2d(previewRect.x, y), Point2d(previewRect.x + previewRect.width, y), Scalar(75, 75, 75));
			}
		}
		for(MarkerTracker *markerTracker : trackers) {
			MarkerPoint markerPoint = markerTracker->getMarkerPoint(frameNumber);
			if(markerPoint.set) {
				Point2d previewPoint = Point2d(
						(markerPoint.point3d.x * previewPointScale) + previewCenter.x,
						(markerPoint.point3d.y * previewPointScale) + previewCenter.y);
				Utilities::drawX(frame, previewPoint, Scalar(255, 255, 255));
			}
		}
	}
}

FrameServer *FaceMapper::getFrameServer(void) {
	return frameServer;
}

FaceTracker *FaceMapper::getFaceTracker(void) {
	return faceTracker;
}

void FaceMapper::handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps) {
	FrameNumber frameNumber = frameTimestamps.frameNumber;
	FaceMapper *self = (FaceMapper *)userdata;
	FaceMapperPendingFrame newFrame;
	self->logger->debug4("Handling Frame Status Change for Frame Number " YERFACE_FRAMENUMBER_FORMAT " to Status %d", frameNumber, newStatus);
	switch(newStatus) {
		default:
			throw logic_error("Handler passed unsupported frame status change event!");
		case FRAME_STATUS_NEW:
			newFrame.frameNumber = frameNumber;
			newFrame.hasEnteredMapping = false;
			newFrame.hasCompletedMapping = false;
			YerFace_MutexLock(self->myMutex);
			self->pendingFrames[frameNumber] = newFrame;
			YerFace_MutexUnlock(self->myMutex);
			for(MarkerTracker *markerTracker : self->trackers) {
				markerTracker->frameStatusNew(frameNumber);
			}
			break;
		case FRAME_STATUS_MAPPING:
			self->logger->debug4("handleFrameStatusChange() Frame #" YERFACE_FRAMENUMBER_FORMAT " entered MAPPING.", frameNumber);
			YerFace_MutexLock(self->myMutex);
			self->pendingFrames[frameNumber].hasEnteredMapping = true;
			YerFace_MutexUnlock(self->myMutex);
			if(self->workerPool != NULL) {
				self->workerPool->sendWorkerSignal();
			}
			break;
		case FRAME_STATUS_GONE:
			YerFace_MutexLock(self->myMutex);
			self->pendingFrames.erase(frameNumber);
			YerFace_MutexUnlock(self->myMutex);
			for(MarkerTracker *markerTracker : self->trackers) {
				markerTracker->frameStatusGone(frameNumber);
			}
			break;
	}
}

bool FaceMapper::workerHandler(WorkerPoolWorker *worker) {
	FaceMapper *self = (FaceMapper *)worker->ptr;
	bool didWork = false;
	static FrameNumber lastFrameNumber = -1;

	YerFace_MutexLock(self->myMutex);
	//// CHECK FOR WORK ////
	FrameNumber myFrameNumber = -1;
	FrameNumber lowestPendingFrameNumber = -1;
	for(auto pendingFramePair : self->pendingFrames) {
		FaceMapperPendingFrame *pendingFrame = &pendingFramePair.second;
		if((lowestPendingFrameNumber < 0 || pendingFrame->frameNumber < lowestPendingFrameNumber) && !pendingFrame->hasCompletedMapping) {
			lowestPendingFrameNumber = pendingFrame->frameNumber;
		}
	}
	if(lowestPendingFrameNumber > 0 && self->pendingFrames[lowestPendingFrameNumber].hasEnteredMapping) {
		myFrameNumber = lowestPendingFrameNumber;
	}
	YerFace_MutexUnlock(self->myMutex);

	//// DO THE WORK ////
	if(myFrameNumber > 0) {
		self->logger->debug4("Thread #%d handling frame #" YERFACE_FRAMENUMBER_FORMAT, worker->num, myFrameNumber);
		if(myFrameNumber <= lastFrameNumber) {
			throw logic_error("FaceMapper handling frames out of order!");
		}
		lastFrameNumber = myFrameNumber;

		MetricsTick tick = self->metrics->startClock();
		for(MarkerTracker *tracker : self->trackers) {
			tracker->processFrame(myFrameNumber);
		}
		self->metrics->endClock(tick);

		self->frameServer->setWorkingFrameStatusCheckpoint(myFrameNumber, FRAME_STATUS_MAPPING, "faceMapper.ran");
		YerFace_MutexLock(self->myMutex);
		self->pendingFrames[myFrameNumber].hasCompletedMapping = true;
		YerFace_MutexUnlock(self->myMutex);
		didWork = true;
	}
	return didWork;
}

}; //namespace YerFace
