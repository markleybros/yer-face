#pragma once

#include "opencv2/imgproc.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

class Utilities {
public:
	static Rect2d scaleRect(Rect2d rect, double scale);
	static Rect2d insetBox(Rect2d originalBox, double scale);
	static Point2d centerRect(Rect2d rect);
	static double lineDistance(Point2d a, Point2d b);
	static Point2d adjustLineDistance(Point2d a, Point2d b, double newDistance);
	static double lineAngle(Point2d a, Point2d b);
	static void leastSquaresFit(vector<Point2d> points, double *m, double *b);
	static void drawRotatedRectOutline(Mat frame, RotatedRect rrect, Scalar color = Scalar(0, 0, 255), int thickness = 1);
	static void drawX(Mat frame, Point2d markerPoint, Scalar color = Scalar(0, 0, 255), int lineLength = 5, int thickness = 1);
};

}; //namespace YerFace
