
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

double Utilities::degreesToRadians(double degrees) {
	return degrees * (M_PI / 180.0);
}

double Utilities::radiansToDegrees(double radians, bool normalize) {
	double degrees = radians * (180 / M_PI);
	if(normalize) {
		while(degrees < 0.0) {
			degrees += 360.0;
		}
	}
	return degrees;
}

Vec3d Utilities::rotationMatrixToEulerAngles(Mat &R, bool returnDegrees) {
	double sy = std::sqrt(R.at<double>(0,0) * R.at<double>(0,0) +  R.at<double>(1,0) * R.at<double>(1,0));

	double x, y, z;
	if(sy > 0.0) {
		x = atan2(R.at<double>(2,1) , R.at<double>(2,2));
		y = atan2(-R.at<double>(2,0), sy);
		z = atan2(R.at<double>(1,0), R.at<double>(0,0));
	} else {
		x = atan2(-R.at<double>(1,2), R.at<double>(1,1));
		y = atan2(-R.at<double>(2,0), sy);
		z = 0;
	}
	if(returnDegrees) {
		return Vec3d(Utilities::radiansToDegrees(x), Utilities::radiansToDegrees(y), Utilities::radiansToDegrees(z));
	}
	return Vec3d(x, y, z);
}

Mat Utilities::eulerAnglesToRotationMatrix(Vec3d &R, bool expectDegrees) {
	Vec3d rot;
	if(expectDegrees) {
		rot = Vec3d(Utilities::degreesToRadians(R[0]), Utilities::degreesToRadians(R[1]), Utilities::degreesToRadians(R[2]));
	} else {
		rot = Vec3d(R);
	}

	Mat matrixX = (Mat_<double>(3,3) <<
		1.0, 0.0,          0.0,
		0.0, cos(rot[0]), -sin(rot[0]),
		0.0, sin(rot[0]),  cos(rot[0]));

	Mat matrixY = (Mat_<double>(3,3) <<
		 cos(rot[1]), 0.0, sin(rot[1]),
		 0.0,         1.0, 0.0,
		-sin(rot[1]), 0.0, cos(rot[1]));

	Mat matrixZ = (Mat_<double>(3,3) <<
		cos(rot[2]), -sin(rot[2]), 0.0,
		sin(rot[2]),  cos(rot[2]), 0.0,
		0.0,          0.0,         1.0);

	Mat matrix = matrixZ * matrixY * matrixX;
	return matrix;
}

Mat Utilities::generateFakeCameraMatrix(double focalLength, Point2d principalPoint) {
	return (Mat_<double>(3,3) <<
		focalLength, 0.0,         principalPoint.x,
		0.0,         focalLength, principalPoint.y,
		0.0,         0.0,         1.0);
}

bool Utilities::rayPlaneIntersection(Point3d &intersection, Point3d rayOrigin, Vec3d rayVector, Point3d planePoint, Vec3d planeNormal) {
	Vec3d rayVectorNormalized = normalize(rayVector);

	Mat rayOriginMat, rayVectorMat, planeNormalMat, planePointMat;
	rayOriginMat = (Mat_<double>(3, 1) << rayOrigin.x, rayOrigin.y, rayOrigin.z);
	rayVectorMat = (Mat_<double>(3, 1) << rayVectorNormalized[0], rayVectorNormalized[1], rayVectorNormalized[2]);
	planePointMat = (Mat_<double>(3, 1) << planePoint.x, planePoint.y, planePoint.z);
	planeNormalMat = (Mat_<double>(3, 1) << planeNormal[0], planeNormal[1], planeNormal[2]);


	if(planeNormalMat.dot(rayVectorMat) == 0) {
		return false;
	}

	double d = planeNormalMat.dot(planePointMat);
	double t = (d - planeNormalMat.dot(rayOriginMat)) / planeNormalMat.dot(rayVectorMat);

	intersection.x = rayOrigin.x + (rayVectorNormalized[0] * t);
	intersection.y = rayOrigin.y + (rayVectorNormalized[1] * t);
	intersection.z = rayOrigin.z + (rayVectorNormalized[2] * t);
	return true;
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

cv::Scalar Utilities::scalarColorFromJSONArray(String jsonArrayString) {
	json j = json::parse(jsonArrayString);
	return Utilities::scalarColorFromJSONArray(j);
}

cv::Scalar Utilities::scalarColorFromJSONArray(json jsonArray) {
	if(!jsonArray.is_array() || jsonArray.size() != 3) {
		throw invalid_argument("jsonArray must be an array with three ints");
	}
	Scalar s;
	for(int i = 0; i < 3; i++) {
		if(!jsonArray[i].is_number_integer() || jsonArray[i] < 0 || jsonArray[i] > 255) {
			throw invalid_argument("color scalar must be three ints between 0-255, but jsonArray contains an element which is not a valid number");
		}
		s[i] = jsonArray[i];
	}
	return s;
}

}; //namespace YerFace
