#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"
#include "FFmpegDriver.hpp"

namespace PocketSphinx {
extern "C" {
#include <pocketsphinx.h>
}
} //Namespace PS

using namespace std;

namespace YerFace {

class SphinxDriver {
public:
	SphinxDriver(string myHiddenMarkovModel, string myAllPhoneLM, FrameDerivatives *myFrameDerivatives, FFmpegDriver *myFFmpegDriver);
	~SphinxDriver();
private:
	static void FFmpegDriverAudioFrameCallback(void *userdata, uint8_t *buf, int audioSamples, int audioBytes, int bufferSize, double timestamp);
	
	string hiddenMarkovModel, allPhoneLM;
	FrameDerivatives *frameDerivatives;
	FFmpegDriver *ffmpegDriver;

	Logger *logger;

	PocketSphinx::ps_decoder_t *pocketSphinx;
	PocketSphinx::cmd_ln_t *pocketSphinxConfig;
	
	bool utteranceRestarted, inSpeech;
	double timestampOffset;
	bool timestampOffsetSet;
};

}; //namespace YerFace
