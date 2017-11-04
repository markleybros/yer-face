#pragma once

#include "opencv2/core/utility.hpp"
#include <list>

using namespace std;

namespace YerFace {

class Metrics {
public:
	Metrics(unsigned int myFrameBufferSize);
	~Metrics();
	void startFrame(void);
	void endFrame(void);
	double getAverageTimeSeconds(void);
	double getWorstTimeSeconds(void);
	double getFPS(void);
private:
	unsigned int frameBufferSize;
	double timer;
	list<double> frameProcessTimes;
	list<double> frameTickStartTimes;
	double averageTimeSeconds;
	double worstTimeSeconds;
	double fps;
};

}; //namespace YerFace
