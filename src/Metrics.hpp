#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"

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

class FrameDerivatives;

class Metrics {
public:
	Metrics(const char *myName, FrameDerivatives *myFrameDerivatives, bool myMetricIsFrames = false, double myAverageOverSeconds = 1.0, double myReportEverySeconds = 0.0);
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
