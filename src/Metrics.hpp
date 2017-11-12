#pragma once

#include "opencv2/core/utility.hpp"
#include <list>

using namespace std;

#define METRICS_STRING_LENGTH 256

namespace YerFace {

class Metrics {
public:
	Metrics(unsigned int myFrameBufferSize);
	~Metrics();
	void startFrame(void);
	void endFrame(void);
	double getAverageTimeSeconds(void);
	double getWorstTimeSeconds(void);
	char *getTimesString(void);
	double getFPS(void);
	char *getFPSString(void);
private:
	unsigned int frameBufferSize;
	double timer;
	list<double> frameProcessTimes;
	list<double> frameTickStartTimes;
	double averageTimeSeconds;
	double worstTimeSeconds;
	double fps;
	char timesString[METRICS_STRING_LENGTH], fpsString[METRICS_STRING_LENGTH];
};

}; //namespace YerFace
