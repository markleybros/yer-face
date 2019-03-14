#pragma once

#include "Logger.hpp"
#include "Utilities.hpp"
#include "FrameServer.hpp"
#include "Metrics.hpp"
#include "WorkerPool.hpp"

#include <list>

using namespace std;

namespace YerFace {

typedef function<void(Mat previewFrame, FrameNumber frameNumber, int density)> PreviewHUDRenderer;

class PreviewHUD {
public:
	PreviewHUD(json config, Status *myStatus, FrameServer *myFrameServer);
	~PreviewHUD() noexcept(false);
	void registerPreviewHUDRenderer(PreviewHUDRenderer renderer);
	void createPreviewHUDRectangle(Size frameSize, Rect2d *previewRect, Point2d *previewCenter);
private:
	static bool workerHandler(WorkerPoolWorker *worker);
	static void handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps);

	Status *status;
	FrameServer *frameServer;

	Metrics *metrics;
	Logger *logger;
	SDL_mutex *myMutex;
	std::list<FrameNumber> pendingFrameNumbers;

	double previewRatio, previewWidthPercentage, previewCenterHeightPercentage;

	std::list<PreviewHUDRenderer> renderers;

	WorkerPool *workerPool;
};

}; //namespace YerFace
