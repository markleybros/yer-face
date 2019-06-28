
#include "Metrics.hpp"
#include "Utilities.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

Metrics::Metrics(json config, const char *myName, bool myMetricIsFrames) {
	name = (string)myName;
	metricIsFrames = myMetricIsFrames;
	averageOverSeconds = config["YerFace"]["Metrics"]["averageOverSeconds"];
	if(averageOverSeconds <= 0.0) {
		throw invalid_argument("averageOverSeconds cannot be less than or equal to zero");
	}
	reportEverySeconds = config["YerFace"]["Metrics"]["reportEverySeconds"];
	if(averageOverSeconds < 1.0) {
		throw invalid_argument("reportEverySeconds cannot be less than one");
	}
	lastReport = 0.0;
	averageTimeSeconds = 0.0;
	worstTimeSeconds = 0.0;
	fps = 0.0;
	snprintf(timesString, METRICS_STRING_LENGTH, "N/A");
	snprintf(fpsString, METRICS_STRING_LENGTH, "N/A");
	string loggerName = "Metrics<" + name + ">";
	logger = new Logger(loggerName.c_str());
	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	logger->debug1("Metrics object constructed and ready to go!");
}

Metrics::~Metrics() {
	logger->debug1("Metrics object destructing...");
	logReportNow("FINAL REPORT: ");
	SDL_DestroyMutex(myMutex);
	delete logger;
}

MetricsTick Metrics::startClock(void) {
	MetricsTick tick;
	tick.startTime = (double)getTickCount() / (double)getTickFrequency();
	return tick;
}

void Metrics::endClock(MetricsTick tick) {
	double now = (double)getTickCount() / (double)getTickFrequency();
	tick.runTime = now - tick.startTime;

	YerFace_MutexLock(myMutex);

	entries.push_front(tick);

	while(entries.back().startTime <= (tick.startTime - averageOverSeconds)) {
		entries.pop_back();
	}

	averageTimeSeconds = 0.0;
	worstTimeSeconds = 0.0;
	size_t numEntries = entries.size();
	for(MetricsTick entry : entries) {
		averageTimeSeconds = averageTimeSeconds + entry.runTime;
		if(entry.runTime > worstTimeSeconds) {
			worstTimeSeconds = entry.runTime;
		}
	}
	averageTimeSeconds = averageTimeSeconds / (double)numEntries;
	snprintf(timesString, METRICS_STRING_LENGTH, "Times: <Avg %.02fms, Worst %.02fms>", averageTimeSeconds * 1000.0, worstTimeSeconds * 1000.0);
	// snprintf(timesString, METRICS_STRING_LENGTH, "Times: <Avg %.02fms, Worst %.02fms> (%lu samples)", averageTimeSeconds * 1000.0, worstTimeSeconds * 1000.0, numEntries);
	string fpsPrefix;
	if(metricIsFrames) {
		fpsPrefix = "Frames/Sec:";
	} else {
		fpsPrefix = "Tasks/Sec:";
	}
	if(numEntries > 1) {
		fps = 1.0 / ((now - entries.back().startTime) / numEntries);
	} else {
		fps = 0.0;
	}
	snprintf(fpsString, METRICS_STRING_LENGTH, "%s <%.02f>", fpsPrefix.c_str(), fps);

	if(lastReport + reportEverySeconds <= now) {
		logReportNow("");
		lastReport = now;
	}
	YerFace_MutexUnlock(myMutex);
}

void Metrics::logReportNow(string prefix) {
	logger->debug1("%s%s, %s", prefix.c_str(), fpsString, timesString);
}

double Metrics::getAverageTimeSeconds(void) {
	YerFace_MutexLock(myMutex);
	double status = averageTimeSeconds;
	YerFace_MutexUnlock(myMutex);
	return status;
}

double Metrics::getWorstTimeSeconds(void) {
	YerFace_MutexLock(myMutex);
	double status = worstTimeSeconds;
	YerFace_MutexUnlock(myMutex);
	return status;
}

double Metrics::getFPS(void) {
	YerFace_MutexLock(myMutex);
	double status = fps;
	YerFace_MutexUnlock(myMutex);
	return status;
}

std::string Metrics::getTimesString(void) {
	YerFace_MutexLock(myMutex);
	std::string str = (std::string)timesString;
	YerFace_MutexUnlock(myMutex);
	return str;
}

std::string Metrics::getFPSString(void) {
	YerFace_MutexLock(myMutex);
	std::string str = (std::string)fpsString;
	YerFace_MutexUnlock(myMutex);
	return str;
}

} //namespace YerFace
