#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"
#include "FFmpegDriver.hpp"
#include "Utilities.hpp"

namespace PocketSphinx {
extern "C" {
#include <pocketsphinx.h>
}
} //Namespace PocketSphinx

using namespace std;

namespace YerFace {

// We use the Preston Blair 'toon phoneme set. See: http://minyos.its.rmit.edu.au/aim/a_notes/mouth_shapes_01.html
// This class will represent a single video frame's snapshot of phoneme representation.
// There will be Booleans for if we saw the phoneme at all, and floating point percentages (0.0 - 1.0) for the influence of each phoneme on the frame.
class PrestonBlairPhonemes {
public:
	PrestonBlairPhonemes(void);
	json seen, percent;
};

class SphinxAudioFrame {
public:
	uint8_t *buf;
	int pos;
	int audioSamples;
	int audioBytes;
	int bufferSize;
	double timestamp;
	bool inUse;
};

class SphinxVideoFrame {
public:
	bool processed;
	FrameTimestamps timestamps;
	double realEndTimestamp;
	PrestonBlairPhonemes phonemes;
};

class SphinxDriver {
public:
	SphinxDriver(json config, FrameDerivatives *myFrameDerivatives, FFmpegDriver *myFFmpegDriver);
	~SphinxDriver();
	void advanceWorkingToCompleted(void);
	void drainPipelineDataNow(void);
private:
	void initializeRecognitionThread(void);
	void handleProcessedVideoFrames(void);
	void processUtteranceHypothesis(void);
	static int runRecognitionLoop(void *ptr);
	SphinxAudioFrame *getNextAvailableAudioFrame(int desiredBufferSize);
	static void FFmpegDriverAudioFrameCallback(void *userdata, uint8_t *buf, int audioSamples, int audioBytes, int bufferSize, double timestamp);
	
	string hiddenMarkovModel, allPhoneLM;
	json sphinxToPrestonBlairPhonemeMapping;
	FrameDerivatives *frameDerivatives;
	FFmpegDriver *ffmpegDriver;

	Logger *logger;

	PocketSphinx::ps_decoder_t *pocketSphinx;
	PocketSphinx::cmd_ln_t *pocketSphinxConfig;
	
	SDL_mutex *myWrkMutex;
	SDL_cond *myWrkCond;
	SDL_Thread *recognizerThread;

	bool recognizerRunning;
	bool utteranceRestarted, inSpeech;
	int utteranceIndex;
	double timestampOffset;
	bool timestampOffsetSet;

	list<SphinxAudioFrame *> audioFrameQueue;
	list<SphinxAudioFrame *> audioFramesAllocated;

	list<SphinxVideoFrame *> videoFrames;
};

}; //namespace YerFace
