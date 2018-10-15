#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"
#include "FFmpegDriver.hpp"
#include "SDLDriver.hpp"
#include "OutputDriver.hpp"
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

class SphinxPhoneme {
public:
	string pbPhoneme;
	double startTime, endTime;
	int utteranceIndex;
};

class SphinxWorkingVariables {
public:
	SphinxWorkingVariables(void);
	PrestonBlairPhonemes lipFlapping;
	int framesIncluded;
	double maxAmplitude;
	bool peak, inSpeech;
};

class SphinxDriver {
public:
	SphinxDriver(json config, FrameDerivatives *myFrameDerivatives, FFmpegDriver *myFFmpegDriver, SDLDriver *mySDLDriver, OutputDriver *myOutputDriver, bool myLowLatency);
	~SphinxDriver() noexcept(false);
	void advanceWorkingToCompleted(void);
	void renderPreviewHUD(void);
	void drainPipelineDataNow(void);
private:
	void initializeRecognitionThread(void);
	void processPhonemesIntoVideoFrames(bool draining);
	void handleProcessedVideoFrames(void);
	void processUtteranceHypothesis(void);
	void processAudioAmplitude(PocketSphinx::int16 const *buf, int samples);
	void processLipFlappingAudio(void);
	static int runRecognitionLoop(void *ptr);
	SphinxAudioFrame *getNextAvailableAudioFrame(int desiredBufferSize);
	static void FFmpegDriverAudioFrameCallback(void *userdata, uint8_t *buf, int audioSamples, int audioBytes, double timestamp);
	
	string hiddenMarkovModel, allPhoneLM;
	string lipFlappingTargetPhoneme;
	double lipFlappingResponseThreshold, lipFlappingNonLinearResponse, lipFlappingNotInSpeechScale;
	json sphinxToPrestonBlairPhonemeMapping;
	FrameDerivatives *frameDerivatives;
	FFmpegDriver *ffmpegDriver;
	SDLDriver *sdlDriver;
	OutputDriver *outputDriver;
	bool lowLatency;

	double vuMeterWidth, vuMeterWarningThreshold, vuMeterPeakHoldSeconds;

	Logger *logger;

	PocketSphinx::ps_decoder_t *pocketSphinx;
	PocketSphinx::cmd_ln_t *pocketSphinxConfig;
	
	SDL_mutex *myWrkMutex, *myCmpMutex;
	SDL_cond *myWrkCond;
	SDL_Thread *recognizerThread;

	bool drained;
	bool recognizerRunning;
	bool utteranceRestarted, inSpeech;
	int utteranceIndex;
	double timestampOffset;
	bool timestampOffsetSet;

	list<SphinxPhoneme> phonemeBuffer;

	list<SphinxAudioFrame *> audioFrameQueue;
	list<SphinxAudioFrame *> audioFramesAllocated;

	list<SphinxVideoFrame *> videoFrames;

	SphinxWorkingVariables working, completed;

	double vuMeterLastSetPeak;
};

}; //namespace YerFace
