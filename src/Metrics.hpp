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

class MetricsTick {
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
	MetricsTick startClock(void);
	void endClock(MetricsTick tick);
	double getAverageTimeSeconds(void);
	double getWorstTimeSeconds(void);
	double getFPS(void);
	std::string getTimesString(void);
	std::string getFPSString(void);
private:
	void logReportNow(string prefix);

	string name;
	FrameDerivatives *frameDerivatives;
	bool metricIsFrames;
	double averageOverSeconds, reportEverySeconds;
	double lastReport;

	Logger *logger;
	SDL_mutex *myMutex;
	list<MetricsTick> entries;
	double averageTimeSeconds;
	double worstTimeSeconds;
	double fps;
	char timesString[METRICS_STRING_LENGTH], fpsString[METRICS_STRING_LENGTH];
};

}; //namespace YerFace
