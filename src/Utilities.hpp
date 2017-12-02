#pragma once

#include "SDL.h"

#include "opencv2/imgproc.hpp"

#include <exception>

using namespace std;
using namespace cv;

namespace YerFace {

#define YerFace_MutexLock(X) do { if(SDL_LockMutex(X) != 0) { throw runtime_error("Failed to lock mutex."); } } while(0)
#define YerFace_MutexUnlock(X) do { if(SDL_UnlockMutex(X) != 0) { throw runtime_error("Failed to unlock mutex."); } } while(0)

class Utilities {
public:
	static Rect2d scaleRect(Rect2d rect, double scale);
	static Rect2d insetBox(Rect2d originalBox, double scale);
	static Point2d centerRect(Rect2d rect);
	static double lineDistance(Point2d a, Point2d b);
	static double degreesToRadians(double degrees);
	static double radiansToDegrees(double radians, bool normalize = true);
	static Vec3d rotationMatrixToEulerAngles(Mat &R, bool returnDegrees = true);
	static Mat eulerAnglesToRotationMatrix(Vec3d &R, bool expectDegrees = true);
	static Mat generateFakeCameraMatrix(double focalLength = 1.0, Point2d principalPoint = Point2d(0, 0));
	static bool rayPlaneIntersection(Point3d &intersection, Point3d rayOrigin, Vec3d rayVector, Point3d planePoint, Vec3d planeNormal);
	static void drawRotatedRectOutline(Mat frame, RotatedRect rrect, Scalar color = Scalar(0, 0, 255), int thickness = 1);
	static void drawX(Mat frame, Point2d markerPoint, Scalar color = Scalar(0, 0, 255), int lineLength = 5, int thickness = 1);
};

}; //namespace YerFace
