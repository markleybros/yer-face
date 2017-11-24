
#include "FaceMapper.hpp"
#include "Utilities.hpp"
#include "opencv2/calib3d.hpp"
#include "opencv2/highgui.hpp"

#include <cstdlib>

using namespace std;
using namespace cv;

namespace YerFace {

FaceMapper::FaceMapper(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, float myEyelidBottomPointWeight, float myFaceAspectRatio) {
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	faceTracker = myFaceTracker;
	if(faceTracker == NULL) {
		throw invalid_argument("faceTracker cannot be NULL");
	}
	eyelidBottomPointWeight = myEyelidBottomPointWeight;
	if(eyelidBottomPointWeight < 0.0 || eyelidBottomPointWeight > 1.0) {
		throw invalid_argument("eyelidBottomPointWeight cannot be less than 0.0 or greater than 1.0");
	}
	faceAspectRatio = myFaceAspectRatio;
	if(faceAspectRatio <= 0.0) {
		throw invalid_argument("faceAspectRatio cannot be less than or equal to 0.0");
	}

	eyebrowLineSet = false;
	midLineSet = false;
	smileLineSet = false;
	centerLineSet = false;
	faceTransformationBaselinePointsSet = false;
	faceTransformationSet = false;

	markerSeparator = new MarkerSeparator(frameDerivatives, faceTracker);

	markerEyelidLeftTop = new MarkerTracker(EyelidLeftTop, this);
	markerEyelidRightTop = new MarkerTracker(EyelidRightTop, this);
	markerEyelidLeftBottom = new MarkerTracker(EyelidLeftBottom, this);
	markerEyelidRightBottom = new MarkerTracker(EyelidRightBottom, this);

	markerEyebrowLeftInner = new MarkerTracker(EyebrowLeftInner, this);
	markerEyebrowRightInner = new MarkerTracker(EyebrowRightInner, this);
	markerEyebrowLeftMiddle = new MarkerTracker(EyebrowLeftMiddle, this);
	markerEyebrowRightMiddle = new MarkerTracker(EyebrowRightMiddle, this);
	markerEyebrowLeftOuter = new MarkerTracker(EyebrowLeftOuter, this);
	markerEyebrowRightOuter = new MarkerTracker(EyebrowRightOuter, this);

	markerCheekLeft = new MarkerTracker(CheekLeft, this);
	markerCheekRight = new MarkerTracker(CheekRight, this);
	
	markerJaw = new MarkerTracker(Jaw, this);

	markerLipsLeftCorner = new MarkerTracker(LipsLeftCorner, this);
	markerLipsRightCorner = new MarkerTracker(LipsRightCorner, this);

	markerLipsLeftTop = new MarkerTracker(LipsLeftTop, this);
	markerLipsRightTop = new MarkerTracker(LipsRightTop, this);

	markerLipsLeftBottom = new MarkerTracker(LipsLeftBottom, this);
	markerLipsRightBottom = new MarkerTracker(LipsRightBottom, this);
	
	fprintf(stderr, "FaceMapper object constructed and ready to go!\n");
}

FaceMapper::~FaceMapper() {
	fprintf(stderr, "FaceMapper object destructing...\n");
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

void FaceMapper::processCurrentFrame(void) {
	facialFeatures = faceTracker->getFacialFeatures();
	calculateEyeRects();

	markerSeparator->processCurrentFrame();

	markerEyelidLeftTop->processCurrentFrame();
	markerEyelidRightTop->processCurrentFrame();
	markerEyelidLeftBottom->processCurrentFrame();
	markerEyelidRightBottom->processCurrentFrame();

	markerEyebrowLeftInner->processCurrentFrame();
	markerEyebrowRightInner->processCurrentFrame();
	markerEyebrowLeftMiddle->processCurrentFrame();
	markerEyebrowRightMiddle->processCurrentFrame();
	markerEyebrowLeftOuter->processCurrentFrame();
	markerEyebrowRightOuter->processCurrentFrame();

	// calculateEyebrowLine();

	markerCheekLeft->processCurrentFrame();
	markerCheekRight->processCurrentFrame();

	// calculateMidLine();
	// calculateCenterLine(true);

	markerJaw->processCurrentFrame();

	// markerLipsLeftCorner->processCurrentFrame();
	// markerLipsRightCorner->processCurrentFrame();

	// markerLipsLeftTop->processCurrentFrame();
	// markerLipsRightTop->processCurrentFrame();

	// calculateSmileLine();
	// calculateCenterLine(false);

	// markerLipsLeftBottom->processCurrentFrame();
	// markerLipsRightBottom->processCurrentFrame();
}

void FaceMapper::renderPreviewHUD(bool verbose) {
	Mat frame = frameDerivatives->getPreviewFrame();
	if(verbose) {
		markerSeparator->renderPreviewHUD(true);

		if(leftEyeRect.set) {
			rectangle(frame, leftEyeRect.rect, Scalar(0, 0, 255));
		}
		if(rightEyeRect.set) {
			rectangle(frame, rightEyeRect.rect, Scalar(0, 0, 255));
		}
	}
	vector<MarkerTracker *> *markerTrackers = MarkerTracker::getMarkerTrackers();
	size_t markerTrackersCount = (*markerTrackers).size();
	for(size_t i = 0; i < markerTrackersCount; i++) {
		(*markerTrackers)[i]->renderPreviewHUD(verbose);
	}
	if(eyebrowLineSet) {
		line(frame, eyebrowLineLeft, eyebrowLineRight, Scalar(0, 255, 0), 2);
	}
	if(midLineSet) {
		line(frame, midLineLeft, midLineRight, Scalar(0, 255, 0), 2);
	}
	if(smileLineSet) {
		line(frame, smileLineLeft, smileLineRight, Scalar(0, 255, 0), 2);
	}
	if(centerLineSet) {
		line(frame, centerLineTop, centerLineBottom, Scalar(0, 255, 255), 2);
	}
}

FrameDerivatives *FaceMapper::getFrameDerivatives(void) {
	return frameDerivatives;
}

FaceTracker *FaceMapper::getFaceTracker(void) {
	return faceTracker;
}

MarkerSeparator *FaceMapper::getMarkerSeparator(void) {
	return markerSeparator;
}

EyeRect FaceMapper::getLeftEyeRect(void) {
	return leftEyeRect;
}

EyeRect FaceMapper::getRightEyeRect(void) {
	return rightEyeRect;
}

tuple<Point2d, Point2d, Point2d, bool> FaceMapper::getEyebrowLine(void) {
	return std::make_tuple(eyebrowLineLeft, eyebrowLineRight, eyebrowLineCenter, eyebrowLineSet);
}

tuple<Point2d, Point2d, Point2d, bool> FaceMapper::getMidLine(void) {
	return std::make_tuple(midLineLeft, midLineRight, midLineCenter, midLineSet);
}

tuple<Point2d, Point2d, Point2d, bool> FaceMapper::getSmileLine(void) {
	return std::make_tuple(smileLineLeft, smileLineRight, smileLineCenter, smileLineSet);
}

tuple<Point2d, Point2d, double, double, bool, bool> FaceMapper::getCenterLine(void) {
	return std::make_tuple(centerLineTop, centerLineBottom, centerLineSlope, centerLineIntercept, centerLineIsIntermediate, centerLineSet);
}

void FaceMapper::calculateEyeRects(void) {
	Point2d pointA, pointB;
	double dist;

	leftEyeRect.set = false;
	rightEyeRect.set = false;

	if(!facialFeatures.set) {
		return;
	}

	pointA = facialFeatures.eyeLeftInnerCorner;
	pointB = facialFeatures.eyeLeftOuterCorner;
	dist = Utilities::lineDistance(pointA, pointB);
	leftEyeRect.rect.x = pointA.x;
	leftEyeRect.rect.y = ((pointA.y + pointB.y) / 2.0) - (dist / 2.0);
	leftEyeRect.rect.width = dist;
	leftEyeRect.rect.height = dist;
	leftEyeRect.rect = Utilities::insetBox(leftEyeRect.rect, 1.25); // FIXME - magic numbers
	leftEyeRect.set = true;

	pointA = facialFeatures.eyeRightOuterCorner;
	pointB = facialFeatures.eyeRightInnerCorner;
	dist = Utilities::lineDistance(pointA, pointB);
	rightEyeRect.rect.x = pointA.x;
	rightEyeRect.rect.y = ((pointA.y + pointB.y) / 2.0) - (dist / 2.0);
	rightEyeRect.rect.width = dist;
	rightEyeRect.rect.height = dist;
	rightEyeRect.rect = Utilities::insetBox(rightEyeRect.rect, 1.25); // FIXME - magic numbers
	rightEyeRect.set = true;
}


bool FaceMapper::calculateEyeCenter(MarkerTracker *top, MarkerTracker *bottom, Point2d *center) {
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

void FaceMapper::calculateEyebrowLine(void) {
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
	if(points.size() < 3) {
		return;
	}
	Utilities::lineBestFit(points, &eyebrowLineSlope, &eyebrowLineIntercept);
	eyebrowLineLeft.x = maxX;
	eyebrowLineLeft.y = (eyebrowLineSlope * maxX) + eyebrowLineIntercept;
	eyebrowLineRight.x = minX;
	eyebrowLineRight.y = (eyebrowLineSlope * minX) + eyebrowLineIntercept;
	eyebrowLineCenter.x = ((innerLeftX + innerRightX) / 2.0);
	eyebrowLineCenter.y = (eyebrowLineSlope * eyebrowLineCenter.x) + eyebrowLineIntercept;
	eyebrowLineSet = true;
}

void FaceMapper::calculateMidLine(void) {
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
	Utilities::lineSlopeIntercept(midLineLeft, midLineRight, &midLineSlope, &midLineIntercept);
	midLineSet = true;
}

void FaceMapper::calculateSmileLine(void) {
	smileLineSet = false;
	vector<MarkerTracker *> lipMarkers = {markerLipsLeftCorner, markerLipsLeftTop, markerLipsRightTop, markerLipsRightCorner};
	vector<Point2d> points;
	double minX = -1.0, maxX = -1.0;
	size_t count = lipMarkers.size();
	for(size_t i = 0; i < count; i++) {
		bool pointSet;
		Point2d point;
		std::tie(point, pointSet) = lipMarkers[i]->getMarkerPoint();
		if(!pointSet) {
			return;
		}
		if(minX < 0.0 || point.x < minX) {
			minX = point.x;
		}
		if(maxX < 0.0 || point.x > maxX) {
			maxX = point.x;
		}
		points.push_back(point);
	}
	Utilities::lineBestFit(points, &smileLineSlope, &smileLineIntercept);
	smileLineLeft.x = maxX;
	smileLineLeft.y = (smileLineSlope * maxX) + smileLineIntercept;
	smileLineRight.x = minX;
	smileLineRight.y = (smileLineSlope * minX) + smileLineIntercept;
	smileLineCenter.x = ((smileLineLeft.x + smileLineRight.x) / 2.0);
	smileLineCenter.y = (smileLineSlope * eyebrowLineCenter.x) + smileLineIntercept;
	smileLineSet = true;
}

// void FaceMapper::calculateCenterLine(bool intermediate) {
// 	centerLineSet = false;
// 	bool jawPointSet;
// 	Point2d jawPoint;
// 	if(!eyebrowLineSet || !midLineSet) {
// 		return;
// 	}
// 	centerLineIsIntermediate = intermediate;

// 	vector<Point2d> points = {eyebrowLineCenter, midLineCenter};
// 	if(!intermediate) {
// 		std::tie(jawPoint, jawPointSet) = markerJaw->getMarkerPoint();
// 		if(!jawPointSet || !smileLineSet) {
// 			return;
// 		}
// 		points.push_back(jawPoint);
// 		points.push_back(smileLineCenter);
// 	}
// 	Utilities::lineBestFit(points, &centerLineSlope, &centerLineIntercept);

// 	double boxWidth = Utilities::lineDistance(eyebrowLineLeft, eyebrowLineRight);
// 	double boxHeight = boxWidth / faceAspectRatio;
// 	double boxHeightAboveEyeLine = boxHeight * percentageOfCenterLineAboveEyeLine;

// 	centerLineTop.y = eyeLineCenter.y - boxHeightAboveEyeLine;
// 	centerLineTop.x = (centerLineTop.y - centerLineIntercept) / centerLineSlope;
// 	centerLineBottom.y = eyeLineCenter.y + boxHeight - boxHeightAboveEyeLine;
// 	centerLineBottom.x = (centerLineBottom.y - centerLineIntercept) / centerLineSlope;
// 	centerLineSet = true;
// }

}; //namespace YerFace
