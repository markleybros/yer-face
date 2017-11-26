
#include "Metrics.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

Metrics::Metrics(bool myMetricIsFrames, unsigned int mySampleBufferSize) {
	metricIsFrames = myMetricIsFrames;
	sampleBufferSize = mySampleBufferSize;
	averageTimeSeconds = 0.0;
	worstTimeSeconds = 0.0;
	fps = 0.0;
	snprintf(timesString, METRICS_STRING_LENGTH, "N/A");
	snprintf(fpsString, METRICS_STRING_LENGTH, "N/A");
	fprintf(stderr, "Metrics object constructed and ready to go!\n");
}

Metrics::~Metrics() {
	fprintf(stderr, "Metrics object destructing...\n");
}

void Metrics::startClock(void) {
	timer = (double)getTickCount();
	tickStartTimes.push_front(timer);
}

void Metrics::endClock(void) {
	double now = (double)getTickCount();
	timer = (now - timer) / getTickFrequency();
	processRunTimes.push_front(timer);
	averageTimeSeconds = 0.0;
	worstTimeSeconds = 0.0;
	int numTimes = 0;
	for(double frameTime : processRunTimes) {
		averageTimeSeconds = averageTimeSeconds + frameTime;
		if(frameTime > worstTimeSeconds) {
			worstTimeSeconds = frameTime;
		}
		numTimes++;
	}
	averageTimeSeconds = averageTimeSeconds / (double)numTimes;
	snprintf(timesString, METRICS_STRING_LENGTH, "Times: <Avg %.02fms, Worst %.02fms>", averageTimeSeconds * 1000.0, worstTimeSeconds * 1000.0);
	if(metricIsFrames) {
		fps = 1.0 / (((now - tickStartTimes.back()) / getTickFrequency()) / tickStartTimes.size());
		snprintf(fpsString, METRICS_STRING_LENGTH, "FPS: <%.02f>", fps);
	}
	while(processRunTimes.size() > sampleBufferSize) {
		processRunTimes.pop_back();
	}
	while(tickStartTimes.size() > sampleBufferSize) {
		tickStartTimes.pop_back();
	}
	// fprintf(stderr, "Metrics... %s %s\n", fpsString, timesString);
}

double Metrics::getAverageTimeSeconds(void) {
	return averageTimeSeconds;
}

double Metrics::getWorstTimeSeconds(void) {
	return worstTimeSeconds;
}

double Metrics::getFPS(void) {
	return fps;
}

char *Metrics::getTimesString(void) {
	return timesString;
}

char *Metrics::getFPSString(void) {
	return fpsString;
}

} //namespace YerFace
