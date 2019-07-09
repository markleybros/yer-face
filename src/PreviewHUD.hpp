#pragma once

#include "Logger.hpp"
#include "Utilities.hpp"
#include "FrameServer.hpp"
#include "Metrics.hpp"
#include "WorkerPool.hpp"

#include <list>

using namespace std;

namespace YerFace {

typedef function<void(cv::Mat previewFrame, FrameNumber frameNumber, int density, bool mirrorMode)> PreviewHUDRenderer;

class PreviewHUD {
public:
	PreviewHUD(json config, Status *myStatus, FrameServer *myFrameServer, bool myMirrorMode);
	~PreviewHUD() noexcept(false);
	void registerPreviewHUDRenderer(PreviewHUDRenderer renderer);
	void createPreviewHUDRectangle(cv::Size frameSize, cv::Rect2d *previewRect, cv::Point2d *previewCenter);
	void doRenderPreviewHUD(cv::Mat previewFrame, FrameNumber frameNumber);
private:

	Status *status;
	FrameServer *frameServer;

	bool mirrorMode;

	Metrics *metrics;
	Logger *logger;
	SDL_mutex *myMutex;

	double previewRatio, previewWidthPercentage, previewCenterHeightPercentage;

	std::list<PreviewHUDRenderer> renderers;
};

}; //namespace YerFace
