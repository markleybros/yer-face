
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

SphinxDriver::SphinxDriver(json config, Status *myStatus, FrameServer *myFrameServer, FFmpegDriver *myFFmpegDriver, SDLDriver *mySDLDriver, OutputDriver *myOutputDriver, PreviewHUD *myPreviewHUD, bool myLowLatency) {
	hiddenMarkovModel = config["YerFace"]["SphinxDriver"]["hiddenMarkovModel"];
	allPhoneLM = config["YerFace"]["SphinxDriver"]["allPhoneLM"];
	lipFlappingTargetPhoneme = config["YerFace"]["SphinxDriver"]["lipFlapping"]["targetPhoneme"];
	lipFlappingResponseThreshold = config["YerFace"]["SphinxDriver"]["lipFlapping"]["responseThreshold"];
	lipFlappingNonLinearResponse = config["YerFace"]["SphinxDriver"]["lipFlapping"]["nonLinearResponse"];
	lipFlappingNotInSpeechScale = config["YerFace"]["SphinxDriver"]["lipFlapping"]["notInSpeechScale"];
	sphinxToPrestonBlairPhonemeMapping = config["YerFace"]["SphinxDriver"]["sphinxToPrestonBlairPhonemeMapping"];
	vuMeterWidth = config["YerFace"]["SphinxDriver"]["PreviewHUD"]["vuMeterWidth"];
	vuMeterWarningThreshold = config["YerFace"]["SphinxDriver"]["PreviewHUD"]["vuMeterWarningThreshold"];
	vuMeterPeakHoldSeconds = config["YerFace"]["SphinxDriver"]["PreviewHUD"]["vuMeterPeakHoldSeconds"];
	status = myStatus;
	if(status == NULL) {
		throw invalid_argument("status cannot be NULL");
	}
	frameServer = myFrameServer;
	if(frameServer == NULL) {
		throw invalid_argument("frameServer cannot be NULL");
	}
	ffmpegDriver = myFFmpegDriver;
	if(ffmpegDriver == NULL) {
		throw invalid_argument("ffmpegDriver cannot be NULL");
	}
	sdlDriver = mySDLDriver;
	if(sdlDriver == NULL) {
		throw invalid_argument("sdlDriver cannot be NULL");
	}
	outputDriver = myOutputDriver;
	if(outputDriver == NULL) {
		throw invalid_argument("outputDriver cannot be NULL");
	}
	previewHUD = myPreviewHUD;
	if(previewHUD == NULL) {
		throw invalid_argument("previewHUD cannot be NULL");
	}
	lowLatency = myLowLatency;
	logger = new Logger("SphinxDriver");

	outputDriver->registerFrameData("phonemes");
	
	pocketSphinx = NULL;
	pocketSphinxConfig = NULL;
	timestampOffsetSet = false;
	utteranceRestarted = false;
	inSpeech = false;
	if((recognitionMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((lipFlappingMutex = SDL_CreateMutex()) == NULL) {
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
	audioFrameCallback.sampleRate = YERFACE_SPHINX_SAMPLERATE;
	audioFrameCallback.callback = FFmpegDriverAudioFrameCallback;
	ffmpegDriver->registerAudioFrameCallback(audioFrameCallback);

	FrameStatusChangeEventCallback frameStatusChangeCallback;
	frameStatusChangeCallback.userdata = (void *)this;
	frameStatusChangeCallback.callback = handleFrameStatusChange;
	frameStatusChangeCallback.newStatus = FRAME_STATUS_NEW;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_MAPPING;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_GONE;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);

	//We also want to introduce a checkpoint so that frames cannot TRANSITION AWAY from the relevant statuses without our blessing.
	frameServer->registerFrameStatusCheckpoint(FRAME_STATUS_MAPPING, "sphinxDriver.ran");

	WorkerPoolParameters workerPoolParameters;
	workerPoolParameters.name = "SphinxDriver.Recognition";
	workerPoolParameters.numWorkers = 1;
	workerPoolParameters.numWorkersPerCPU = 0.0;
	workerPoolParameters.initializer = NULL;
	workerPoolParameters.deinitializer = NULL;
	workerPoolParameters.usrPtr = (void *)this;
	workerPoolParameters.handler = recognitionWorkerHandler;
	recognitionWorkerPool = new WorkerPool(config, status, frameServer, workerPoolParameters);

	workerPoolParameters.name = "SphinxDriver.LipFlapping";
	workerPoolParameters.numWorkers = 1;
	workerPoolParameters.numWorkersPerCPU = 0.0;
	workerPoolParameters.initializer = NULL;
	workerPoolParameters.deinitializer = NULL;
	workerPoolParameters.usrPtr = (void *)this;
	workerPoolParameters.handler = lipFlappingWorkerHandler;
	lipFlappingWorkerPool = new WorkerPool(config, status, frameServer, workerPoolParameters);

	logger->debug("SphinxDriver object constructed in %s mode!", lowLatency ? "LOW LATENCY (lip flapping)" : "OFFLINE (preston blair phoneme breakdown)");
}

SphinxDriver::~SphinxDriver() noexcept(false) {
	logger->debug("SphinxDriver object destructing...");

	delete recognitionWorkerPool;

	YerFace_MutexLock(recognitionMutex);
	if(audioFrameQueue.size() > 0) {
		logger->error("Input audio frames are still pending! Woe is me!");
	}
	YerFace_MutexUnlock(recognitionMutex);

	delete lipFlappingWorkerPool;

	YerFace_MutexLock(lipFlappingMutex);
	if(pendingLipFlappingFrames.size() > 0) {
		logger->error("Lip Flapping frames are still pending! Woe is me!");
	}
	YerFace_MutexUnlock(lipFlappingMutex);

	YerFace_MutexLock(recognitionMutex);
	if(recognitionResults.size() > 0) {
		logger->error("Not all recognition results were consumed! Woe is me!");
	}
	YerFace_MutexUnlock(recognitionMutex);

	SDL_DestroyMutex(recognitionMutex);
	recognitionMutex = NULL;
	SDL_DestroyMutex(lipFlappingMutex);
	lipFlappingMutex = NULL;

	ps_free(pocketSphinx);
	cmd_ln_free_r(pocketSphinxConfig);

	delete logger;
}

// void SphinxDriver::advanceWorkingToCompleted(void) {
// 	YerFace_MutexLock(myWrkMutex);

// 	if(lowLatency) {
// 		//Sometimes an entire video frame goes by without us seeing any new audio frames.
// 		if(!working.framesIncluded) {
// 			//Cheat by using last frame's results for lip flapping.
// 			working = completed;
// 		}
// 		processLipFlappingAudio();
// 		outputDriver->insertCompletedFrameData("phonemes", working.lipFlapping.percent);
// 	} else {
// 		// Add an (unprocessed) video frame (with some metadata) to the video frame buffer.
// 		SphinxVideoFrame *sphinxVideoFrame = new SphinxVideoFrame();
// 		sphinxVideoFrame->processed = false;
// 		sphinxVideoFrame->timestamps = frameServer->getCompletedFrameTimestamps();
// 		sphinxVideoFrame->realEndTimestamp = sphinxVideoFrame->timestamps.estimatedEndTimestamp;
// 		if(videoFrames.size() > 0) {
// 			videoFrames.back()->realEndTimestamp = sphinxVideoFrame->timestamps.startTimestamp;
// 		}
// 		videoFrames.push_back(sphinxVideoFrame);

// 		processPhonemesIntoVideoFrames(false);
// 		handleProcessedVideoFrames();
// 	}

// 	YerFace_MutexLock(myCmpMutex);
// 	SphinxWorkingVariables empty;
// 	completed = working;
// 	working = empty;
// 	YerFace_MutexUnlock(myCmpMutex);

// 	YerFace_MutexUnlock(myWrkMutex);
// }

void SphinxDriver::renderPreviewHUD(Mat frame, FrameNumber frameNumber, int density) {

	static double vuMeterLastSetPeak = vuMeterPeakHoldSeconds * (-1.0);

	if(density > 0) {
		YerFace_MutexLock(lipFlappingMutex);
		double maxAmplitude = pendingLipFlappingFrames[frameNumber]->maxAmplitude;
		bool peak = pendingLipFlappingFrames[frameNumber]->peak;
		YerFace_MutexUnlock(lipFlappingMutex);
		if(peak) {
			logger->verbose("PEAK!!!");
		}

		Rect2d previewRect;
		Point2d previewCenter;
		previewHUD->createPreviewHUDRectangle(frame.size(), &previewRect, &previewCenter);

		double vuHeight = previewRect.height * maxAmplitude;
		Rect2d vuMeter;
		vuMeter.x = previewRect.x;
		vuMeter.y = previewRect.y + (previewRect.height - vuHeight);
		vuMeter.width = vuMeterWidth;
		vuMeter.height = vuHeight;

		Scalar color = Scalar(0, 255, 0);
		if(maxAmplitude >= vuMeterWarningThreshold) {
			color = Scalar(0, 165, 255);
		}
		rectangle(frame, vuMeter, color, FILLED);

		if(peak) {
			vuMeterLastSetPeak = (double)SDL_GetTicks() / (double)1000.0;
		}
		double now = (double)SDL_GetTicks() / (double)1000.0;
		if(now <= vuMeterLastSetPeak + vuMeterPeakHoldSeconds) {
			Rect2d peakIndicator = Rect2d(previewRect.x, previewRect.y, vuMeterWidth, vuMeterWidth);
			rectangle(frame, peakIndicator, Scalar(0, 0, 255), FILLED);
		}
	}
}

// void SphinxDriver::processPhonemesIntoVideoFrames(bool draining) {
// 	list<SphinxVideoFrame *>::iterator videoFrameIterator = videoFrames.begin();
// 	list<SphinxPhoneme>::iterator phonemeIterator = phonemeBuffer.begin();
// 	while(phonemeIterator != phonemeBuffer.end()) {
// 		if(videoFrameIterator == videoFrames.end()) {
// 			if(draining) {
// 				logger->warn("We ran out of video frames trying to processPhonemesIntoVideoFrames()!");
// 				return;
// 			}
// 			throw logic_error("We ran out of video frames trying to processPhonemesIntoVideoFrames()!");
// 		}
// 		bool iteratePhoneme = true;
// 		SphinxVideoFrame *videoFrame = *videoFrameIterator;
// 		SphinxPhoneme phoneme = *phonemeIterator;

// 		if(
// 		  // Does the startTime land within the bounds of this frame?
// 		  (phoneme.startTime >= videoFrame->timestamps.startTimestamp && phoneme.startTime < videoFrame->realEndTimestamp) ||
// 		  // Does the endTime land within the bounds of this frame?
// 		  (phoneme.endTime >= videoFrame->timestamps.startTimestamp && phoneme.endTime < videoFrame->realEndTimestamp) ||
// 		  // Does this frame sit completely inside the start and end times of this segment?
// 		  (phoneme.startTime < videoFrame->timestamps.startTimestamp && phoneme.endTime > videoFrame->realEndTimestamp)) {
// 			if(phoneme.pbPhoneme.length() > 0) {
// 				bool wasSeen = false;
// 				double currentPercent = 0.0;
// 				try {
// 					wasSeen = videoFrame->phonemes.seen.at(phoneme.pbPhoneme);
// 					currentPercent = videoFrame->phonemes.percent.at(phoneme.pbPhoneme);
// 				} catch(exception &e) {
// 					throw logic_error("Preston Blair mapping returned an unrecognized phoneme!");
// 				}
// 				if(!wasSeen) {
// 					wasSeen = true;
// 					videoFrame->phonemes.seen[phoneme.pbPhoneme] = wasSeen;
// 				}
// 				double divisor = videoFrame->realEndTimestamp - videoFrame->timestamps.startTimestamp;
// 				double startTime = phoneme.startTime;
// 				double endTime = phoneme.endTime;
// 				if(startTime < videoFrame->timestamps.startTimestamp) {
// 					startTime = videoFrame->timestamps.startTimestamp;
// 				}
// 				if(endTime > videoFrame->realEndTimestamp) {
// 					endTime = videoFrame->realEndTimestamp;
// 				}
// 				double numerator = endTime - startTime;
// 				currentPercent = currentPercent + (numerator / divisor);
// 				// logger->verbose("pbPhenome %s is %.04lf / %.04lf = %.04lf", pbPhoneme.c_str(), numerator, divisor, numerator / divisor);
// 				if(currentPercent > 1.0) {
// 					currentPercent = 1.0;
// 				}
// 				videoFrame->phonemes.percent[phoneme.pbPhoneme] = currentPercent;
// 			}
// 		}
// 		if(phoneme.startTime >= (*videoFrameIterator)->realEndTimestamp || phoneme.endTime >= (*videoFrameIterator)->realEndTimestamp) {
// 			iteratePhoneme = false;
// 			videoFrame->processed = true;
// 			++videoFrameIterator;
// 		}
// 		if(iteratePhoneme) {
// 			++phonemeIterator;
// 		}
// 	}
// }

// void SphinxDriver::handleProcessedVideoFrames(void) {
// 	// Handle any already-processed frames off the other side of the video frame buffer.
// 	while(videoFrames.size() > 0 && videoFrames.front()->processed) {
// 		std::stringstream jsonString;
// 		jsonString << videoFrames.front()->phonemes.percent.dump(-1, ' ', true);
// 		// logger->verbose("  FRAME " YERFACE_FRAMENUMBER_FORMAT ": %s", videoFrames.front()->timestamps.frameNumber, jsonString.str().c_str());
// 		outputDriver->updateLateFrameData(videoFrames.front()->timestamps.frameNumber, "phonemes", videoFrames.front()->phonemes.percent);
// 		SphinxVideoFrame *old = videoFrames.front();
// 		videoFrames.pop_front();
// 		delete old;
// 	}
// }

void SphinxDriver::processUtteranceHypothesis(SphinxRecognizerResult *result) {
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
			result->phonemeBuffer.push_back(phoneme);
		} catch(nlohmann::detail::out_of_range &e) {
			// logger->verbose("Sphinx reported a phoneme (%s) which we don't have in our mapping. Error was: %s", symbol.c_str(), e.what());
		}

		segmentIterator = ps_seg_next(segmentIterator);
	}
}

void SphinxDriver::processAudioAmplitude(SphinxAudioFrame *audioFrame, SphinxRecognizerResult *result) {
	PocketSphinx::int16 const *buf = (PocketSphinx::int16 const *)audioFrame->buf;
	int samples = audioFrame->audioSamples;
	for(int i = 0; i < samples; i++) {
		double amplitude = fabs((double)buf[i]) / (double)0x7FFF;
		if(amplitude > result->maxAmplitude) {
			result->maxAmplitude = amplitude;
		}
	}
	if(result->maxAmplitude >= 1.0) {
		result->maxAmplitude = 1.0;
		result->peak = true;
	}
}

void SphinxDriver::processLipFlappingAudio(SphinxVideoFrame *videoFrame) {
	static double lastMaxAmplitude = 0.0;
	static bool lastInSpeech = false;
	static bool lastPeak = false;
	double maxAmplitude = 0.0;
	bool inSpeech = false;
	bool peak = false;
	bool sampledAtLeastOneAudioFrame = false;
	YerFace_MutexLock(recognitionMutex);
	while(recognitionResults.size() && recognitionResults.back().endTimestamp <= videoFrame->timestamps.startTimestamp) {
		recognitionResults.pop_back();
	}
	while(recognitionResults.size() && ( \
		recognitionResults.back().startTimestamp >= videoFrame->timestamps.startTimestamp || \
		recognitionResults.back().endTimestamp >= videoFrame->timestamps.startTimestamp \
	  ) && ( \
		recognitionResults.back().startTimestamp < videoFrame->timestamps.estimatedEndTimestamp || \
		recognitionResults.back().endTimestamp < videoFrame->timestamps.estimatedEndTimestamp \
	  )) {
		if(recognitionResults.back().maxAmplitude >= maxAmplitude) {
			maxAmplitude = recognitionResults.back().maxAmplitude;
		}
		if(recognitionResults.back().inSpeech) {
			inSpeech = true;
		}
		if(recognitionResults.back().peak) {
			peak = true;
		}
		sampledAtLeastOneAudioFrame = true;
		recognitionResults.pop_back();
	  }
	YerFace_MutexUnlock(recognitionMutex);

	if(!sampledAtLeastOneAudioFrame) {
		maxAmplitude = lastMaxAmplitude;
		inSpeech = lastInSpeech;
		peak = lastPeak;
	} else {
		lastMaxAmplitude = maxAmplitude;
		lastInSpeech = inSpeech;
		lastPeak = peak;
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
	if(lipFlappingAmount > (double)videoFrame->phonemes.percent[lipFlappingTargetPhoneme]) {
		videoFrame->phonemes.percent[lipFlappingTargetPhoneme] = lipFlappingAmount;
	}
	videoFrame->maxAmplitude = maxAmplitude;
	videoFrame->peak = peak;
}

SphinxAudioFrame *SphinxDriver::getNextAvailableAudioFrame(int desiredBufferSize) {
	YerFace_MutexLock(recognitionMutex);
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
			YerFace_MutexUnlock(recognitionMutex);
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
	YerFace_MutexUnlock(recognitionMutex);
	return audioFrame;
}

void SphinxDriver::FFmpegDriverAudioFrameCallback(void *userdata, uint8_t *buf, int audioSamples, int audioBytes, double timestamp) {
	SphinxDriver *self = (SphinxDriver *)userdata;
	if(self->recognitionMutex == NULL) {
		return;
	}
	YerFace_MutexLock(self->recognitionMutex);
	if(!self->timestampOffsetSet) {
		self->timestampOffset = timestamp;
		self->timestampOffsetSet = true;
		self->logger->info("Received first audio frame. Set initial timestamp offset to %.04lf seconds.", self->timestampOffset);
	}
	SphinxAudioFrame *audioFrame = self->getNextAvailableAudioFrame(audioBytes);
	memcpy(audioFrame->buf, buf, audioBytes);
	audioFrame->audioSamples = audioSamples;
	audioFrame->audioBytes = audioBytes;
	audioFrame->timestamp = timestamp;
	self->audioFrameQueue.push_front(audioFrame);
	YerFace_MutexUnlock(self->recognitionMutex);
	self->recognitionWorkerPool->sendWorkerSignal();
}

void SphinxDriver::handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps) {
	SphinxDriver *self = (SphinxDriver *)userdata;
	FrameNumber frameNumber = frameTimestamps.frameNumber;
	SphinxVideoFrame *videoFrame = NULL;
	switch(newStatus) {
		default:
			throw logic_error("Handler passed unsupported frame status change event!");
		case FRAME_STATUS_NEW:
			videoFrame = new SphinxVideoFrame();
			videoFrame->isLipFlappingReady = false;
			videoFrame->isLipFlappingProcessed = false;
			videoFrame->timestamps = frameTimestamps;
			// videoFrame->realEndTimestamp = frameTimestamps.estimatedEndTimestamp;
			YerFace_MutexLock(self->lipFlappingMutex);
			self->pendingLipFlappingFrames[frameNumber] = videoFrame;
			YerFace_MutexUnlock(self->lipFlappingMutex);
			break;
		case FRAME_STATUS_MAPPING:
			YerFace_MutexLock(self->lipFlappingMutex);
			self->logger->verbose("handleFrameStatusChange() Frame #" YERFACE_FRAMENUMBER_FORMAT " waiting on me. Queue depth is now %lu", frameNumber, self->pendingLipFlappingFrames.size());
			self->pendingLipFlappingFrames[frameNumber]->isLipFlappingReady = true;
			YerFace_MutexUnlock(self->lipFlappingMutex);
			self->lipFlappingWorkerPool->sendWorkerSignal();
			break;
		case FRAME_STATUS_GONE:
			YerFace_MutexLock(self->lipFlappingMutex);
			videoFrame = self->pendingLipFlappingFrames[frameNumber];
			if(!videoFrame->isLipFlappingProcessed) {
				throw logic_error("Frame is gone, but not yet processed!");
			}
			delete videoFrame;
			self->pendingLipFlappingFrames.erase(frameNumber);
			YerFace_MutexUnlock(self->lipFlappingMutex);
			break;
	}
}

bool SphinxDriver::recognitionWorkerHandler(WorkerPoolWorker *worker) {
	SphinxDriver *self = (SphinxDriver *)worker->ptr;

	bool didWork = false;

	YerFace_MutexLock(self->recognitionMutex);
	if(self->audioFrameQueue.size() > 0) {
		if(ps_process_raw(self->pocketSphinx, (int16 const *)self->audioFrameQueue.back()->buf, self->audioFrameQueue.back()->audioSamples, 0, 0) < 0) {
			throw runtime_error("Failed processing audio samples in PocketSphinx");
		}
		self->inSpeech = ps_get_in_speech(self->pocketSphinx);
		SphinxRecognizerResult result;
		result.inSpeech = self->inSpeech;
		result.peak = false;
		result.startTimestamp = self->audioFrameQueue.back()->timestamp;
		result.endTimestamp = result.startTimestamp + ((double)self->audioFrameQueue.back()->audioSamples / (double)YERFACE_SPHINX_SAMPLERATE);
		self->processAudioAmplitude(self->audioFrameQueue.back(), &result);
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
				self->processUtteranceHypothesis(&result);
			}
			self->utteranceIndex++;
			if(ps_start_utt(self->pocketSphinx) < 0) {
				throw runtime_error("Failed to start PocketSphinx utterance");
			}
			self->utteranceRestarted = true;
		}

		self->recognitionResults.push_front(result);
		self->lipFlappingWorkerPool->sendWorkerSignal();

		didWork = true;
	}
	YerFace_MutexUnlock(self->recognitionMutex);

	return didWork;
}

bool SphinxDriver::lipFlappingWorkerHandler(WorkerPoolWorker *worker) {
	SphinxDriver *self = (SphinxDriver *)worker->ptr;

	bool didWork = false;
	static FrameNumber lastFrameNumber = -1;
	FrameNumber myFrameNumber = -1;
	SphinxVideoFrame *videoFrame = NULL;

	YerFace_MutexLock(self->lipFlappingMutex);
	//// CHECK FOR WORK ////
	for(auto pendingFramePair : self->pendingLipFlappingFrames) {
		if(myFrameNumber < 0 || pendingFramePair.first < myFrameNumber) {
			if(!self->pendingLipFlappingFrames[pendingFramePair.first]->isLipFlappingProcessed) {
				myFrameNumber = pendingFramePair.first;
			}
		}
	}
	if(myFrameNumber > 0) {
		videoFrame = self->pendingLipFlappingFrames[myFrameNumber];
	}
	if(videoFrame != NULL) {
		if(!videoFrame->isLipFlappingReady) {
			self->logger->verbose("BLOCKED on frame " YERFACE_FRAMENUMBER_FORMAT " because it is not ready!", myFrameNumber);
			myFrameNumber = -1;
			videoFrame = NULL;
		}
	}
	YerFace_MutexUnlock(self->lipFlappingMutex);

	//// DO THE WORK ////
	if(videoFrame != NULL) {
		self->logger->verbose("Lip Flapping Worker Thread handling frame #" YERFACE_FRAMENUMBER_FORMAT, videoFrame->timestamps.frameNumber);

		if(videoFrame->timestamps.frameNumber <= lastFrameNumber) {
			throw logic_error("SphinxDriver handling frames out of order!");
		}
		lastFrameNumber = videoFrame->timestamps.frameNumber;

		self->processLipFlappingAudio(videoFrame);
		if(videoFrame->phonemes.percent[self->lipFlappingTargetPhoneme] > 0.0) {
			string jsonString = videoFrame->phonemes.percent.dump(-1, ' ', true);
			self->logger->verbose("  FRAME " YERFACE_FRAMENUMBER_FORMAT ": %s", videoFrame->timestamps.frameNumber, jsonString.c_str());
		}
		// FIXME - don't insert frame data if we're not low latency
		self->logger->verbose("INSERTING FRAME DATA FOR " YERFACE_FRAMENUMBER_FORMAT, videoFrame->timestamps.frameNumber);
		self->outputDriver->insertFrameData("phonemes", videoFrame->phonemes.percent, videoFrame->timestamps.frameNumber);

		YerFace_MutexLock(self->lipFlappingMutex);
		videoFrame->isLipFlappingProcessed = true;
		YerFace_MutexUnlock(self->lipFlappingMutex);

		self->frameServer->setWorkingFrameStatusCheckpoint(videoFrame->timestamps.frameNumber, FRAME_STATUS_MAPPING, "sphinxDriver.ran");

		didWork = true;
	}
	return didWork;
}

} //namespace YerFace
