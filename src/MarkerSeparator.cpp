
#include "MarkerSeparator.hpp"
#include "Utilities.hpp"
#include "opencv2/highgui.hpp"

#include <cstdlib>
#include <cstdio>

using namespace std;
using namespace cv;

namespace YerFace {

MarkerSeparator::MarkerSeparator(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, Scalar myHSVRangeMin, Scalar myHSVRangeMax, float myFaceSizePercentage, float myMinTargetMarkerAreaPercentage, float myMaxTargetMarkerAreaPercentage) {
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	faceTracker = myFaceTracker;
	if(faceTracker == NULL) {
		throw invalid_argument("faceTracker cannot be NULL");
	}
	minTargetMarkerAreaPercentage = myMinTargetMarkerAreaPercentage;
	if(minTargetMarkerAreaPercentage <= 0.0 || minTargetMarkerAreaPercentage > 1.0) {
		throw invalid_argument("minTargetMarkerAreaPercentage is out of range.");
	}
	maxTargetMarkerAreaPercentage = myMaxTargetMarkerAreaPercentage;
	if(maxTargetMarkerAreaPercentage <= 0.0 || maxTargetMarkerAreaPercentage > 1.0) {
		throw invalid_argument("maxTargetMarkerAreaPercentage is out of range.");
	}
	faceSizePercentage = myFaceSizePercentage;
	if(faceSizePercentage <= 0.0 || faceSizePercentage > 2.0) {
		throw invalid_argument("faceSizePercentage is out of range.");
	}
	setHSVRange(myHSVRangeMin, myHSVRangeMax);
	fprintf(stderr, "MarkerSeparator object constructed and ready to go!\n");
}

MarkerSeparator::~MarkerSeparator() {
	fprintf(stderr, "MarkerSeparator object destructing...\n");
}

void MarkerSeparator::setHSVRange(Scalar myHSVRangeMin, Scalar myHSVRangeMax) {
	HSVRangeMin = Scalar(myHSVRangeMin);
	HSVRangeMax = Scalar(myHSVRangeMax);
}

void MarkerSeparator::processCurrentFrame(void) {
	markerList.clear();
	tuple<Rect2d, bool> faceRectTuple = faceTracker->getFaceRect();
	Rect2d faceRect = get<0>(faceRectTuple);
	bool faceRectSet = get<1>(faceRectTuple);
	if(!faceRectSet) {
		return;
	}
	Rect2d searchBox = Rect(Utilities::insetBox(faceRect, faceSizePercentage));
	try {
		Mat frame = frameDerivatives->getCurrentFrame();
		Size frameSize = frame.size();
		Rect2d imageRect = Rect(0, 0, frameSize.width, frameSize.height);
		markerBoundaryRect = Rect(searchBox & imageRect);
		searchFrameBGR = frame(markerBoundaryRect);
	} catch(exception &e) {
		fprintf(stderr, "MarkerSeparator: WARNING: Failed search box cropping. Got exception: %s", e.what());
		return;
	}
	cvtColor(searchFrameBGR, searchFrameHSV, COLOR_BGR2HSV);
	Mat searchFrameThreshold;
    inRange(searchFrameHSV, HSVRangeMin, HSVRangeMax, searchFrameThreshold);

	Mat structuringElement = getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2, 2));
	morphologyEx(searchFrameThreshold, searchFrameThreshold, cv::MORPH_OPEN, structuringElement);
	morphologyEx(searchFrameThreshold, searchFrameThreshold, cv::MORPH_CLOSE, structuringElement);

	vector<vector<Point>> contours;
	vector<Vec4i> heirarchy;
	findContours(searchFrameThreshold, contours, heirarchy, CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE);

	float minTargetMarkerArea = markerBoundaryRect.area() * minTargetMarkerAreaPercentage;
	float maxTargetMarkerArea = markerBoundaryRect.area() * maxTargetMarkerAreaPercentage;
	size_t count = contours.size();
	size_t i;
	for(i = 0; i < count; i++) {
		RotatedRect markerCandidate = minAreaRect(contours[i]);
		int area = markerCandidate.size.area();
		if(area >= minTargetMarkerArea && area <= maxTargetMarkerArea) {
			markerCandidate.center = markerCandidate.center + Point2f(markerBoundaryRect.tl());
			MarkerSeparated markerSeparated;
			markerSeparated.active = true;
			markerSeparated.marker = markerCandidate;
			markerList.push_back(markerSeparated);
		}
	}

	count = markerList.size();
	for(i = 0; i < count; i++) {
		if(!markerList[i].active) {
			continue;
		}
		// fprintf(stderr, "MarkerSeparator INFO: Deduplicating, considering base marker %lu\n", i);
		Point2f vertices[8];
		markerList[i].marker.points(vertices);
		bool didRemoveDuplicate;
		unsigned int iterations = 0;
		do {
			iterations++;
			didRemoveDuplicate = false;
			Rect2d primaryRect = Rect(markerList[i].marker.boundingRect2f());
			for(size_t j = 0; j < count; j++) {
				if(i == j || !markerList[j].active) {
					continue;
				}
				// fprintf(stderr, "MarkerSeparator INFO: Deduplicating, comparing secondary marker %lu with base marker %lu\n", j, i);
				Rect2d secondaryRect = Rect(markerList[j].marker.boundingRect2f());
				if((primaryRect & secondaryRect).area() > 0) {
					// fprintf(stderr, "MarkerSeparator INFO: Deduplicating, folding secondary marker %lu into base marker %lu\n", j, i);
					markerList[i].marker.points(&vertices[0]);
					markerList[j].marker.points(&vertices[4]);
					vector<Point2f> verticesVector(std::begin(vertices), std::end(vertices));
					markerList[i].marker = minAreaRect(verticesVector);
					markerList[j].active = false;
					didRemoveDuplicate = true;
				}
			}
			if(didRemoveDuplicate && iterations > 1) {
				fprintf(stderr, "MarkerSeparator WARNING: Deduplicating, altering base marker %lu in a loop; this was iteration %u and more iterations are still required. Looping over it again...\n", i, iterations);
			}
		} while(didRemoveDuplicate);
	}

	// imshow("Trackers Separated", searchFrameThreshold);
	// char c = (char)waitKey(1);
	// if(c == ' ') {
	// 	doPickColor();
	// 	fprintf(stderr, "MarkerSeparator User asked for a color picker...\n");
	// }
}

void MarkerSeparator::renderPreviewHUD(bool verbose) {
	Mat frame = frameDerivatives->getPreviewFrame();
	if(verbose) {
		size_t count = markerList.size();
		for(unsigned int i = 0; i < count; i++) {
			if(markerList[i].active) {
				Utilities::drawRotatedRectOutline(frame, markerList[i].marker, Scalar(255,255,0), 3);
			}
		}
	}
}

vector<MarkerSeparated> *MarkerSeparator::getMarkerList(void) {
	return &markerList;
}

void MarkerSeparator::doPickColor(void) {
	Rect2d rect = selectROI(searchFrameBGR);
	fprintf(stderr, "MarkerSeparator::doPickColor: Got a ROI Rectangle of: <%.02f, %.02f, %.02f, %.02f>\n", rect.x, rect.y, rect.width, rect.height);
	double hue = 0.0, minHue = -1, maxHue = -1;
	double saturation = 0.0, minSaturation = -1, maxSaturation = -1;
	double value = 0.0, minValue = -1, maxValue = -1;
	unsigned long samples = 0;
	for(int x = rect.x; x < rect.x + rect.width; x++) {
		for(int y = rect.y; y < rect.y + rect.height; y++) {
			samples++;
			Vec3b intensity = searchFrameHSV.at<Vec3b>(y, x);
			fprintf(stderr, "MarkerSeparator::doPickColor: <%d, %d> HSV: <%d, %d, %d>\n", x, y, intensity[0], intensity[1], intensity[2]);
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
	fprintf(stderr, "MarkerSeparator::doPickColor: Average HSV color within selected rectangle: <%.02f, %.02f, %.02f>\n", hue, saturation, value);
	fprintf(stderr, "MarkerSeparator::doPickColor: Minimum HSV color within selected rectangle: <%.02f, %.02f, %.02f>\n", minHue, minSaturation, minValue);
	fprintf(stderr, "MarkerSeparator::doPickColor: Maximum HSV color within selected rectangle: <%.02f, %.02f, %.02f>\n", maxHue, maxSaturation, maxValue);
}

}; //namespace YerFace
