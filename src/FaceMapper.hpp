#pragma once

#include "Logger.hpp"
#include "SDLDriver.hpp"
#include "FrameDerivatives.hpp"
#include "FaceTracker.hpp"
#include "MarkerTracker.hpp"
#include "MarkerSeparator.hpp"
#include "Metrics.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class MarkerTracker;

class EyeRect {
public:
	Rect2d rect;
	bool set;
};

class FaceMapperWorkingVariables {
public:
	FacialFeatures features;
	EyeRect leftEye, rightEye;
};

class FaceMapperWorkerThread {
public:
	bool running, working;
	const char *name;
	SDL_Thread *thread;
	SDL_mutex *mutex;
	SDL_cond *condition;
	Logger *logger;
	std::vector<MarkerTracker *> trackers;
};

class FaceMapper {
public:
	FaceMapper(SDLDriver *mySDLDriver, FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker);
	~FaceMapper();
	void processCurrentFrame(void);
	void advanceWorkingToCompleted(void);
	void renderPreviewHUD(void);
	SDLDriver *getSDLDriver(void);
	FrameDerivatives *getFrameDerivatives(void);
	FaceTracker *getFaceTracker(void);
	MarkerSeparator *getMarkerSeparator(void);
	EyeRect getLeftEyeRect(void);
	EyeRect getRightEyeRect(void);
private:
	void calculateEyeRects(void);
	void initializeWorkerThread(FaceMapperWorkerThread *thread, const char *name);
	void destroyWorkerThread(FaceMapperWorkerThread *thread);
	static int workerThreadFunction(void* data);
	
	SDLDriver *sdlDriver;
	FrameDerivatives *frameDerivatives;
	FaceTracker *faceTracker;

	Logger *logger;
	SDL_mutex *myWrkMutex, *myCmpMutex;
	Metrics *metrics;

	MarkerSeparator *markerSeparator;

	MarkerTracker *markerEyelidLeftTop;
	MarkerTracker *markerEyelidRightTop;
	MarkerTracker *markerEyelidLeftBottom;
	MarkerTracker *markerEyelidRightBottom;

	MarkerTracker *markerEyebrowLeftInner;
	MarkerTracker *markerEyebrowLeftMiddle;
	MarkerTracker *markerEyebrowLeftOuter;
	MarkerTracker *markerEyebrowRightInner;
	MarkerTracker *markerEyebrowRightMiddle;
	MarkerTracker *markerEyebrowRightOuter;

	MarkerTracker *markerCheekLeft;
	MarkerTracker *markerCheekRight;

	MarkerTracker *markerJaw;

	MarkerTracker *markerLipsLeftCorner;
	MarkerTracker *markerLipsRightCorner;

	MarkerTracker *markerLipsLeftTop;
	MarkerTracker *markerLipsRightTop;
	
	MarkerTracker *markerLipsLeftBottom;
	MarkerTracker *markerLipsRightBottom;

	FaceMapperWorkingVariables working, complete;

	FaceMapperWorkerThread workerLeft, workerRight;
};

}; //namespace YerFace
