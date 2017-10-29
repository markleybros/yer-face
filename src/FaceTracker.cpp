
#include "FaceTracker.hpp"
#include <exception>
#include <stdio.h>

using namespace std;

namespace YerFace {

FaceTracker::FaceTracker(string myClassifierFileName, FrameDerivatives *myFrameDerivatives) {
	classifierFileName = myClassifierFileName;
	frameDerivatives = myFrameDerivatives;
	trackerState = DETECTING;
	trackingBoxScaleFactor = 0.75; //FIXME - magic numbers
	classificationBoxSet = false;
	trackingBoxSet = false;

	if(!cascadeClassifier.load(classifierFileName)) {
		throw invalid_argument("Unable to load specified classifier.");
	}
	fprintf(stderr, "FaceTracker object constructed and ready to go!\n");
}

TrackerState FaceTracker::processCurrentFrame(void) {
	if(trackerState == DETECTING || trackerState == LOST || trackerState == STALE) {
        classificationBoxSet = false;
		std::vector<Rect> faces;
		//FIXME - Magic number 30x30 ?
        cascadeClassifier.detectMultiScale(frameDerivatives->getClassificationFrame(), faces, 1.1, 3, 0|CASCADE_SCALE_IMAGE, Size(30, 30) );

        int largestFace = -1;
        int largestFaceArea = -1;
        for( size_t i = 0; i < faces.size(); i++ ) {
            if(faces[i].area() > largestFaceArea) {
                largestFace = i;
                largestFaceArea = faces[i].area();
            }
        }
        if(largestFace >= 0) {
            classificationBoxSet = true;
			double classificationScaleFactor = frameDerivatives->getClassificationScaleFactor();
            classificationBox = faces[largestFace];
            classificationBoxNormalSize = Rect(
                (double)classificationBox.x / classificationScaleFactor,
                (double)classificationBox.y / classificationScaleFactor,
                (double)classificationBox.width / classificationScaleFactor,
                (double)classificationBox.height / classificationScaleFactor);
            //Switch to TRACKING
            trackerState = TRACKING;
            #if (CV_MINOR_VERSION < 3)
            tracker = Tracker::create("KCF");
            #else
            tracker = TrackerKCF::create();
            #endif
            double trackingBoxWidth = (double)classificationBoxNormalSize.width * trackingBoxScaleFactor;
            double trackingBoxHeight = (double)classificationBoxNormalSize.height * trackingBoxScaleFactor;
            Rect trackingBox = Rect(
                (double)classificationBoxNormalSize.x + (((double)classificationBoxNormalSize.width - trackingBoxWidth) / 2.0),
                (double)classificationBoxNormalSize.y + (((double)classificationBoxNormalSize.height - trackingBoxHeight) / 2.0),
                trackingBoxWidth,
                trackingBoxHeight);
            tracker->init(frameDerivatives->getCurrentFrame(), trackingBox);
			//FIXME - more magic numbers boo
            staleCounter = 15;
        }
    } else if(trackerState == TRACKING) {
        bool trackSuccess = tracker->update(frameDerivatives->getCurrentFrame(), trackingBox);
        if(!trackSuccess) {
            fprintf(stderr, "FaceTracker WARNING! Track lost. Will keep searching...\n");
            trackingBoxSet = false;
            trackerState = LOST;
			//Attempt to re-process this same frame in LOST mode.
            return this->processCurrentFrame();
        } else {
            trackingBoxSet = true;
            staleCounter--;
            if(staleCounter <= 0) {
                trackerState = STALE;
            }
        }
    }
	return trackerState;
}

void FaceTracker::renderPreviewHUD(void) {
	Mat frame = frameDerivatives->getPreviewFrame();
	if(classificationBoxSet) {
		rectangle(frame, classificationBoxNormalSize, Scalar( 0, 255, 0 ), 1, 0);
	}
	if(trackingBoxSet) {
		rectangle(frame, trackingBox, Scalar( 255, 0, 0 ), 1, 0);
	}
}

TrackerState FaceTracker::getTrackerState(void) {
	return trackerState;
}

}; //namespace YerFace
