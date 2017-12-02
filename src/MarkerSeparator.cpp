
#include "MarkerSeparator.hpp"
#include "Utilities.hpp"

#include "opencv2/highgui.hpp"

#include <cstdlib>
#include <cstdio>

using namespace std;
using namespace cv;

namespace YerFace {

MarkerSeparator::MarkerSeparator(SDLDriver *mySDLDriver, FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, Scalar myHSVRangeMin, Scalar myHSVRangeMax, float myFaceSizePercentage, float myMinTargetMarkerAreaPercentage, float myMaxTargetMarkerAreaPercentage, float myMarkerBoxInflatePixels) {
	sdlDriver = mySDLDriver;
	if(sdlDriver == NULL) {
		throw invalid_argument("sdlDriver cannot be NULL");
	}
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
	markerBoxInflatePixels = myMarkerBoxInflatePixels;
	if(markerBoxInflatePixels < 0.0) {
		throw invalid_argument("markerBoxInflatePixels cannot be less than zero");
	}
	logger = new Logger("MarkerSeparator");
	metrics = new Metrics("MarkerSeparator");
	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	setHSVRange(myHSVRangeMin, myHSVRangeMax);
	sdlDriver->onColorPickerEvent([this] (void) -> void {
		this->logger->info("Got a Color Picker event. Popping up a color picker...");
		this->doPickColor();
	});
	logger->debug("MarkerSeparator object constructed and ready to go!");
}

MarkerSeparator::~MarkerSeparator() {
	logger->debug("MarkerSeparator object destructing...");
	SDL_DestroyMutex(myMutex);
	delete metrics;
	delete logger;
}

void MarkerSeparator::setHSVRange(Scalar myHSVRangeMin, Scalar myHSVRangeMax) {
	YerFace_MutexLock(myMutex);
	HSVRangeMin = myHSVRangeMin;
	HSVRangeMax = myHSVRangeMax;
	YerFace_MutexUnlock(myMutex);
}

void MarkerSeparator::processCurrentFrame(bool debug) {
	YerFace_MutexLock(myMutex);

	metrics->startClock();
	
	working.markerList.clear();
	FacialRect facialBoundingBox = faceTracker->getFacialBoundingBox();
	if(!facialBoundingBox.set) {
		return;
	}
	Rect2d markerBoundaryRect;
	Rect2d searchBox = Rect(Utilities::insetBox(facialBoundingBox.rect, faceSizePercentage));
	try {
		Mat frame = frameDerivatives->getWorkingFrame();
		Size frameSize = frame.size();
		Rect2d imageRect = Rect(0, 0, frameSize.width, frameSize.height);
		markerBoundaryRect = Rect(searchBox & imageRect);
		searchFrameBGR = frame(markerBoundaryRect);
	} catch(exception &e) {
		logger->warn("Failed search box cropping. Got exception: %s", e.what());
		return;
	}
	cvtColor(searchFrameBGR, searchFrameHSV, COLOR_BGR2HSV);
	Mat searchFrameThreshold;
    inRange(searchFrameHSV, HSVRangeMin, HSVRangeMax, searchFrameThreshold);

	Mat structuringElement = getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2, 2));
	morphologyEx(searchFrameThreshold, searchFrameThreshold, cv::MORPH_OPEN, structuringElement);
	morphologyEx(searchFrameThreshold, searchFrameThreshold, cv::MORPH_CLOSE, structuringElement);

	Mat debugFrame;
	if(debug) {
		cvtColor(searchFrameThreshold, debugFrame, COLOR_GRAY2BGR);
	}

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
			markerCandidate.center = markerCandidate.center;
			MarkerSeparated markerSeparated;
			markerSeparated.active = true;
			markerSeparated.marker = markerCandidate;
			working.markerList.push_back(markerSeparated);
		}
	}

	count = working.markerList.size();
	for(i = 0; i < count; i++) {
		if(!working.markerList[i].active) {
			continue;
		}
		Point2f vertices[8];
		working.markerList[i].marker.points(vertices);
		Rect2d primaryRect;
		bool didRemoveDuplicate;
		do {
			didRemoveDuplicate = false;
			primaryRect = Rect2d(working.markerList[i].marker.boundingRect2f());
			primaryRect.x -= markerBoxInflatePixels;
			primaryRect.y -= markerBoxInflatePixels;
			primaryRect.width += markerBoxInflatePixels * 2;
			primaryRect.height += markerBoxInflatePixels * 2;
			for(size_t j = 0; j < count; j++) {
				if(i == j || !working.markerList[j].active) {
					continue;
				}
				Rect2d secondaryRect = Rect2d(working.markerList[j].marker.boundingRect2f());
				secondaryRect.x -= markerBoxInflatePixels;
				secondaryRect.y -= markerBoxInflatePixels;
				secondaryRect.width += markerBoxInflatePixels * 2;
				secondaryRect.height += markerBoxInflatePixels * 2;
				if((primaryRect & secondaryRect).area() > 0) {
					working.markerList[i].marker.points(&vertices[0]);
					working.markerList[j].marker.points(&vertices[4]);
					vector<Point2f> verticesVector(std::begin(vertices), std::end(vertices));
					working.markerList[i].marker = minAreaRect(verticesVector);
					working.markerList[j].active = false;
					didRemoveDuplicate = true;
				}
			}
		} while(didRemoveDuplicate);
		if(debug) {
			rectangle(debugFrame, primaryRect, Scalar(0, 0, 255));
		}
	}

	if(debug) {
		imshow("Trackers Separated", debugFrame);
		waitKey(1);
	}

	count = working.markerList.size();
	for(i = 0; i < count; i++) {
		if(!working.markerList[i].active) {
			continue;
		}
		working.markerList[i].marker.center += Point2f(markerBoundaryRect.tl());
	}

	metrics->endClock();
	YerFace_MutexUnlock(myMutex);
}

void MarkerSeparator::advanceWorkingToCompleted(void) {
	YerFace_MutexLock(myMutex);
	complete = working;
	working.markerList.clear();
	YerFace_MutexUnlock(myMutex);
}

void MarkerSeparator::renderPreviewHUD(void) {
	Mat frame = frameDerivatives->getPreviewFrame();
	int density = sdlDriver->getPreviewDebugDensity();
	if(density > 2) {
		for(auto marker : complete.markerList) {
			if(marker.active) {
				Utilities::drawRotatedRectOutline(frame, marker.marker, Scalar(255,255,0), 3);
			}
		}
	}
}

vector<MarkerSeparated> *MarkerSeparator::getMarkerList(void) {
	return &working.markerList;
}

void MarkerSeparator::doPickColor(void) {
	Rect2d rect = selectROI(searchFrameBGR);
	logger->verbose("doPickColor: Got a ROI Rectangle of: <%.02f, %.02f, %.02f, %.02f>", rect.x, rect.y, rect.width, rect.height);
	double hue = 0.0, minHue = -1, maxHue = -1;
	double saturation = 0.0, minSaturation = -1, maxSaturation = -1;
	double value = 0.0, minValue = -1, maxValue = -1;
	unsigned long samples = 0;
	for(int x = rect.x; x < rect.x + rect.width; x++) {
		for(int y = rect.y; y < rect.y + rect.height; y++) {
			samples++;
			Vec3b intensity = searchFrameHSV.at<Vec3b>(y, x);
			logger->verbose("doPickColor: <%d, %d> HSV: <%d, %d, %d>", x, y, intensity[0], intensity[1], intensity[2]);
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
	logger->info("doPickColor: Average HSV color within selected rectangle: <%.02f, %.02f, %.02f>", hue, saturation, value);
	logger->info("doPickColor: Minimum HSV color within selected rectangle: <%.02f, %.02f, %.02f>", minHue, minSaturation, minValue);
	logger->info("doPickColor: Maximum HSV color within selected rectangle: <%.02f, %.02f, %.02f>", maxHue, maxSaturation, maxValue);
}

}; //namespace YerFace
