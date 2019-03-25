#pragma once

#include "Logger.hpp"
#include "FrameServer.hpp"
#include "FFmpegDriver.hpp"
#include "SDLDriver.hpp"
#include "OutputDriver.hpp"
#include "Utilities.hpp"
#include "Status.hpp"
#include "WorkerPool.hpp"
#include "PreviewHUD.hpp"

namespace PocketSphinx {
extern "C" {
#include <pocketsphinx.h>
}
} //Namespace PocketSphinx

using namespace std;

namespace YerFace {

#define YERFACE_SPHINX_SAMPLERATE 16000

// We use the Preston Blair 'toon phoneme set. See: http://minyos.its.rmit.edu.au/aim/a_notes/mouth_shapes_01.html
// This class will represent a single video frame's snapshot of phoneme representation.
// There will be Booleans for if we saw the phoneme at all, and floating point percentages (0.0 - 1.0) for the influence of each phoneme on the frame.
class PrestonBlairPhonemes {
public:
	PrestonBlairPhonemes(void);
	json seen, percent;
};

class SphinxPhoneme {
public:
	string pbPhoneme;
	double startTime, endTime;
	int utteranceIndex;
};

class SphinxRecognizerResult {
public:
	double startTimestamp, endTimestamp;
	double maxAmplitude;
	bool peak, inSpeech;
	list<SphinxPhoneme> phonemeBuffer;
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
	bool isLipFlappingReady, isLipFlappingProcessed;
	FrameTimestamps timestamps;
	// double realEndTimestamp;
	PrestonBlairPhonemes phonemes;
	bool peak;
	double maxAmplitude;
};

// class SphinxWorkingVariables {
// public:
// 	SphinxWorkingVariables(void);
// 	PrestonBlairPhonemes lipFlapping;
// 	int framesIncluded;
// 	double maxAmplitude;
// 	bool peak, inSpeech;
// };

class SphinxDriver {
public:
	SphinxDriver(json config, Status *myStatus, FrameServer *myFrameServer, FFmpegDriver *myFFmpegDriver, SDLDriver *mySDLDriver, OutputDriver *myOutputDriver, PreviewHUD *myPreviewHUD, bool myLowLatency);
	~SphinxDriver() noexcept(false);
	// void advanceWorkingToCompleted(void);
	void renderPreviewHUD(Mat frame, FrameNumber frameNumber, int density);
private:
	// void processPhonemesIntoVideoFrames(bool draining);
	// void handleProcessedVideoFrames(void);
	void processUtteranceHypothesis(SphinxRecognizerResult *result);
	void processAudioAmplitude(SphinxAudioFrame *audioFrame, SphinxRecognizerResult *result);
	void processLipFlappingAudio(SphinxVideoFrame *videoFrame);
	SphinxAudioFrame *getNextAvailableAudioFrame(int desiredBufferSize);
	static void FFmpegDriverAudioFrameCallback(void *userdata, uint8_t *buf, int audioSamples, int audioBytes, double timestamp);
	static void handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps);
	static bool recognitionWorkerHandler(WorkerPoolWorker *worker);
	static bool lipFlappingWorkerHandler(WorkerPoolWorker *worker);
	
	string hiddenMarkovModel, allPhoneLM;
	string lipFlappingTargetPhoneme;
	double lipFlappingResponseThreshold, lipFlappingNonLinearResponse, lipFlappingNotInSpeechScale;
	json sphinxToPrestonBlairPhonemeMapping;
	Status *status;
	FrameServer *frameServer;
	FFmpegDriver *ffmpegDriver;
	SDLDriver *sdlDriver;
	OutputDriver *outputDriver;
	PreviewHUD *previewHUD;
	bool lowLatency;
	Logger *logger;

	double vuMeterWidth, vuMeterWarningThreshold, vuMeterPeakHoldSeconds;

	PocketSphinx::ps_decoder_t *pocketSphinx;
	PocketSphinx::cmd_ln_t *pocketSphinxConfig;

	WorkerPool *recognitionWorkerPool;
	SDL_mutex *recognitionMutex;
	double timestampOffset;
	bool timestampOffsetSet;
	list<SphinxAudioFrame *> audioFrameQueue;
	list<SphinxAudioFrame *> audioFramesAllocated;
	bool utteranceRestarted, inSpeech;
	int utteranceIndex;
	list<SphinxRecognizerResult> recognitionResults;

	WorkerPool *lipFlappingWorkerPool;
	SDL_mutex *lipFlappingMutex;
	unordered_map<FrameNumber, SphinxVideoFrame *> pendingLipFlappingFrames;
};

}; //namespace YerFace
