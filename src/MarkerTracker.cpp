
#include "MarkerType.hpp"
#include "MarkerTracker.hpp"
#include "Utilities.hpp"

#include <iostream>
#include <cstdlib>

using namespace std;
using namespace cv;

namespace YerFace {

MarkerTracker::MarkerTracker(json config, MarkerType myMarkerType, FaceMapper *myFaceMapper) {
	markerType = MarkerType(myMarkerType);

	if(markerType.type == NoMarkerAssigned) {
		throw invalid_argument("MarkerTracker class cannot be assigned NoMarkerAssigned");
	}

	YerFace_MutexLock(myStaticMutex);
	for(auto markerTracker : markerTrackers) {
		if(markerTracker->getMarkerType().type == markerType.type) {
			throw invalid_argument("MarkerType collision trying to construct MarkerTracker");
		}
	}
	markerTrackers.push_back(this);
	YerFace_MutexUnlock(myStaticMutex);
	
	faceMapper = myFaceMapper;
	if(faceMapper == NULL) {
		throw invalid_argument("faceMapper cannot be NULL");
	}
	pointSmoothingOverSeconds = config["YerFace"]["MarkerTracker"]["pointSmoothingOverSeconds"];
	if(pointSmoothingOverSeconds <= 0.0) {
		throw invalid_argument("pointSmoothingOverSeconds cannot be less than or equal to zero");
	}
	pointSmoothingExponent = config["YerFace"]["MarkerTracker"]["pointSmoothingExponent"];
	if(pointSmoothingExponent <= 0.0) {
		throw invalid_argument("pointSmoothingExponent cannot be less than or equal to zero");
	}
	pointMotionLowRejectionThreshold = config["YerFace"]["MarkerTracker"]["pointMotionLowRejectionThreshold"];
	if(pointMotionLowRejectionThreshold <= 0.0) {
		throw invalid_argument("pointMotionLowRejectionThreshold cannot be less than or equal to zero");
	}
	pointMotionHighRejectionThreshold = config["YerFace"]["MarkerTracker"]["pointMotionHighRejectionThreshold"];
	if(pointMotionHighRejectionThreshold <= 0.0) {
		throw invalid_argument("pointMotionHighRejectionThreshold cannot be less than or equal to zero");
	}
	markerRejectionResetAfterSeconds = config["YerFace"]["MarkerTracker"]["markerRejectionResetAfterSeconds"];
	if(markerRejectionResetAfterSeconds <= 0.0) {
		throw invalid_argument("markerRejectionResetAfterSeconds cannot be less than or equal to zero");
	}

	frameServer = faceMapper->getFrameServer();
	faceTracker = faceMapper->getFaceTracker();

	previouslyReportedMarkerPoint.set = false;
	previouslyReportedMarkerPoint.timestamp.startTimestamp = -1.0;
	previouslyReportedMarkerPoint.timestamp.estimatedEndTimestamp = -1.0;
	previouslyReportedMarkerPoint.timestamp.frameNumber = -1;

	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	string loggerName = "MarkerTracker<" + (string)markerType.toString() + ">";
	logger = new Logger(loggerName.c_str());
	logger->debug("MarkerTracker object constructed and ready to go!");
}

MarkerTracker::~MarkerTracker() noexcept(false) {
	logger->debug("MarkerTracker object destructing...");
	YerFace_MutexLock(myStaticMutex);
	for(vector<MarkerTracker *>::iterator iterator = markerTrackers.begin(); iterator != markerTrackers.end(); ++iterator) {
		if(*iterator == this) {
			markerTrackers.erase(iterator);
			return;
		}
	}
	YerFace_MutexUnlock(myStaticMutex);
	SDL_DestroyMutex(myMutex);
	delete logger;
}

MarkerType MarkerTracker::getMarkerType(void) {
	return markerType;
}

void MarkerTracker::processFrame(FrameNumber frameNumber) {
	MarkerPoint markerPoint;
	markerPoint.set = false;

	assignMarkerPoint(frameNumber, &markerPoint);

	calculate3dMarkerPoint(frameNumber, &markerPoint);

	WorkingFrame *workingFrame = frameServer->getWorkingFrame(frameNumber);

	YerFace_MutexLock(myMutex);
	performMarkerPointValidationAndSmoothing(workingFrame, frameNumber, &markerPoint);

	markerPoints[frameNumber] = markerPoint;
	YerFace_MutexUnlock(myMutex);
}

void MarkerTracker::assignMarkerPoint(FrameNumber frameNumber, MarkerPoint *markerPoint) {
	FacialFeatures facialFeatures = faceTracker->getFacialFeatures(frameNumber);
	if(!facialFeatures.set) {
		return;
	}
	switch(markerType.type) {
		default:
			throw logic_error("Unsupported marker type!");
		case EyelidLeftTop:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_LEFTEYE_UPPERLID_RIGHT],
				facialFeatures.features[IDX_LEFTEYE_UPPERLID_LEFT]
			});
			markerPoint->set = true;
			break;
		case EyelidLeftBottom:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_LEFTEYE_LOWERLID_RIGHT],
				facialFeatures.features[IDX_LEFTEYE_LOWERLID_LEFT]
			});
			markerPoint->set = true;
			break;
		case EyelidRightTop:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_RIGHTEYE_UPPERLID_RIGHT],
				facialFeatures.features[IDX_RIGHTEYE_UPPERLID_LEFT]
			});
			markerPoint->set = true;
			break;
		case EyelidRightBottom:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_RIGHTEYE_LOWERLID_RIGHT],
				facialFeatures.features[IDX_RIGHTEYE_LOWERLID_LEFT]
			});
			markerPoint->set = true;
			break;
		case EyebrowLeftInner:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_LEFTEYEBROW_NEARINNER],
				facialFeatures.features[IDX_LEFTEYEBROW_FARINNER]
			});
			markerPoint->set = true;
			break;
		case EyebrowLeftMiddle:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_LEFTEYEBROW_MIDDLE],
				facialFeatures.features[IDX_LEFTEYEBROW_MIDDLE],
				facialFeatures.features[IDX_LEFTEYEBROW_NEAROUTER],
				facialFeatures.features[IDX_LEFTEYEBROW_NEARINNER]
			});
			markerPoint->set = true;
			break;
		case EyebrowLeftOuter:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_LEFTEYEBROW_NEAROUTER],
				facialFeatures.features[IDX_LEFTEYEBROW_FAROUTER]
			});
			markerPoint->set = true;
			break;
		case EyebrowRightInner:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_RIGHTEYEBROW_NEARINNER],
				facialFeatures.features[IDX_RIGHTEYEBROW_FARINNER]
			});
			markerPoint->set = true;
			break;
		case EyebrowRightMiddle:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_RIGHTEYEBROW_MIDDLE],
				facialFeatures.features[IDX_RIGHTEYEBROW_MIDDLE],
				facialFeatures.features[IDX_RIGHTEYEBROW_NEAROUTER],
				facialFeatures.features[IDX_RIGHTEYEBROW_NEARINNER]
			});
			markerPoint->set = true;
			break;
		case EyebrowRightOuter:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_RIGHTEYEBROW_NEAROUTER],
				facialFeatures.features[IDX_RIGHTEYEBROW_FAROUTER]
			});
			markerPoint->set = true;
			break;
		case LipsLeftCorner:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_MOUTHOUT_LEFT_CORNER],
				facialFeatures.features[IDX_MOUTHIN_LEFT_CORNER]
			});
			markerPoint->set = true;
			break;
		case LipsLeftTop:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_MOUTHOUT_LEFT_NEAROUTER_TOP],
				facialFeatures.features[IDX_MOUTHOUT_LEFT_FAROUTER_TOP]
			});
			markerPoint->set = true;
			break;
		case LipsLeftBottom:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_MOUTHOUT_LEFT_NEAROUTER_BOTTOM],
				facialFeatures.features[IDX_MOUTHOUT_LEFT_FAROUTER_BOTTOM]
			});
			markerPoint->set = true;
			break;
		case LipsRightCorner:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_MOUTHOUT_RIGHT_CORNER],
				facialFeatures.features[IDX_MOUTHIN_RIGHT_CORNER]
			});
			markerPoint->set = true;
			break;
		case LipsRightTop:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_MOUTHOUT_RIGHT_NEAROUTER_TOP],
				facialFeatures.features[IDX_MOUTHOUT_RIGHT_FAROUTER_TOP]
			});
			markerPoint->set = true;
			break;
		case LipsRightBottom:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_MOUTHOUT_RIGHT_NEAROUTER_BOTTOM],
				facialFeatures.features[IDX_MOUTHOUT_RIGHT_FAROUTER_BOTTOM]
			});
			markerPoint->set = true;
			break;
		case Jaw:
			markerPoint->point = Utilities::averagePoint({
				facialFeatures.features[IDX_JAWLINE_7],
				facialFeatures.features[IDX_JAWLINE_8],
				facialFeatures.features[IDX_JAWLINE_9]
			});
			markerPoint->set = true;
			break;
	}
}

void MarkerTracker::calculate3dMarkerPoint(FrameNumber frameNumber, MarkerPoint *markerPoint) {
	if(!markerPoint->set) {
		return;
	}
	FacialPose facialPose = faceTracker->getFacialPose(frameNumber);
	FacialCameraModel cameraModel = faceTracker->getFacialCameraModel();
	if(!facialPose.set || !cameraModel.set) {
		markerPoint->set = false;
		return;
	}
	FacialPlane facialPlane = faceTracker->getCalculatedFacialPlaneForWorkingFacialPose(frameNumber, markerType);
	Mat homogeneousPoint = (Mat_<double>(3,1) << markerPoint->point.x, markerPoint->point.y, 1.0);
	Mat worldPoint = cameraModel.cameraMatrix.inv() * homogeneousPoint;
	Point3d intersection;
	Point3d rayOrigin = Point3d(0,0,0);
	Vec3d rayVector = Vec3d(worldPoint.at<double>(0), worldPoint.at<double>(1), worldPoint.at<double>(2));
	if(!Utilities::rayPlaneIntersection(intersection, rayOrigin, rayVector, facialPlane.planePoint, facialPlane.planeNormal)) {
		logger->warn("Failed 3d ray/plane intersection with face plane! No update to 3d marker point.");
		return;
	}
	Mat markerMat = (Mat_<double>(3, 1) << intersection.x, intersection.y, intersection.z);
	markerMat = markerMat - facialPose.translationVectorInternal;
	markerMat = facialPose.rotationMatrixInternal.inv() * markerMat;
	markerPoint->point3d = Point3d(markerMat.at<double>(0), markerMat.at<double>(1), markerMat.at<double>(2));
	// logger->verbose("Recovered approximate 3D position: <%.03f, %.03f, %.03f>", markerPoint->point3d.x, markerPoint->point3d.y, markerPoint->point3d.z);
}

void MarkerTracker::performMarkerPointValidationAndSmoothing(WorkingFrame *workingFrame, FrameNumber frameNumber, MarkerPoint *markerPoint) {
	if(!markerPoint->set) {
		return;
	}

	FrameTimestamps frameTimestamps = workingFrame->frameTimestamps;
	double timeScale = (double)(frameTimestamps.estimatedEndTimestamp - frameTimestamps.startTimestamp) / (double)(1.0 / 30.0);
	double frameTimestamp = frameTimestamps.startTimestamp;
	markerPoint->timestamp = frameTimestamps;

	YerFace_MutexLock(myMutex);

	//// REJECT BAD MARKER POINTS ////

	double distance;
	if(previouslyReportedMarkerPoint.set) {
		if(previouslyReportedMarkerPoint.timestamp.frameNumber >= frameTimestamps.frameNumber) {
			logger->error("MarkerTracker is being fed frames out of order! This will wreak havoc with smoothing code.");
		}

		distance = Utilities::lineDistance(markerPoint->point3d, previouslyReportedMarkerPoint.point3d);
		if(distance > (pointMotionHighRejectionThreshold * timeScale)) {
			logger->warn("Dropping marker position due to high motion (%.02lf)!", distance);
			if(markerPoint->timestamp.startTimestamp - previouslyReportedMarkerPoint.timestamp.startTimestamp >= markerRejectionResetAfterSeconds) {
				logger->warn("Marker position has come back bad consistantly for %.02lf seconds! Unsetting the marker completely.", markerPoint->timestamp.startTimestamp - previouslyReportedMarkerPoint.timestamp.startTimestamp);
				previouslyReportedMarkerPoint.set = false;
			}
			*markerPoint = previouslyReportedMarkerPoint;
			YerFace_MutexUnlock(myMutex);
			return;
		}
	}

	//// PERFORM MARKER POINT SMOOTHING ////

	markerPointSmoothingBuffer.push_back(*markerPoint);
	while(markerPointSmoothingBuffer.front().timestamp.startTimestamp <= (frameTimestamp - pointSmoothingOverSeconds)) {
		markerPointSmoothingBuffer.pop_front();
	}

	MarkerPoint tempPoint = *markerPoint;
	tempPoint.point3d.x = 0;
	tempPoint.point3d.y = 0;
	tempPoint.point3d.z = 0;

	double combinedWeights = 0.0;
	for(MarkerPoint point : markerPointSmoothingBuffer) {
		double progress = (point.timestamp.startTimestamp - (frameTimestamp - pointSmoothingOverSeconds)) / pointSmoothingOverSeconds;
		double weight = std::pow(progress, (double)pointSmoothingExponent) - combinedWeights;
		combinedWeights += weight;
		tempPoint.point3d.x += point.point3d.x * weight;
		tempPoint.point3d.y += point.point3d.y * weight;
		tempPoint.point3d.z += point.point3d.z * weight;
	}

	//// REJECT NOISY UPDATES ////

	bool reportNewPoint = true;
	if(previouslyReportedMarkerPoint.set) {
		distance = Utilities::lineDistance(tempPoint.point3d, previouslyReportedMarkerPoint.point3d);
		if(distance < (pointMotionLowRejectionThreshold * timeScale)) {
			reportNewPoint = false;
		}
	}

	if(reportNewPoint) {
		*markerPoint = tempPoint;
		previouslyReportedMarkerPoint = *markerPoint;
	} else {
		*markerPoint = previouslyReportedMarkerPoint;
	}

	YerFace_MutexUnlock(myMutex);
}

void MarkerTracker::renderPreviewHUD(Mat frame, FrameNumber frameNumber, int density) {
	Scalar color = Scalar(0, 0, 255);
	if(markerType.type == EyelidLeftBottom || markerType.type == EyelidRightBottom || markerType.type == EyelidLeftTop || markerType.type == EyelidRightTop) {
		color = Scalar(0, 255, 255);
		if(markerType.type == EyelidLeftTop || markerType.type == EyelidRightTop) {
			color[1] = 127;
		}
	} else if(markerType.type == EyebrowLeftInner || markerType.type == EyebrowLeftMiddle || markerType.type == EyebrowLeftOuter || markerType.type == EyebrowRightInner || markerType.type == EyebrowRightMiddle || markerType.type == EyebrowRightOuter) {
		color = Scalar(255, 0, 0);
		if(markerType.type == EyebrowLeftMiddle || markerType.type == EyebrowRightMiddle) {
			color[2] = 127;
		} else if(markerType.type == EyebrowLeftOuter || markerType.type == EyebrowRightOuter) {
			color[2] = 255;
		}
	} else if(markerType.type == Jaw) {
		color = Scalar(0, 255, 0);
	} else if(markerType.type == LipsLeftCorner || markerType.type == LipsRightCorner || markerType.type == LipsLeftTop || markerType.type == LipsRightTop || markerType.type == LipsLeftBottom || markerType.type == LipsRightBottom) {
		color = Scalar(0, 0, 255);
		if(markerType.type == LipsLeftCorner || markerType.type == LipsRightCorner) {
			color[1] = 127;
		} else if(markerType.type == LipsLeftTop || markerType.type == LipsRightTop) {
			color[0] = 127;
		} else {
			color[0] = 127;
			color[1] = 127;
		}
	}
	YerFace_MutexLock(myMutex);
	if(density > 0 && markerPoints[frameNumber].set) {
		Utilities::drawX(frame, markerPoints[frameNumber].point, color, 10, 2);
	}
	YerFace_MutexUnlock(myMutex);
}

void MarkerTracker::frameStatusNew(FrameNumber frameNumber) {
	MarkerPoint markerPoint;
	markerPoint.set = false;
	YerFace_MutexLock(myMutex);
	markerPoints[frameNumber] = markerPoint;
	YerFace_MutexUnlock(myMutex);
}

void MarkerTracker::frameStatusGone(FrameNumber frameNumber) {
	YerFace_MutexLock(myMutex);
	markerPoints.erase(frameNumber);
	YerFace_MutexUnlock(myMutex);
}

MarkerPoint MarkerTracker::getMarkerPoint(FrameNumber frameNumber) {
	MarkerPoint val;
	YerFace_MutexLock(myMutex);
	val = markerPoints[frameNumber];
	YerFace_MutexUnlock(myMutex);
	return val;
}

vector<MarkerTracker *> MarkerTracker::markerTrackers;
SDL_mutex *MarkerTracker::myStaticMutex = SDL_CreateMutex();

vector<MarkerTracker *> MarkerTracker::getMarkerTrackers(void) {
	YerFace_MutexLock(myStaticMutex);
	auto val = markerTrackers;
	YerFace_MutexUnlock(myStaticMutex);
	return val;
}

MarkerTracker *MarkerTracker::getMarkerTrackerByType(MarkerType markerType) {
	YerFace_MutexLock(myStaticMutex);
	for(auto markerTracker : markerTrackers) {
		if(markerTracker->getMarkerType().type == markerType.type) {
			YerFace_MutexUnlock(myStaticMutex);
			return markerTracker;
		}
	}
	YerFace_MutexUnlock(myStaticMutex);
	return NULL;
}

}; //namespace YerFace
