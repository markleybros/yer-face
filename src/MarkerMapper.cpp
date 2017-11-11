
#include "MarkerMapper.hpp"
#include "Utilities.hpp"
#include "opencv2/highgui.hpp"

#include <cstdlib>

using namespace std;
using namespace cv;

namespace YerFace {

MarkerMapper::MarkerMapper(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, EyeTracker *myLeftEyeTracker, EyeTracker *myRightEyeTracker, float myEyelidBottomPointWeight, float myEyeLineLengthPercentage) {
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	faceTracker = myFaceTracker;
	if(faceTracker == NULL) {
		throw invalid_argument("faceTracker cannot be NULL");
	}
	leftEyeTracker = myLeftEyeTracker;
	if(leftEyeTracker == NULL) {
		throw invalid_argument("leftEyeTracker cannot be NULL");
	}
	rightEyeTracker = myRightEyeTracker;
	if(rightEyeTracker == NULL) {
		throw invalid_argument("rightEyeTracker cannot be NULL");
	}
	eyelidBottomPointWeight = myEyelidBottomPointWeight;
	if(eyelidBottomPointWeight < 0.0 || eyelidBottomPointWeight > 1.0) {
		throw invalid_argument("eyelidBottomPointWeight cannot be less than 0.0 or greater than 1.0");
	}
	eyeLineLengthPercentage = myEyeLineLengthPercentage;
	if(eyeLineLengthPercentage <= 0.0) {
		throw invalid_argument("eyeLineLengthPercentage cannot be less than or equal to 0.0");
	}

	eyeLineSet = false;
	eyebrowLineSet = false;
	midLineSet = false;
	centerLineSet = false;

	markerSeparator = new MarkerSeparator(frameDerivatives, faceTracker);

	markerEyelidLeftTop = new MarkerTracker(EyelidLeftTop, this, frameDerivatives, markerSeparator, leftEyeTracker);
	markerEyelidRightTop = new MarkerTracker(EyelidRightTop, this, frameDerivatives, markerSeparator, rightEyeTracker);
	markerEyelidLeftBottom = new MarkerTracker(EyelidLeftBottom, this, frameDerivatives, markerSeparator, leftEyeTracker);
	markerEyelidRightBottom = new MarkerTracker(EyelidRightBottom, this, frameDerivatives, markerSeparator, rightEyeTracker);

	markerEyebrowLeftInner = new MarkerTracker(EyebrowLeftInner, this, frameDerivatives, markerSeparator);
	markerEyebrowRightInner = new MarkerTracker(EyebrowRightInner, this, frameDerivatives, markerSeparator);
	markerEyebrowLeftMiddle = new MarkerTracker(EyebrowLeftMiddle, this, frameDerivatives, markerSeparator);
	markerEyebrowRightMiddle = new MarkerTracker(EyebrowRightMiddle, this, frameDerivatives, markerSeparator);
	markerEyebrowLeftOuter = new MarkerTracker(EyebrowLeftOuter, this, frameDerivatives, markerSeparator);
	markerEyebrowRightOuter = new MarkerTracker(EyebrowRightOuter, this, frameDerivatives, markerSeparator);

	markerCheekLeft = new MarkerTracker(CheekLeft, this, frameDerivatives, markerSeparator);
	markerCheekRight = new MarkerTracker(CheekRight, this, frameDerivatives, markerSeparator);
	
	markerJaw = new MarkerTracker(Jaw, this, frameDerivatives, markerSeparator);
	
	fprintf(stderr, "MarkerMapper object constructed and ready to go!\n");
}

MarkerMapper::~MarkerMapper() {
	fprintf(stderr, "MarkerMapper object destructing...\n");
	//Make a COPY of the vector, because otherwise it will change size out from under us while we are iterating.
	vector<MarkerTracker *> markerTrackersSnapshot = vector<MarkerTracker *>(*MarkerTracker::getMarkerTrackers());
	size_t markerTrackersCount = markerTrackersSnapshot.size();
	for(size_t i = 0; i < markerTrackersCount; i++) {
		if(markerTrackersSnapshot[i] != NULL) {
			delete markerTrackersSnapshot[i];
		}
	}
	delete markerSeparator;
}

void MarkerMapper::processCurrentFrame(void) {
	markerSeparator->processCurrentFrame();

	markerEyelidLeftTop->processCurrentFrame();
	markerEyelidRightTop->processCurrentFrame();
	markerEyelidLeftBottom->processCurrentFrame();
	markerEyelidRightBottom->processCurrentFrame();

	calculateEyeLine();

	markerEyebrowLeftInner->processCurrentFrame();
	markerEyebrowRightInner->processCurrentFrame();
	markerEyebrowLeftMiddle->processCurrentFrame();
	markerEyebrowRightMiddle->processCurrentFrame();
	markerEyebrowLeftOuter->processCurrentFrame();
	markerEyebrowRightOuter->processCurrentFrame();

	calculateEyebrowLine();

	markerCheekLeft->processCurrentFrame();
	markerCheekRight->processCurrentFrame();

	calculateMidLine();
	calculateCenterLine(true);

	markerJaw->processCurrentFrame();

	calculateCenterLine(false);
}

void MarkerMapper::renderPreviewHUD(bool verbose) {
	Mat frame = frameDerivatives->getPreviewFrame();
	if(verbose) {
		markerSeparator->renderPreviewHUD(true);
	}
	vector<MarkerTracker *> *markerTrackers = MarkerTracker::getMarkerTrackers();
	size_t markerTrackersCount = (*markerTrackers).size();
	for(size_t i = 0; i < markerTrackersCount; i++) {
		(*markerTrackers)[i]->renderPreviewHUD(verbose);
	}
	if(eyeLineSet) {
		line(frame, eyeLineLeft, eyeLineRight, Scalar(0, 255, 0), 2);
		Utilities::drawX(frame, eyeLineCenter, Scalar(0, 255, 255), 10, 2);
	}
	if(eyebrowLineSet) {
		line(frame, eyebrowLineLeft, eyebrowLineRight, Scalar(0, 255, 0), 2);
		Utilities::drawX(frame, eyebrowLineCenter, Scalar(0, 255, 255), 10, 2);
	}
	if(midLineSet) {
		line(frame, midLineLeft, midLineRight, Scalar(0, 255, 0), 2);
		Utilities::drawX(frame, midLineCenter, Scalar(0, 255, 255), 10, 2);
	}
	if(centerLineSet) {
		line(frame, centerLineTop, centerLineBottom, Scalar(0, 255, 255), 2);
	}
}

tuple<Point2d, Point2d, Point2d, bool> MarkerMapper::getEyeLine(void) {
	return std::make_tuple(eyeLineLeft, eyeLineRight, eyeLineCenter, eyeLineSet);
}

tuple<Point2d, Point2d, Point2d, bool> MarkerMapper::getEyebrowLine(void) {
	return std::make_tuple(eyebrowLineLeft, eyebrowLineRight, eyebrowLineCenter, eyebrowLineSet);
}

tuple<Point2d, Point2d, Point2d, bool> MarkerMapper::getMidLine(void) {
	return std::make_tuple(midLineLeft, midLineRight, midLineCenter, midLineSet);
}

tuple<Point2d, Point2d, double, double, bool, bool> MarkerMapper::getCenterLine(void) {
	return std::make_tuple(centerLineTop, centerLineBottom, centerLineSlope, centerLineIntercept, centerLineIsIntermediate, centerLineSet);
}

void MarkerMapper::calculateEyeLine(void) {
	eyeLineSet = false;
	if(!calculateEyeCenter(markerEyelidLeftTop, markerEyelidLeftBottom, &eyeLineLeft)) {
		return;
	}
	if(!calculateEyeCenter(markerEyelidRightTop, markerEyelidRightBottom, &eyeLineRight)) {
		return;
	}

	double originalEyeLineLength = Utilities::lineDistance(eyeLineLeft, eyeLineRight);
	double desiredEyeLineLength = originalEyeLineLength * eyeLineLengthPercentage;
	eyeLineCenter = (eyeLineLeft + eyeLineRight);
	eyeLineCenter.x = eyeLineCenter.x / 2.0;
	eyeLineCenter.y = eyeLineCenter.y / 2.0;

	eyeLineLeft = Utilities::adjustLineDistance(eyeLineCenter, eyeLineLeft, (desiredEyeLineLength / 2.0));
	eyeLineRight = Utilities::adjustLineDistance(eyeLineCenter, eyeLineRight, (desiredEyeLineLength / 2.0));
	eyeLineSet = true;
}

bool MarkerMapper::calculateEyeCenter(MarkerTracker *top, MarkerTracker *bottom, Point2d *center) {
	Point2d topPoint;
	bool topPointSet;
	std::tie(topPoint, topPointSet) = top->getMarkerPoint();
	if(!topPointSet) {
		return false;
	}
	Point2d bottomPoint;
	bool bottomPointSet;
	std::tie(bottomPoint, bottomPointSet) = bottom->getMarkerPoint();
	if(!bottomPointSet) {
		return false;
	}
	double bottomPointWeight = eyelidBottomPointWeight;
	bottomPoint.x = bottomPoint.x * bottomPointWeight;
	bottomPoint.y = bottomPoint.y * bottomPointWeight;
	topPoint.x = topPoint.x * (1.0 - bottomPointWeight);
	topPoint.y = topPoint.y * (1.0 - bottomPointWeight);
	*center = bottomPoint + topPoint;
	return true;
}

void MarkerMapper::calculateEyebrowLine(void) {
	eyebrowLineSet = false;
	vector<MarkerTracker *> eyebrows = {markerEyebrowLeftInner, markerEyebrowLeftMiddle, markerEyebrowLeftOuter, markerEyebrowRightInner, markerEyebrowRightMiddle, markerEyebrowRightOuter};
	vector<Point2d> points;
	double minX = -1.0, maxX = -1.0, innerLeftX = -1.0, innerRightX = -1.0;
	size_t count = eyebrows.size();
	for(size_t i = 0; i < count; i++) {
		bool pointSet;
		Point2d point;
		std::tie(point, pointSet) = eyebrows[i]->getMarkerPoint();
		if(pointSet) {
			if(minX < 0.0 || point.x < minX) {
				minX = point.x;
			}
			if(maxX < 0.0 || point.x > maxX) {
				maxX = point.x;
			}
			if(eyebrows[i]->getMarkerType().type == EyebrowLeftInner) {
				innerLeftX = point.x;
			}
			if(eyebrows[i]->getMarkerType().type == EyebrowRightInner) {
				innerRightX = point.x;
			}
			points.push_back(point);
		}
	}
	if(innerLeftX < 0 || innerRightX < 0) {
		return;
	}
	if(points.size() > 3) {
		double slope, intercept;
		Utilities::lineBestFit(points, &slope, &intercept);
		eyebrowLineLeft.x = maxX;
		eyebrowLineLeft.y = (slope * maxX) + intercept;
		eyebrowLineRight.x = minX;
		eyebrowLineRight.y = (slope * minX) + intercept;
		eyebrowLineCenter.x = ((innerLeftX + innerRightX) / 2.0);
		eyebrowLineCenter.y = (slope * eyebrowLineCenter.x) + intercept;
	}
	eyebrowLineSet = true;
}

void MarkerMapper::calculateMidLine(void) {
	midLineSet = false;
	bool midLineLeftSet;
	std::tie(midLineLeft, midLineLeftSet) = markerCheekLeft->getMarkerPoint();
	if(!midLineLeftSet) {
		return;
	}
	bool midLineRightSet;
	std::tie(midLineRight, midLineRightSet) = markerCheekRight->getMarkerPoint();
	if(!midLineRightSet) {
		return;
	}
	midLineCenter = (midLineLeft + midLineRight);
	midLineCenter.x = midLineCenter.x / 2.0;
	midLineCenter.y = midLineCenter.y / 2.0;
	midLineSet = true;
}

void MarkerMapper::calculateCenterLine(bool intermediate) {
	centerLineSet = false;
	bool jawPointSet;
	Point2d jawPoint;
	if(!eyebrowLineSet || !eyeLineSet || !midLineSet) {
		return;
	}
	centerLineIsIntermediate = intermediate;

	vector<Point2d> points = {eyebrowLineCenter, eyeLineCenter, midLineCenter};
	if(!intermediate) {
		std::tie(jawPoint, jawPointSet) = markerJaw->getMarkerPoint();
		if(jawPointSet) {
			points.push_back(jawPoint);
		} else {
			return;
		}
	}

	Utilities::lineBestFit(points, &centerLineSlope, &centerLineIntercept);

	centerLineTop.y = eyebrowLineCenter.y;
	centerLineTop.x = (centerLineTop.y - centerLineIntercept) / centerLineSlope;
	if(!intermediate) {
		centerLineBottom.y = jawPoint.y;
	} else {
		centerLineBottom.y = midLineCenter.y;
	}
	centerLineBottom.x = (centerLineBottom.y - centerLineIntercept) / centerLineSlope;
	centerLineSet = true;
}

}; //namespace YerFace
