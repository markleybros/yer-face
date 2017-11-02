#include "SeparateMarkers.hpp"
#include "Utilities.hpp"
#include "opencv2/highgui.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

SeparateMarkers::SeparateMarkers(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, float myFaceSizePercentage) {
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	faceTracker = myFaceTracker;
	if(faceTracker == NULL) {
		throw invalid_argument("faceTracker cannot be NULL");
	}
	faceSizePercentage = myFaceSizePercentage;
	if(faceSizePercentage <= 0.0 || faceSizePercentage > 2.0) {
		throw invalid_argument("faceSizePercentage is out of range.");
	}

	HSVThreshold = Scalar(255, 255, 255);
	HSVThresholdTolerance = Scalar(0, 0, 0);
}

void SeparateMarkers::setHSVThreshold(Scalar myHSVThreshold, Scalar myHSVThresholdTolerance) {
	HSVThreshold = Scalar(myHSVThreshold);
	HSVThresholdTolerance = Scalar(myHSVThresholdTolerance);
}

void SeparateMarkers::processCurrentFrame(void) {
	// static bool pickedColorInfo = false;
	tuple<Rect2d, bool> faceRectTuple = faceTracker->getFaceRect();
	Rect2d faceRect = get<0>(faceRectTuple);
	bool faceRectSet = get<1>(faceRectTuple);
	if(!faceRectSet) {
		return;
	}
	Rect2d searchBox = Rect(Utilities::insetBox(faceRect, faceSizePercentage));
	try {
		Mat frame = frameDerivatives->getCurrentFrame();
		Size s = frame.size();
		Rect2d imageRect = Rect(0, 0, s.width, s.height);
		searchFrameBGR = frame(searchBox & imageRect);
	} catch(exception &e) {
		fprintf(stderr, "SeparateMarkers: WARNING: Failed search box cropping. Got exception: %s", e.what());
		return;
	}
	cvtColor(searchFrameBGR, searchFrameHSV, COLOR_BGR2HSV);
	// if(!pickedColorInfo) {
	// 	pickedColorInfo = true;
	// 	this->doPickColor();
	// }
	Mat searchFrameThreshold;
	Scalar minColor = HSVThreshold - HSVThresholdTolerance;
	Scalar maxColor = HSVThreshold + HSVThresholdTolerance;
    inRange(searchFrameHSV, minColor, maxColor, searchFrameThreshold);

	Mat structuringElement = getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2, 2));
	morphologyEx(searchFrameThreshold, searchFrameThreshold, cv::MORPH_OPEN, structuringElement);
	morphologyEx(searchFrameThreshold, searchFrameThreshold, cv::MORPH_CLOSE, structuringElement);

	imshow("somewin", searchFrameThreshold);
	waitKey(1);
}

void SeparateMarkers::doPickColor(void) {
	Rect2d rect = selectROI(searchFrameBGR);
	fprintf(stderr, "SeparateMarkers::doPickColor: Got a ROI Rectangle of: <%.02f, %.02f, %.02f, %.02f>\n", rect.x, rect.y, rect.width, rect.height);
	double hue = 0.0, minHue = -1, maxHue = -1;
	double saturation = 0.0, minSaturation = -1, maxSaturation = -1;
	double value = 0.0, minValue = -1, maxValue = -1;
	unsigned long samples = 0;
	for(int x = rect.x; x < rect.x + rect.width; x++) {
		for(int y = rect.y; y < rect.y + rect.height; y++) {
			samples++;
			Vec3b intensity = searchFrameHSV.at<Vec3b>(y, x);
			fprintf(stderr, "SeparateMarkers::doPickColor: <%d, %d> HSV: <%d, %d, %d>\n", x, y, intensity[0], intensity[1], intensity[2]);
			hue += (double)intensity[0];
			if(minHue < 0.0 || intensity[0] < minHue) {
				minHue = intensity[0];
			}
			if(maxHue < 0.0 || intensity[0] > maxHue) {
				maxHue = intensity[0];
			}
			saturation += (double)intensity[1];
			if(minSaturation < 0.0 || intensity[1] < minSaturation) {
				minSaturation = intensity[1];
			}
			if(maxSaturation < 0.0 || intensity[1] > maxSaturation) {
				maxSaturation = intensity[1];
			}
			value += (double)intensity[2];
			if(minValue < 0.0 || intensity[2] < minValue) {
				minValue = intensity[2];
			}
			if(maxValue < 0.0 || intensity[2] > maxValue) {
				maxValue = intensity[2];
			}
		}
	}
	hue = hue / (double)samples;
	saturation = saturation / (double)samples;
	value = value / (double)samples;
	fprintf(stderr, "SeparateMarkers::doPickColor: Average HSV color within selected rectangle: <%.02f, %.02f, %.02f>\n", hue, saturation, value);
	fprintf(stderr, "SeparateMarkers::doPickColor: Minimum HSV color within selected rectangle: <%.02f, %.02f, %.02f>\n", minHue, minSaturation, minValue);
	fprintf(stderr, "SeparateMarkers::doPickColor: Maximum HSV color within selected rectangle: <%.02f, %.02f, %.02f>\n", maxHue, maxSaturation, maxValue);
}

}; //namespace YerFace
