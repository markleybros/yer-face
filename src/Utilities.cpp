
#include "Utilities.hpp"
#define _USE_MATH_DEFINES
#include <cmath>

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
	return Rect2d(
		originalBox.x + ((originalBox.width - newBoxWidth) / 2.0),
		originalBox.y + ((originalBox.height - newBoxHeight) / 2.0),
		newBoxWidth,
		newBoxHeight);
}

Point2d Utilities::centerRect(Rect2d rect) {
	Point2d center = Point(rect.width / 2.0, rect.height / 2.0);
	return rect.tl() + center;
}

double Utilities::lineDistance(Point2d a, Point2d b) {
    Point2d d = a - b;
    return std::sqrt(std::pow(d.x, 2.0) + std::pow(d.y, 2.0));
}

Point2d Utilities::adjustLineDistance(Point2d a, Point2d b, double newDistance) {
	Point2d temp;
	double lenAB = std::sqrt(std::pow(a.x - b.x, 2.0) + pow(a.y - b.y, 2.0));
	temp.x = a.x + ((b.x - a.x) / lenAB) * newDistance;
	temp.y = a.y + ((b.y - a.y) / lenAB) * newDistance;
	return temp;
}

double Utilities::lineAngle(Point2d a, Point2d b) {
	Point2d delta = b - a;
	double radians = atan2(delta.y, delta.x);
	return radians * (180 / M_PI);
}

void Utilities::lineBestFit(vector<Point2d> points, double *m, double *b) {
	Vec4d line;
	fitLine(points, line, DIST_L2, 0, 0.01, 0.01);

	Point2d pointA = Point2d(line[2],line[3]);
	Point2d pointB = Point2d(line[2]+line[0],line[3]+line[1]);

	*m = (pointA.y - pointB.y) / (pointA.x - pointB.x);
	int classification = fpclassify(*m);
	if(classification == FP_NAN || classification == FP_INFINITE) {
		*m = 0;
	}
	*b = pointA.y - (*m * pointA.x);
}

void Utilities::drawRotatedRectOutline(Mat frame, RotatedRect rrect, Scalar color, int thickness) {
	Point2f vertices[4];
	rrect.points(vertices);
	for(int i = 0; i < 4; i++) {
		line(frame, vertices[i], vertices[(i+1)%4], color, thickness);
	}
}

void Utilities::drawX(Mat frame, Point2d markerPoint, Scalar color, int lineLength, int thickness) {
	Point2d a, b;
	a.x = markerPoint.x;
	a.y = markerPoint.y - lineLength;
	b.x = markerPoint.x;
	b.y = markerPoint.y + lineLength;
	line(frame, a, b, color, thickness);
	a.x = markerPoint.x - lineLength;
	a.y = markerPoint.y;
	b.x = markerPoint.x + lineLength;
	b.y = markerPoint.y;
	line(frame, a, b, color, thickness);
}


}; //namespace YerFace
