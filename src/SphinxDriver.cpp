
#include "SphinxDriver.hpp"
#include "Utilities.hpp"

#include <cmath>

using namespace std;
using namespace PocketSphinx;

namespace YerFace {

PrestonBlairPhonemes::PrestonBlairPhonemes(void) {
	seen = {
		{ "AI", false },
		{ "E", false },
		{ "O", false },
		{ "U", false },
		{ "MBP", false },
		{ "FV", false },
		{ "L", false },
		{ "WQ", false },
		{ "etc", false }
	};
	percent = {
		{ "AI", 0.0 },
		{ "E", 0.0 },
		{ "O", 0.0 },
		{ "U", 0.0 },
		{ "MBP", 0.0 },
		{ "FV", 0.0 },
		{ "L", 0.0 },
		{ "WQ", 0.0 },
		{ "etc", 0.0 }
	};
}

SphinxDriver::SphinxDriver(json config, FrameDerivatives *myFrameDerivatives, FFmpegDriver *myFFmpegDriver, OutputDriver *myOutputDriver, bool myLowLatency) {
	hiddenMarkovModel = config["YerFace"]["SphinxDriver"]["hiddenMarkovModel"];
	allPhoneLM = config["YerFace"]["SphinxDriver"]["allPhoneLM"];
	lipFlappingTargetPhoneme = config["YerFace"]["SphinxDriver"]["lipFlapping"]["targetPhoneme"];
	lipFlappingResponseThreshold = config["YerFace"]["SphinxDriver"]["lipFlapping"]["responseThreshold"];
	lipFlappingNonLinearResponse = config["YerFace"]["SphinxDriver"]["lipFlapping"]["nonLinearResponse"];
	lipFlappingNotInSpeechScale = config["YerFace"]["SphinxDriver"]["lipFlapping"]["notInSpeechScale"];
	sphinxToPrestonBlairPhonemeMapping = config["YerFace"]["SphinxDriver"]["sphinxToPrestonBlairPhonemeMapping"];
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	ffmpegDriver = myFFmpegDriver;
	if(ffmpegDriver == NULL) {
		throw invalid_argument("ffmpegDriver cannot be NULL");
	}
	outputDriver = myOutputDriver;
	if(outputDriver == NULL) {
		throw invalid_argument("outputDriver cannot be NULL");
	}
	lowLatency = myLowLatency;
	if(!lowLatency) {
		outputDriver->registerLateFrameData("phonemes");
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

	logger->debug("SphinxDriver object constructed in %s mode!", lowLatency ? "realtime lip flapping" : "offline phoneme breakdown");
}

SphinxDriver::~SphinxDriver() noexcept(false) {
	logger->debug("SphinxDriver object destructing...");
	drainPipelineDataNow();
	ps_free(pocketSphinx);
	cmd_ln_free_r(pocketSphinxConfig);
	for(SphinxVideoFrame *frame : videoFrames) {
		delete frame;
	}
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

void SphinxDriver::advanceWorkingToCompleted(void) {
	YerFace_MutexLock(myWrkMutex);

	if(lowLatency) {
		PrestonBlairPhonemes empty;
		completedLipFlapping = workingLipFlapping;
		workingLipFlapping = empty;

		outputDriver->insertCompletedFrameData("phonemes", completedLipFlapping.percent);
	} else {
		// Add an (unprocessed) video frame (with some metadata) to the video frame buffer.
		SphinxVideoFrame *sphinxVideoFrame = new SphinxVideoFrame();
		sphinxVideoFrame->processed = false;
		sphinxVideoFrame->timestamps = frameDerivatives->getCompletedFrameTimestamps();
		sphinxVideoFrame->realEndTimestamp = sphinxVideoFrame->timestamps.estimatedEndTimestamp;
		if(videoFrames.size() > 0) {
			videoFrames.back()->realEndTimestamp = sphinxVideoFrame->timestamps.startTimestamp;
		}
		videoFrames.push_back(sphinxVideoFrame);

		processPhonemesIntoVideoFrames(false);
		handleProcessedVideoFrames();
	}

	YerFace_MutexUnlock(myWrkMutex);
}

void SphinxDriver::drainPipelineDataNow(void) {
	if(recognizerThread == NULL) {
		return;
	}
	YerFace_MutexLock(myWrkMutex);
	recognizerRunning = false;
	SDL_CondSignal(myWrkCond);
	YerFace_MutexUnlock(myWrkMutex);
	SDL_WaitThread(recognizerThread, NULL);
	recognizerThread = NULL;

	SDL_DestroyMutex(myWrkMutex);
	SDL_DestroyCond(myWrkCond);
	myWrkMutex = NULL;
	myWrkCond = NULL;
}

void SphinxDriver::processPhonemesIntoVideoFrames(bool draining) {
	list<SphinxVideoFrame *>::iterator videoFrameIterator = videoFrames.begin();
	list<SphinxPhoneme>::iterator phonemeIterator = phonemeBuffer.begin();
	while(phonemeIterator != phonemeBuffer.end()) {
		if(videoFrameIterator == videoFrames.end()) {
			if(draining) {
				logger->warn("We ran out of video frames trying to processPhonemesIntoVideoFrames()!");
				return;
			}
			throw logic_error("We ran out of video frames trying to processPhonemesIntoVideoFrames()!");
		}
		bool iteratePhoneme = true;
		SphinxVideoFrame *videoFrame = *videoFrameIterator;
		SphinxPhoneme phoneme = *phonemeIterator;

		if(
		  // Does the startTime land within the bounds of this frame?
		  (phoneme.startTime >= videoFrame->timestamps.startTimestamp && phoneme.startTime < videoFrame->realEndTimestamp) ||
		  // Does the endTime land within the bounds of this frame?
		  (phoneme.endTime >= videoFrame->timestamps.startTimestamp && phoneme.endTime < videoFrame->realEndTimestamp) ||
		  // Does this frame sit completely inside the start and end times of this segment?
		  (phoneme.startTime < videoFrame->timestamps.startTimestamp && phoneme.endTime > videoFrame->realEndTimestamp)) {
			if(phoneme.pbPhoneme.length() > 0) {
				bool wasSeen = false;
				double currentPercent = 0.0;
				try {
					wasSeen = videoFrame->phonemes.seen.at(phoneme.pbPhoneme);
					currentPercent = videoFrame->phonemes.percent.at(phoneme.pbPhoneme);
				} catch(exception &e) {
					throw logic_error("Preston Blair mapping returned an unrecognized phoneme!");
				}
				if(!wasSeen) {
					wasSeen = true;
					videoFrame->phonemes.seen[phoneme.pbPhoneme] = wasSeen;
				}
				double divisor = videoFrame->realEndTimestamp - videoFrame->timestamps.startTimestamp;
				double startTime = phoneme.startTime;
				double endTime = phoneme.endTime;
				if(startTime < videoFrame->timestamps.startTimestamp) {
					startTime = videoFrame->timestamps.startTimestamp;
				}
				if(endTime > videoFrame->realEndTimestamp) {
					endTime = videoFrame->realEndTimestamp;
				}
				double numerator = endTime - startTime;
				currentPercent = currentPercent + (numerator / divisor);
				// logger->verbose("pbPhenome %s is %.04lf / %.04lf = %.04lf", pbPhoneme.c_str(), numerator, divisor, numerator / divisor);
				if(currentPercent > 1.0) {
					currentPercent = 1.0;
				}
				videoFrame->phonemes.percent[phoneme.pbPhoneme] = currentPercent;
			}
		}
		if(phoneme.startTime >= (*videoFrameIterator)->realEndTimestamp || phoneme.endTime >= (*videoFrameIterator)->realEndTimestamp) {
			iteratePhoneme = false;
			videoFrame->processed = true;
			++videoFrameIterator;
		}
		if(iteratePhoneme) {
			++phonemeIterator;
		}
	}
}

void SphinxDriver::handleProcessedVideoFrames(void) {
	// Handle any already-processed frames off the other side of the video frame buffer.
	while(videoFrames.size() > 0 && videoFrames.front()->processed) {
		std::stringstream jsonString;
		jsonString << videoFrames.front()->phonemes.percent.dump(-1, ' ', true);
		// logger->verbose("  FRAME %ld: %s", videoFrames.front()->timestamps.frameNumber, jsonString.str().c_str());
		outputDriver->updateLateFrameData(videoFrames.front()->timestamps.frameNumber, "phonemes", videoFrames.front()->phonemes.percent);
		SphinxVideoFrame *old = videoFrames.front();
		videoFrames.pop_front();
		delete old;
	}
}

void SphinxDriver::processUtteranceHypothesis(void) {
	int frameRate = cmd_ln_int32_r(pocketSphinxConfig, "-frate");
	ps_seg_t *segmentIterator = ps_seg_iter(pocketSphinx);
	while(segmentIterator != NULL) {
		int32 startFrame, endFrame;
		ps_seg_frames(segmentIterator, &startFrame, &endFrame);
		string symbol = ps_seg_word(segmentIterator);

		SphinxPhoneme phoneme;
		phoneme.utteranceIndex = utteranceIndex;
		phoneme.startTime = ((double)startFrame / (double)frameRate) + timestampOffset;
		phoneme.endTime = ((double)endFrame / (double)frameRate) + timestampOffset;
		phoneme.pbPhoneme = "";
		try {
			phoneme.pbPhoneme = sphinxToPrestonBlairPhonemeMapping.at(symbol);
			phonemeBuffer.push_back(phoneme);
		} catch(nlohmann::detail::out_of_range &e) {
			// logger->verbose("Sphinx reported a phoneme (%s) which we don't have in our mapping. Error was: %s", symbol.c_str(), e.what());
		}

		segmentIterator = ps_seg_next(segmentIterator);
	}
}

void SphinxDriver::processLipFlappingAudio(PocketSphinx::int16 const *buf, int samples) {
	double maxAmplitude = 0.0;
	for(int i = 0; i < samples; i++) {
		double amplitude = fabs((double)buf[i]) / (double)0x7FFF;
		if(amplitude > maxAmplitude) {
			maxAmplitude = amplitude;
		}
	}
	if(maxAmplitude > 1.0) {
		maxAmplitude = 1.0;
	}
	double lipFlappingAmount = 0.0;
	double normalized = 0.0;
	if(maxAmplitude >= lipFlappingResponseThreshold) {
		normalized = Utilities::normalize(maxAmplitude - lipFlappingResponseThreshold, 1.0 - lipFlappingResponseThreshold);
		double temp = pow(normalized, lipFlappingNonLinearResponse);
		if(!inSpeech) {
			temp = temp * lipFlappingNotInSpeechScale;
		}
		lipFlappingAmount = temp;
	}
	if(lipFlappingAmount > (double)workingLipFlapping.percent[lipFlappingTargetPhoneme]) {
		workingLipFlapping.percent[lipFlappingTargetPhoneme] = lipFlappingAmount;
	}
	// logger->verbose("Lip Flapping... Samples: %d; maxAmplitude: %lf, normalized: %lf, lipFlappingAmount: %lf, output: %lf", samples, maxAmplitude, normalized, lipFlappingAmount, (double)workingLipFlapping.percent[lipFlappingTargetPhoneme]);
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
			if(ps_process_raw(self->pocketSphinx, (int16 const *)self->audioFrameQueue.back()->buf, self->audioFrameQueue.back()->audioSamples, 0, 0) < 0) {
				throw runtime_error("Failed processing audio samples in PocketSphinx");
			}
			self->inSpeech = ps_get_in_speech(self->pocketSphinx);
			if(self->lowLatency) {
				self->processLipFlappingAudio((int16 const *)self->audioFrameQueue.back()->buf, self->audioFrameQueue.back()->audioSamples);
			}
			self->audioFrameQueue.back()->inUse = false;
			self->audioFrameQueue.pop_back();

			if(self->inSpeech && self->utteranceRestarted) {
				self->utteranceRestarted = false;
			}
			if(!self->inSpeech && !self->utteranceRestarted) {
				if(ps_end_utt(self->pocketSphinx) < 0) {
					throw runtime_error("Failed to end PocketSphinx utterance");
				}
				if(!self->lowLatency) {
					self->processUtteranceHypothesis();
				}
				self->utteranceIndex++;
				if(ps_start_utt(self->pocketSphinx) < 0) {
					throw runtime_error("Failed to start PocketSphinx utterance");
				}
				self->utteranceRestarted = true;
			}
		}
	}
	self->logger->verbose("Recognition loop ended. Draining speech recognizer...");
	if(ps_end_utt(self->pocketSphinx) < 0) {
		throw runtime_error("Failed to end PocketSphinx utterance");
	}
	if(!self->lowLatency) {
		self->processUtteranceHypothesis();
		for(SphinxVideoFrame *frame : self->videoFrames) {
			frame->processed = true;
		}
		self->processPhonemesIntoVideoFrames(true);
		self->handleProcessedVideoFrames();
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
