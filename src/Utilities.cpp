#include "Utilities.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

Rect2d Utilities::scaleRect(Rect2d rect, double scale) {
	return Rect2d(
		rect.x * scale,
		rect.y * scale,
		rect.width * scale,
		rect.height * scale);
}

Rect2d Utilities::insetBox(Rect2d originalBox, double scale) {
	double newBoxWidth = originalBox.width * scale;
	double newBoxHeight = originalBox.height * scale;
	return  Rect2d(
		originalBox.x + ((originalBox.width - newBoxWidth) / 2.0),
		originalBox.y + ((originalBox.height - newBoxHeight) / 2.0),
		newBoxWidth,
		newBoxHeight);
}

}; //namespace YerFace
