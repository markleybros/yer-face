#pragma once

#include "opencv2/core/utility.hpp"
#include <list>

using namespace std;

#define METRICS_STRING_LENGTH 256

namespace YerFace {

class Metrics {
public:
	Metrics(bool myMetricIsFrames = false, unsigned int mySampleBufferSize = 30);
	~Metrics();
	void startClock(void);
	void endClock(void);
	double getAverageTimeSeconds(void);
	double getWorstTimeSeconds(void);
	char *getTimesString(void);
	double getFPS(void);
	char *getFPSString(void);
private:
	bool metricIsFrames;
	unsigned int sampleBufferSize;
	double timer;
	list<double> processRunTimes;
	list<double> tickStartTimes;
	double averageTimeSeconds;
	double worstTimeSeconds;
	double fps;
	char timesString[METRICS_STRING_LENGTH], fpsString[METRICS_STRING_LENGTH];
};

}; //namespace YerFace
