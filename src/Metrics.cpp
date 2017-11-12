
#include "Metrics.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

Metrics::Metrics(unsigned int myFrameBufferSize) {
	frameBufferSize = myFrameBufferSize;
	fprintf(stderr, "Metrics object constructed and ready to go!\n");
}

Metrics::~Metrics() {
	fprintf(stderr, "Metrics object destructing...\n");
}

void Metrics::startFrame(void) {
	timer = (double)getTickCount();
	frameTickStartTimes.push_front(timer);
}

void Metrics::endFrame(void) {
	double now = (double)getTickCount();
	timer = (now - timer) / getTickFrequency();
	frameProcessTimes.push_front(timer);
	averageTimeSeconds = 0.0;
	worstTimeSeconds = 0.0;
	int numTimes = 0;
	for(double frameTime : frameProcessTimes) {
		averageTimeSeconds = averageTimeSeconds + frameTime;
		if(frameTime > worstTimeSeconds) {
			worstTimeSeconds = frameTime;
		}
		numTimes++;
	}
	averageTimeSeconds = averageTimeSeconds / (double)numTimes;
	fps = 1.0 / (((now - frameTickStartTimes.back()) / getTickFrequency()) / frameTickStartTimes.size());
	while(frameProcessTimes.size() >= frameBufferSize) {
		frameProcessTimes.pop_back();
	}
	while(frameTickStartTimes.size() >= frameBufferSize) {
		frameTickStartTimes.pop_back();
	}
	snprintf(timesString, METRICS_STRING_LENGTH, "Times: <Avg %.02fms, Worst %.02fms>", averageTimeSeconds * 1000.0, worstTimeSeconds * 1000.0);
	snprintf(fpsString, METRICS_STRING_LENGTH, "FPS: <%.02f>", fps);
	fprintf(stderr, "Metrics... %s %s\n", fpsString, timesString);
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
