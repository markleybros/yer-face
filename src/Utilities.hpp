#pragma once

#include "opencv2/core/types.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class Utilities {
public:
	static Rect2d scaleRect(Rect2d rect, double scale);
	static Rect2d insetBox(Rect2d originalBox, double scale);
};

}; //namespace YerFace
