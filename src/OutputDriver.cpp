
#include "OutputDriver.hpp"

#include "json.hpp"

using json = nlohmann::json;

namespace YerFace {

OutputDriver::OutputDriver(FrameDerivatives *myFrameDerivatives) {
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
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
	frame["meta"]["basis"] = false;
	logger->verbose("Completed frame... %s", frame.dump().c_str());
}

}; //namespace YerFace
