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

Point2d Utilities::centerRect(Rect2d rect) {
	Point2d center = Point(rect.width / 2.0, rect.height / 2.0);
	return rect.tl() + center;
}

double Utilities::distance(Point2d a, Point2d b) {
    Point2d d = a - b;
    return std::sqrt(d.x*d.x + d.y*d.y);
}

void Utilities::drawRotatedRectOutline(Mat frame, RotatedRect rrect, Scalar color, int thickness) {
	Point2f vertices[4];
	rrect.points(vertices);
	for(int i = 0; i < 4; i++) {
		line(frame, vertices[i], vertices[(i+1)%4], color, thickness);
	}
}

}; //namespace YerFace
