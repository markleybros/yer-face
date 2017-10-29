#pragma once

#include "opencv2/core/utility.hpp"
#include <list>

using namespace std;

namespace YerFace {

class Metrics {
public:
	void startFrame(void);
	void endFrame(void);
	double getAverageTimeSeconds(void);
	double getWorstTimeSeconds(void);
private:
	double timer;
	list<double> frameTimes;
	double averageTimeSeconds;
	double worstTimeSeconds;
};

}; //namespace YerFace
