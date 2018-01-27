#pragma once

#include "Logger.hpp"

#include "SDL.h"

#include "opencv2/core/utility.hpp"
#include <list>

using namespace std;

#define METRICS_STRING_LENGTH 256

namespace YerFace {

class MetricEntry {
public:
	double startTime;
	double runTime;
	double frameTimestamp;
};

class Metrics {
public:
	Metrics(const char *myName, bool myMetricIsFrames = false, unsigned int mySampleBufferSize = 30);
	~Metrics();
	void startClock(void);
	void endClock(void);
	double getAverageTimeSeconds(void);
	double getWorstTimeSeconds(void);
	double getFPS(void);
	std::string getTimesString(void);
	std::string getFPSString(void);
private:
	string name;
	bool metricIsFrames;
	unsigned int sampleBufferSize;

	Logger *logger;
	SDL_mutex *myMutex;
	list<MetricEntry> entries;
	double averageTimeSeconds;
	double worstTimeSeconds;
	double fps;
	char timesString[METRICS_STRING_LENGTH], fpsString[METRICS_STRING_LENGTH];
};

}; //namespace YerFace
