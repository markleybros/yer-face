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
	//static bool pickedColorInfo = false;
	tuple<Rect2d, bool> faceRectTuple = faceTracker->getFaceRect();
	Rect2d faceRect = get<0>(faceRectTuple);
	bool faceRectSet = get<1>(faceRectTuple);
	if(!faceRectSet) {
		return;
	}
	Rect2d searchBox = Rect(Utilities::insetBox(faceRect, faceSizePercentage));
	try {
		searchFrameBGR = frameDerivatives->getCurrentFrame()(searchBox);
	} catch(exception &e) {
		fprintf(stderr, "SeparateMarkers: WARNING: Failed search box cropping. Got exception: %s", e.what());
		return;
	}
	cvtColor(searchFrameBGR, searchFrameHSV, COLOR_BGR2HSV);
	/*if(!pickedColorInfo) {
		pickedColorInfo = true;
		this->doPickColor();
	}*/
	Mat searchFrameThreshold;
	Scalar minColor = HSVThreshold - HSVThresholdTolerance;
	Scalar maxColor = HSVThreshold + HSVThresholdTolerance;
    inRange(searchFrameHSV, minColor, maxColor, searchFrameThreshold);

	imshow("somewin", searchFrameThreshold);
	waitKey(1);
}

void SeparateMarkers::doPickColor(void) {
	Rect2d rect = selectROI(searchFrameBGR);
	fprintf(stderr, "SeparateMarkers::doPickColor: Got a ROI Rectangle of: <%.02f, %.02f, %.02f, %.02f>\n", rect.x, rect.y, rect.width, rect.height);
	double hue = 0.0;
	double saturation = 0.0;
	double value = 0.0;
	unsigned long samples = 0;
	for(int x = rect.x; x < rect.x + rect.width; x++) {
		for(int y = rect.y; y < rect.y + rect.height; y++) {
			samples++;
			Vec3b intensity = searchFrameHSV.at<Vec3b>(y, x);
			fprintf(stderr, "SeparateMarkers::doPickColor: <%d, %d> HSV: <%d, %d, %d>\n", x, y, intensity[0], intensity[1], intensity[2]);
			hue += (double)intensity[0];
			saturation += (double)intensity[1];
			value += (double)intensity[2];
		}
	}
	hue = hue / (double)samples;
	saturation = saturation / (double)samples;
	value = value / (double)samples;
	fprintf(stderr, "SeparateMarkers::doPickColor: Average HSV color within selected rectangle: <%.02f, %.02f, %.02f>\n", hue, saturation, value);
}

}; //namespace YerFace
