
#include "SphinxDriver.hpp"
#include "Utilities.hpp"

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

SphinxDriver::SphinxDriver(json config, FrameDerivatives *myFrameDerivatives, FFmpegDriver *myFFmpegDriver) {
	hiddenMarkovModel = config["YerFace"]["SphinxDriver"]["hiddenMarkovModel"];
	allPhoneLM = config["YerFace"]["SphinxDriver"]["allPhoneLM"];
	sphinxToPrestonBlairPhonemeMapping = config["YerFace"]["SphinxDriver"]["sphinxToPrestonBlairPhonemeMapping"];
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

void SphinxDriver::destroyRecognitionThread(void) {
	YerFace_MutexLock(myWrkMutex);
	recognizerRunning = false;
	SDL_CondSignal(myWrkCond);
	YerFace_MutexUnlock(myWrkMutex);

	SDL_WaitThread(recognizerThread, NULL);

	SDL_DestroyCond(myWrkCond);
}

void SphinxDriver::advanceWorkingToCompleted(void) {
	YerFace_MutexLock(myWrkMutex);
	// Add an (unprocessed) video frame (with some metadata) to the video frame buffer.
	SphinxVideoFrame *sphinxVideoFrame = new SphinxVideoFrame();
	sphinxVideoFrame->processed = false;
	sphinxVideoFrame->timestamps = frameDerivatives->getCompletedFrameTimestamps();
	if(videoFrames.size() > 0) {
		videoFrames.back()->realEndTimestamp = sphinxVideoFrame->timestamps.startTimestamp;
	}
	// logger->verbose("==== FRAME FLIP %ld: %.3lf -> ~%.3lf", sphinxVideoFrame->timestamps.frameNumber, sphinxVideoFrame->timestamps.startTimestamp, sphinxVideoFrame->timestamps.estimatedEndTimestamp);
	videoFrames.push_back(sphinxVideoFrame);

	// Handle any already-processed frames off the other side of the video frame buffer.
	bool bannerDropped = false;
	while(videoFrames.front()->processed) {
		if(!bannerDropped) {
			logger->verbose("======== PROCESSED FRAMES BANNER:::");
			bannerDropped = true;
		}
		std::stringstream jsonString;
		jsonString << videoFrames.front()->phonemes.percent.dump(-1, ' ', true);
		logger->verbose("  FRAME %ld: %s", videoFrames.front()->timestamps.frameNumber, jsonString.str().c_str());
		SphinxVideoFrame *old = videoFrames.front();
		videoFrames.pop_front();
		delete old;
	}
	YerFace_MutexUnlock(myWrkMutex);
}

void SphinxDriver::processUtteranceHypothesis(void) {
	int frameRate = cmd_ln_int32_r(pocketSphinxConfig, "-frate");
	ps_seg_t *segmentIterator = ps_seg_iter(pocketSphinx);
	list<SphinxVideoFrame *>::iterator videoFrameIterator = videoFrames.begin();
	logger->verbose("======== HYPOTHESIS BREAKDOWN:");
	bool frameReportStarted = false;
	while(segmentIterator != NULL) {
		if(videoFrameIterator == videoFrames.end()) {
			throw logic_error("We ran out of video frames trying to process recognized speech!");
		}

		bool iterate = true;
		int32 startFrame, endFrame;
		double startTime, endTime;
		ps_seg_frames(segmentIterator, &startFrame, &endFrame);
		startTime = ((double)startFrame / (double)frameRate) + timestampOffset;
		endTime = ((double)endFrame / (double)frameRate) + timestampOffset;

		if(
		  // Does the startTime land within the bounds of this frame?
		  (startTime >= (*videoFrameIterator)->timestamps.startTimestamp && startTime < (*videoFrameIterator)->realEndTimestamp) ||
		  // Does the endTime land within the bounds of this frame?
		  (endTime >= (*videoFrameIterator)->timestamps.startTimestamp && endTime < (*videoFrameIterator)->realEndTimestamp) ||
		  // Does this frame sit completely inside the start and end times of this segment?
		  (startTime < (*videoFrameIterator)->timestamps.startTimestamp && endTime > (*videoFrameIterator)->realEndTimestamp)) {
			string symbol = ps_seg_word(segmentIterator);
			if(!frameReportStarted) {
				logger->verbose("---- FRAME: %ld, %.3lf - %.3lf", (*videoFrameIterator)->timestamps.frameNumber, (*videoFrameIterator)->timestamps.startTimestamp, (*videoFrameIterator)->realEndTimestamp);
				frameReportStarted = true;
			}
			string pbPhoneme = "";
			try {
				pbPhoneme = sphinxToPrestonBlairPhonemeMapping.at(symbol);
			} catch(nlohmann::detail::out_of_range &e) {
				// logger->verbose("Sphinx reported a phoneme (%s) which we don't have in our mapping. Error was: %s", symbol.c_str(), e.what());
			}
			logger->verbose("  %s -> (%s) Times: %.3lf - %.3lf, Utterance: %d\n", symbol.c_str(), pbPhoneme.length() > 0 ? pbPhoneme.c_str() : "", startTime, endTime, utteranceIndex);
			if(pbPhoneme.length() > 0) {
				bool wasSeen = false;
				double currentPercent = 0.0;
				try {
					wasSeen = (*videoFrameIterator)->phonemes.seen.at(pbPhoneme);
					currentPercent = (*videoFrameIterator)->phonemes.percent.at(pbPhoneme);
				} catch(exception &e) {
					throw logic_error("Preston Blair mapping returned an unrecognized phoneme!");
				}
				if(!wasSeen) {
					wasSeen = true;
					(*videoFrameIterator)->phonemes.seen[pbPhoneme] = wasSeen;
				}
				double divisor = (*videoFrameIterator)->realEndTimestamp - (*videoFrameIterator)->timestamps.startTimestamp;
				if(startTime < (*videoFrameIterator)->timestamps.startTimestamp) {
					startTime = (*videoFrameIterator)->timestamps.startTimestamp;
				}
				if(endTime > (*videoFrameIterator)->realEndTimestamp) {
					endTime = (*videoFrameIterator)->realEndTimestamp;
				}
				double numerator = endTime - startTime;
				currentPercent = currentPercent + (numerator / divisor);
				logger->verbose("pbPhenome %s is %.04lf / %.04lf = %.04lf", pbPhoneme.c_str(), numerator, divisor, numerator / divisor);
				if(currentPercent > 1.0) {
					currentPercent = 1.0;
				}
				(*videoFrameIterator)->phonemes.percent[pbPhoneme] = currentPercent;
			}
		}
		if(startTime >= (*videoFrameIterator)->realEndTimestamp || endTime >= (*videoFrameIterator)->realEndTimestamp) {
			iterate = false;
			(*videoFrameIterator)->processed = true;
			++videoFrameIterator;
			frameReportStarted = false;
		}

		if(iterate) {
			segmentIterator = ps_seg_next(segmentIterator);
		}
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
