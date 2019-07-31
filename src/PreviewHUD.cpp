
#include "PreviewHUD.hpp"
#include "Utilities.hpp"

#include <math.h>

using namespace std;
using namespace cv;

namespace YerFace {

PreviewHUD::PreviewHUD(json config, Status *myStatus, FrameServer *myFrameServer, bool myMirrorMode) {
	status = myStatus;
	if(status == NULL) {
		throw invalid_argument("status cannot be NULL");
	}
	frameServer = myFrameServer;
	if(frameServer == NULL) {
		throw invalid_argument("frameServer cannot be NULL");
	}
	mirrorMode = myMirrorMode;
	frameServer->setMirrorMode(mirrorMode);
	logger = new Logger("PreviewHUD");
	metrics = new Metrics(config, "PreviewHUD", true);

	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	status->setPreviewDebugDensity(config["YerFace"]["PreviewHUD"]["initialPreviewDisplayDensity"]);
	previewRatio = config["YerFace"]["PreviewHUD"]["previewRatio"];
	previewWidthPercentage = config["YerFace"]["PreviewHUD"]["previewWidthPercentage"];
	previewCenterHeightPercentage = config["YerFace"]["PreviewHUD"]["previewCenterHeightPercentage"];

	logger->debug1("PreviewHUD object constructed with Mirror Mode %s and ready to go.", mirrorMode ? "ENABLED" : "DISABLED");
}

PreviewHUD::~PreviewHUD() noexcept(false) {
	logger->debug1("PreviewHUD object destructing...");

	SDL_DestroyMutex(myMutex);
	delete metrics;
	delete logger;
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

void PreviewHUD::doRenderPreviewHUD(cv::Mat previewFrame, FrameNumber frameNumber) {
	MetricsTick tick = metrics->startClock();

	YerFace_MutexLock(myMutex);

	int density = status->getPreviewDebugDensity();

	for(PreviewHUDRenderer renderer : renderers) {
		renderer(previewFrame, frameNumber, density, mirrorMode);
	}

	YerFace_MutexUnlock(myMutex);

	metrics->endClock(tick);
}

} //namespace YerFace
