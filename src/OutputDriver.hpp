#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"

using namespace std;

namespace YerFace {

class OutputDriver {
public:
	OutputDriver(FrameDerivatives *myFrameDerivatives);
	~OutputDriver();
	void handleCompletedFrame(void);
private:
	FrameDerivatives *frameDerivatives;
	Logger *logger;
};

}; //namespace YerFace
