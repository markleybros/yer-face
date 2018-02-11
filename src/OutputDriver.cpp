
#include "OutputDriver.hpp"
#include "Utilities.hpp"

#include "json.hpp"
#include <cstring>

//FIXME - get rid of this please (we don't want to use named pipes ultimately -- this is just a thin slice to get our data into blender quickly)
#include <sys/stat.h>
#include <fcntl.h>
#include <sstream>
#include <iostream>

using json = nlohmann::json;
using namespace cv;

namespace YerFace {

OutputDriver::OutputDriver(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker, SDLDriver *mySDLDriver) {
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	faceTracker = myFaceTracker;
	if(faceTracker == NULL) {
		throw invalid_argument("faceTracker cannot be NULL");
	}
	sdlDriver = mySDLDriver;
	if(sdlDriver == NULL) {
		throw invalid_argument("sdlDriver cannot be NULL");
	}
	if((pipeHandle = open(OUTPUTDRIVER_NAMED_PIPE, O_WRONLY)) == -1) {
		throw runtime_error("tried to open named pipe " OUTPUTDRIVER_NAMED_PIPE " for writing but failed!");
	}
	autoBasisTransmitted = false;
	basisFlagged = false;
	sdlDriver->onBasisFlagEvent([this] (void) -> void {
		this->logger->info("Got a Basis Flag event. Handling...");
		this->basisFlagged = true;
	});
	logger = new Logger("OutputDriver");
	logger->debug("OutputDriver object constructed and ready to go!");
};

OutputDriver::~OutputDriver() {
	logger->debug("OutputDriver object destructing...");
	delete logger;
}

void OutputDriver::handleCompletedFrame(void) {
	FrameTimestamps frameTimestamps = frameDerivatives->getCompletedFrameTimestamps();

	json frame;
	frame["meta"] = nullptr;
	frame["meta"]["frameNumber"] = frameTimestamps.frameNumber;
	frame["meta"]["startTime"] = frameTimestamps.startTimestamp;
	frame["meta"]["basis"] = false; //Default to no basis. Will override below as necessary.

	bool allPropsSet = true;
	FacialPose facialPose = faceTracker->getCompletedFacialPose();
	if(facialPose.set) {
		frame["pose"] = nullptr;
		Vec3d angles = Utilities::rotationMatrixToEulerAngles(facialPose.rotationMatrix);
		frame["pose"]["rotation"] = { {"x", angles[0]}, {"y", angles[1]}, {"z", angles[2]} };
		frame["pose"]["translation"] = { {"x", facialPose.translationVector.at<double>(0)}, {"y", facialPose.translationVector.at<double>(1)}, {"z", facialPose.translationVector.at<double>(2)} };
	} else {
		allPropsSet = false;
	}

	json trackers;
	auto markerTrackers = MarkerTracker::getMarkerTrackers();
	for(auto markerTracker : markerTrackers) {
		MarkerPoint markerPoint = markerTracker->getCompletedMarkerPoint();
		if(markerPoint.set) {
			string trackerName = markerTracker->getMarkerType().toString();
			trackers[trackerName.c_str()]["position"] = { {"x", markerPoint.point3d.x}, {"y", markerPoint.point3d.y}, {"z", markerPoint.point3d.z} };
		} else {
			allPropsSet = false;
		}
	}
	if(trackers.size()) {
		frame["trackers"] = trackers;
	}

	if(allPropsSet && autoBasisTransmitted) {
		autoBasisTransmitted = true;
		frame["meta"]["basis"] = true;
		logger->info("All properties set. Transmitting initial basis flag automatically.");
	}
	if(basisFlagged) {
		autoBasisTransmitted = true;
		basisFlagged = false;
		frame["meta"]["basis"] = true;
		logger->info("Transmitting basis flag based on received basis event.");
	}

	string jsonString = frame.dump(-1, ' ', true) + "\n";
	const char *jsonStringC = jsonString.c_str();
	// logger->verbose("Completed frame... %s", jsonStringC);
	//FIXME - get rid of this please (we don't want to use named pipes ultimately -- this is just a thin slice to get our data into blender quickly)
	size_t jsonStringLen = strlen(jsonStringC);
	ssize_t writeRet;
	size_t wrote = 0;
	while(wrote < jsonStringLen) {
		writeRet = write(pipeHandle, jsonStringC + wrote, jsonStringLen - wrote);
		if(writeRet < 0) {
			throw runtime_error("failed writing to named pipe " OUTPUTDRIVER_NAMED_PIPE);
		} else {
			wrote += writeRet;
		}
	}
}

}; //namespace YerFace
