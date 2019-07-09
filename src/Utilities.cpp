
#include "Utilities.hpp"
#define _USE_MATH_DEFINES
#include <cmath>
#include <sys/stat.h>
#include <regex>

using namespace std;
using namespace cv;

namespace YerFace {

double Utilities::normalize(double x, double length) {
	return (1.0/length) * x;
}

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
	Point2d center = Point2d(rect.width / 2.0, rect.height / 2.0);
	return rect.tl() + center;
}

Point2d Utilities::averagePoint(vector<Point2d> points) {
	Point2d result = Point2d(0.0, 0.0);
	double scale = (double)1.0 / (double)points.size();
	for(Point2d point : points) {
		result.x += point.x * scale;
		result.y += point.y * scale;
	}
	return result;
}

double Utilities::lineDistance(Point2d a, Point2d b) {
	Point2d d = a - b;
	return std::sqrt(std::pow(d.x, 2.0) + std::pow(d.y, 2.0));
}

double Utilities::lineDistance(Point3d a, Point3d b) {
	Point3d d = a - b;
	return std::sqrt(std::pow(d.x, 2.0) + std::pow(d.y, 2.0) + std::pow(d.z, 2.0));
}

TimeIntervalComparison Utilities::timeIntervalCompare(double startTimeA, double endTimeA, double startTimeB, double endTimeB) {
	TimeIntervalComparison result;
	if(startTimeA > endTimeA || startTimeB > endTimeB) {
		throw invalid_argument("Cannot perform time comparison if startTime comes after endTime.");
	}

	result.doesAEndBeforeB = false;
	if(endTimeA < startTimeB) {
		result.doesAEndBeforeB = true;
	}

	result.doesAOccurBeforeB = false;
	if(startTimeA < startTimeB) {
		result.doesAOccurBeforeB = true;
	}

	result.doesAOccurDuringB = false;
	if((startTimeA >= startTimeB || endTimeA >= startTimeB) && \
	  (startTimeA < endTimeB || endTimeA < endTimeB)) {
		result.doesAOccurDuringB = true;
	}

	result.doesAOccurAfterB = false;
	if(endTimeA > endTimeB) {
		result.doesAOccurAfterB = true;
	}

	result.doesAStartAfterB = false;
	if(startTimeA > endTimeB) {
		result.doesAStartAfterB = true;
	}
	
	return result;
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

Vec3d Utilities::rotationMatrixToEulerAngles(Mat &R, bool returnDegrees, bool degreesReflectAroundZero) {
	double sy = std::sqrt(R.at<double>(0,0) * R.at<double>(0,0) + R.at<double>(1,0) * R.at<double>(1,0));

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
		Vec3d angles = Vec3d(Utilities::radiansToDegrees(x), Utilities::radiansToDegrees(y), Utilities::radiansToDegrees(z));
		if(degreesReflectAroundZero) {
			for(int i = 0; i < 3; i++) {
				if(angles[i] > 180.0) {
					angles[i] = angles[i] - 360.0;
				}
			}
		}
		return angles;
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

double Utilities::degreesDifferenceBetweenTwoRotationMatrices(Mat a, Mat b) {
	Mat aTransposed = a.t();
	Mat ab = aTransposed * b;
	Scalar abTraceScalar = cv::trace(ab);
	double abTrace = abTraceScalar[0];
	while(abTrace > 1.0) {
		abTrace = abTrace - 2.0;
	}
	while(abTrace < -1.0) {
		abTrace = abTrace + 2.0;
	}
	return Utilities::radiansToDegrees(std::acos(abTrace) / 2.0);
}

Mat Utilities::generateFakeCameraMatrix(double focalLength, Point2d principalPoint) {
	return (Mat_<double>(3,3) <<
		focalLength, 0.0,         principalPoint.x,
		0.0,         focalLength, principalPoint.y,
		0.0,         0.0,         1.0);
}

bool Utilities::rayPlaneIntersection(Point3d &intersection, Point3d rayOrigin, Vec3d rayVector, Point3d planePoint, Vec3d planeNormal) {
	Vec3d rayVectorNormalized = cv::normalize(rayVector);

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
	line(frame, a, b, color, thickness, LINE_AA);
	a.x = markerPoint.x - lineLength;
	a.y = markerPoint.y;
	b.x = markerPoint.x + lineLength;
	b.y = markerPoint.y;
	line(frame, a, b, color, thickness, LINE_AA);
}

void Utilities::drawText(cv::Mat frame, const cv::String &text, cv::Point origin, cv::Scalar color, double size, cv::Point2d shadowOffset, double shadowColorMultiply) {
	// FIXME - proportional drawing
	cv::Point shadowOrigin = origin + cv::Point((int)round(shadowOffset.x * size), (int)round(shadowOffset.y * size));
	cv::putText(frame, text, shadowOrigin, FONT_HERSHEY_DUPLEX, 0.75 * size, color * shadowColorMultiply, ceil(size), LINE_AA);
	cv::putText(frame, text, origin, FONT_HERSHEY_DUPLEX, 0.75 * size, color, ceil(size), LINE_AA);
}

cv::Size Utilities::getTextSize(const cv::String &text, int *baseline, double size) {
	return cv::getTextSize(text, FONT_HERSHEY_DUPLEX, 0.75 * size, ceil(size), baseline);
}

cv::Scalar Utilities::scalarColorFromJSONArray(string jsonArrayString) {
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

json Utilities::JSONArrayFromScalarColor(cv::Scalar color) {
	json result = json::array({
		(int)color[0],
		(int)color[1],
		(int)color[2]
	});
	return result;
}

cv::Point3d Utilities::Point3dFromJSONArray(json jsonArray) {
	if(!jsonArray.is_array() || jsonArray.size() != 3) {
		throw invalid_argument("jsonArray must be an array with three numbers");
	}
	for(int i = 0; i < 3; i++) {
		if(!jsonArray[i].is_number()) {
			throw invalid_argument("Point3d must be an array of three numbers, but jsonArray contains an element which is not a valid number");
		}
	}
	return Point3d(jsonArray[0], jsonArray[1], jsonArray[2]);
}

bool Utilities::stringEndMatches(string haystack, string needle) {
	int start = (int)haystack.length() - (int)needle.length();
	if(start < 0) {
		return false;
	}
	if(haystack.substr(start, string::npos) == needle) {
		return true;
	}
	return false;
}

bool Utilities::fileExists(string filePath) {
	struct stat buf;
	logger->debug2("Checking if \"%s\" exists.", filePath.c_str());
	return (stat(filePath.c_str(), &buf) == 0);
}

string Utilities::fileSearchInCommonLocations(string filePath) {
	if(sdlDataPath == NULL) {
		sdlDataPath = SDL_GetBasePath();
		logger->debug3("SDL Reports Data Path: %s", sdlDataPath);
	}

	vector<string> searchBases;
	if(sdlDataPath != NULL) {
		string sdlDataPathStr = (string)sdlDataPath;
		vector<string> baseTrims = {
			"usr" YERFACE_PATH_SEP "local" YERFACE_PATH_SEP "bin" YERFACE_PATH_SEP,
			"usr" YERFACE_PATH_SEP "bin" YERFACE_PATH_SEP,
			"bin" YERFACE_PATH_SEP
		};
		for(string baseTrim : baseTrims) {
			if(stringEndMatches(sdlDataPathStr, baseTrim)) {
				searchBases.push_back(sdlDataPathStr.substr(0, sdlDataPathStr.length() - baseTrim.length()));
			}
		}
		searchBases.push_back(sdlDataPathStr);
	}
	searchBases.push_back("." YERFACE_PATH_SEP);
	#ifndef WIN32
	searchBases.push_back(YERFACE_PATH_SEP);
	#endif

	vector<string> searchSecondComponents = {
		"usr" YERFACE_PATH_SEP "local" YERFACE_PATH_SEP,
		"usr" YERFACE_PATH_SEP,
		""
	};

	vector<string> searchThirdComponents = {
		YERFACE_DATA_DIR YERFACE_PATH_SEP,
		""
	};
	for(string searchBase : searchBases) {
		logger->debug3("=== Searching BASE: %s", searchBase.c_str());
		for(string searchSecondComponent : searchSecondComponents) {
			logger->debug3("== Searching 2nd: %s", searchSecondComponent.c_str());
			for(string searchThirdComponent : searchThirdComponents) {
				logger->debug3("= Searching 3rd: %s", searchThirdComponent.c_str());
				string testPath = searchBase + searchSecondComponent + searchThirdComponent + filePath;
				#ifdef WIN32
				testPath = std::regex_replace(testPath, std::regex("/"), "\\");
				#endif
				logger->debug4("TEST PATH: %s", testPath.c_str());
				if(fileExists(testPath)) {
					return testPath;
				}
			}
		}
	}
	return "";
}

string Utilities::fileValidPathOrDie(string filePath, bool searchOnly) {
	if(!searchOnly) {
		if(fileExists(filePath)) {
			return filePath;
		}
	}
	string result = Utilities::fileSearchInCommonLocations(filePath);
	if(result == "") {
		logger->err("Could not find \"%s\"!", filePath.c_str());
		throw runtime_error("File or directory does not exist.");
	}
	return result;
}

string Utilities::stringTrim(std::string str) {
	str = stringTrimLeft(str);
	str = stringTrimRight(str);
	return str;
}

string Utilities::stringTrimLeft(std::string str) {
	//Trim whitespace from the beginning of the string.
	auto first = str.begin();
	auto last = std::find_if(str.begin(), str.end(), [](int ch) {
        return !std::isspace(ch);
    });
    str.erase(first, last);
	return str;
}

string Utilities::stringTrimRight(std::string str) {
	//Trim whitespace from the end of the string.
	auto first = std::find_if(str.rbegin(), str.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base();
	auto last = str.end();
    str.erase(first, last);
	return str;
}

Logger *Utilities::logger = new Logger("Utilities");
char *Utilities::sdlDataPath = NULL;

}; //namespace YerFace
