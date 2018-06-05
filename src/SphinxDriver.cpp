
#include "SphinxDriver.hpp"
#include "Utilities.hpp"

using namespace std;
using namespace PocketSphinx;

namespace YerFace {

SphinxDriver::SphinxDriver(json config, FrameDerivatives *myFrameDerivatives, FFmpegDriver *myFFmpegDriver) {
	hiddenMarkovModel = config["YerFace"]["SphinxDriver"]["hiddenMarkovModel"];
	allPhoneLM = config["YerFace"]["SphinxDriver"]["allPhoneLM"];
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	ffmpegDriver = myFFmpegDriver;
	if(ffmpegDriver == NULL) {
		throw invalid_argument("ffmpegDriver cannot be NULL");
	}
	logger = new Logger("SphinxDriver");
	
	pocketSphinx = NULL;
	pocketSphinxConfig = NULL;
	timestampOffsetSet = false;
	utteranceRestarted = false;
	inSpeech = false;
	if((myWrkMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((myCmpMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	
	logger->info("Initializing PocketSphinx with Models... <HMM: %s, AllPhone: %s>", hiddenMarkovModel.c_str(), allPhoneLM.c_str());
	// Configuration for phoneme recognition from: https://cmusphinx.github.io/wiki/phonemerecognition/
	if((pocketSphinxConfig = cmd_ln_init(NULL, ps_args(), TRUE, "-hmm", hiddenMarkovModel.c_str(), "-allphone", allPhoneLM.c_str(), "-beam", "1e-20", "-pbeam", "1e-20", "-lw", "2.0", NULL)) == NULL) {
		throw runtime_error("Failed to create PocketSphinx configuration object!");
	}
	
	if((pocketSphinx = ps_init(pocketSphinxConfig)) == NULL) {
		throw runtime_error("Failed to create PocketSphinx speech recognizer!");
	}
	
	utteranceIndex = 1;
	if(ps_start_utt(pocketSphinx) != 0) {
		throw runtime_error("Failed to start PocketSphinx utterance");
	}
	
	//This audio format is the only audio format that the Pocket Sphinx phoneme recognizer is trained to work on.
	AudioFrameCallback audioFrameCallback;
	audioFrameCallback.userdata = (void *)this;
	audioFrameCallback.channelLayout = AV_CH_LAYOUT_MONO;
	audioFrameCallback.sampleFormat = AV_SAMPLE_FMT_S16;
	audioFrameCallback.sampleRate = 16000;
	audioFrameCallback.callback = FFmpegDriverAudioFrameCallback;
	ffmpegDriver->registerAudioFrameCallback(audioFrameCallback);
	
	initializeRecognitionThread();

	logger->debug("SphinxDriver object constructed and ready to go!");
}

SphinxDriver::~SphinxDriver() {
	logger->debug("SphinxDriver object destructing...");
	destroyRecognitionThread();
	ps_free(pocketSphinx);
	cmd_ln_free_r(pocketSphinxConfig);
	SDL_DestroyMutex(myWrkMutex);
	SDL_DestroyMutex(myCmpMutex);
	delete logger;
}

void SphinxDriver::initializeRecognitionThread(void) {
	recognizerRunning = true;
	if((myWrkCond = SDL_CreateCond()) == NULL) {
		throw runtime_error("Failed creating condition!");
	}
	if((recognizerThread = SDL_CreateThread(SphinxDriver::runRecognitionLoop, "SphinxLoop", (void *)this)) == NULL) {
		throw runtime_error("Failed starting thread!");
	}
}

void SphinxDriver::destroyRecognitionThread(void) {
	YerFace_MutexLock(myWrkMutex);
	recognizerRunning = false;
	SDL_CondSignal(myWrkCond);
	YerFace_MutexUnlock(myWrkMutex);

	SDL_WaitThread(recognizerThread, NULL);

	SDL_DestroyCond(myWrkCond);
}

void SphinxDriver::advanceWorkingToCompleted(void) {
	// YerFace_MutexLock(myWrkMutex);
	// YerFace_MutexLock(myCmpMutex);
	// FrameTimestamps frameTimestamps = frameDerivatives->getCompletedFrameTimestamps();
	// updateRecognizedPhonemes();
	// logger->verbose("==== FRAME FLIP %.3lf - %.3lf", frameTimestamps.startTimestamp, frameTimestamps.estimatedEndTimestamp);
	// // FIXME - do stuff
	// YerFace_MutexUnlock(myCmpMutex);
	// YerFace_MutexUnlock(myWrkMutex);
}

void SphinxDriver::processUtteranceHypothesis(void) {
	int frameRate = cmd_ln_int32_r(pocketSphinxConfig, "-frate");
	ps_seg_t *segmentIterator = ps_seg_iter(pocketSphinx);
	logger->verbose("======== HYPOTHESIS BREAKDOWN:");
	while(segmentIterator != NULL) {
		int32 startFrame, endFrame;
		double startTime, endTime;
		string symbol = ps_seg_word(segmentIterator);
		ps_seg_frames(segmentIterator, &startFrame, &endFrame);
		startTime = ((double)startFrame / (double)frameRate) + timestampOffset;
		endTime = ((double)endFrame / (double)frameRate) + timestampOffset;
		logger->verbose("    %s Times: %.3lf (%d) - %.3lf (%d), Utterance: %d\n", symbol.c_str(), startTime, startFrame, endTime, endFrame, utteranceIndex);
		segmentIterator = ps_seg_next(segmentIterator);
	}
}

int SphinxDriver::runRecognitionLoop(void *ptr) {
	SphinxDriver *self = (SphinxDriver *)ptr;
	self->logger->verbose("Speech Recognition Thread alive!");

	YerFace_MutexLock(self->myWrkMutex);
	while(self->recognizerRunning) {
		if(SDL_CondWait(self->myWrkCond, self->myWrkMutex) < 0) {
			throw runtime_error("Failed waiting on condition.");
		}
		if(self->recognizerRunning && self->audioFrameQueue.size() > 0) {
			char const *hypothesis;
			if(ps_process_raw(self->pocketSphinx, (int16 const *)self->audioFrameQueue.back()->buf, self->audioFrameQueue.back()->audioSamples, 0, 0) < 0) {
				throw runtime_error("Failed processing audio samples in PocketSphinx");
			}
			self->audioFrameQueue.back()->inUse = false;
			self->audioFrameQueue.pop_back();
			self->inSpeech = ps_get_in_speech(self->pocketSphinx);
			if(self->inSpeech && self->utteranceRestarted) {
				self->utteranceRestarted = false;
			}
			if(!self->inSpeech && !self->utteranceRestarted) {
				if(ps_end_utt(self->pocketSphinx) < 0) {
					throw runtime_error("Failed to end PocketSphinx utterance");
				}
				hypothesis = ps_get_hyp(self->pocketSphinx, NULL);
				self->logger->verbose("======== Utterance Ended. Hypothesis: %s", hypothesis);
				self->processUtteranceHypothesis();
				self->utteranceIndex++;
				if(ps_start_utt(self->pocketSphinx) < 0) {
					throw runtime_error("Failed to start PocketSphinx utterance");
				}
				self->utteranceRestarted = true;
			}
		}
	}

	YerFace_MutexUnlock(self->myWrkMutex);
	self->logger->verbose("Speech Recognition Thread quitting...");
	return 0;
}

SphinxAudioFrame *SphinxDriver::getNextAvailableAudioFrame(int desiredBufferSize) {
	YerFace_MutexLock(myWrkMutex);
	for(SphinxAudioFrame *audioFrame : audioFramesAllocated) {
		if(!audioFrame->inUse) {
			if(audioFrame->bufferSize < desiredBufferSize) {
				//Not using realloc because it does not support guaranteed buffer alignment.
				av_freep(&audioFrame->buf);
				if((audioFrame->buf = (uint8_t *)av_malloc(desiredBufferSize)) == NULL) {
					throw runtime_error("unable to allocate memory for audio frame");
				}
				audioFrame->bufferSize = desiredBufferSize;
			}
			audioFrame->pos = 0;
			audioFrame->inUse = true;
			YerFace_MutexUnlock(myWrkMutex);
			return audioFrame;
		}
	}
	SphinxAudioFrame *audioFrame = new SphinxAudioFrame();
	if((audioFrame->buf = (uint8_t *)av_malloc(desiredBufferSize)) == NULL) {
		throw runtime_error("unable to allocate memory for audio frame");
	}
	audioFrame->bufferSize = desiredBufferSize;
	audioFrame->pos = 0;
	audioFrame->inUse = true;
	YerFace_MutexUnlock(myWrkMutex);
	return audioFrame;
}

void SphinxDriver::FFmpegDriverAudioFrameCallback(void *userdata, uint8_t *buf, int audioSamples, int audioBytes, int bufferSize, double timestamp) {
	SphinxDriver *self = (SphinxDriver *)userdata;
	YerFace_MutexLock(self->myWrkMutex);
	if(!self->timestampOffsetSet) {
		self->timestampOffset = timestamp;
		self->timestampOffsetSet = true;
		self->logger->info("Received first audio frame. Set initial timestamp offset to %.04lf seconds.", self->timestampOffset);
	}
	SphinxAudioFrame *audioFrame = self->getNextAvailableAudioFrame(bufferSize);
	memcpy(audioFrame->buf, buf, audioBytes);
	audioFrame->audioSamples = audioSamples;
	audioFrame->audioBytes = audioBytes;
	audioFrame->timestamp = timestamp;
	self->audioFrameQueue.push_front(audioFrame);
	SDL_CondSignal(self->myWrkCond);
	YerFace_MutexUnlock(self->myWrkMutex);
}

} //namespace YerFace
