#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"
#include "FaceTracker.hpp"

using namespace std;

namespace YerFace {

class OutputDriver {
public:
	OutputDriver(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker);
	~OutputDriver();
	void handleCompletedFrame(void);
private:
	FrameDerivatives *frameDerivatives;
	FaceTracker *faceTracker;
	Logger *logger;
};

}; //namespace YerFace
