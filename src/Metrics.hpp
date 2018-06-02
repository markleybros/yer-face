#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"
#include "Utilities.hpp"

#include "SDL.h"

#include "opencv2/core/utility.hpp"
#include <list>

using namespace std;

namespace YerFace {

#define METRICS_STRING_LENGTH 256

class MetricEntry {
public:
	double startTime;
	double runTime;
	double frameTimestamp;
};

class FrameDerivatives;

class Metrics {
public:
	Metrics(json config, const char *myName, FrameDerivatives *myFrameDerivatives, bool myMetricIsFrames = false);
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
	FrameDerivatives *frameDerivatives;
	bool metricIsFrames;
	double averageOverSeconds, reportEverySeconds;
	double lastReport;

	Logger *logger;
	SDL_mutex *myMutex;
	list<MetricEntry> entries;
	double averageTimeSeconds;
	double worstTimeSeconds;
	double fps;
	char timesString[METRICS_STRING_LENGTH], fpsString[METRICS_STRING_LENGTH];
};

}; //namespace YerFace
