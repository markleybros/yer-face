
#include "Metrics.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

void Metrics::startFrame(void) {
	timer = (double)getTickCount();
}

void Metrics::endFrame(void) {
	timer = ((double)getTickCount() - timer) / getTickFrequency();
	frameTimes.push_front(timer);
	averageTimeSeconds = 0.0;
	worstTimeSeconds = 0.0;
	int numTimes = 0;
	for(double frameTime : frameTimes) {
		averageTimeSeconds = averageTimeSeconds + frameTime;
		if(frameTime > worstTimeSeconds) {
			worstTimeSeconds = frameTime;
		}
		numTimes++;
	}
	averageTimeSeconds = averageTimeSeconds / (double)numTimes;
	while(frameTimes.size() > 30) {
		frameTimes.pop_back();
	}
}

double Metrics::getAverageTimeSeconds(void) {
	return averageTimeSeconds;
}

double Metrics::getWorstTimeSeconds(void) {
	return worstTimeSeconds;
}

} //namespace YerFace
