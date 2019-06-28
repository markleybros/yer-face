
#include "SphinxDriver.hpp"
#include "Utilities.hpp"

#include <cmath>

using namespace std;
using namespace cv;
using namespace PocketSphinx;

namespace YerFace {

PrestonBlairPhonemes::PrestonBlairPhonemes(void) {
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
	hiddenMarkovModel = Utilities::fileValidPathOrDie(config["YerFace"]["SphinxDriver"]["hiddenMarkovModel"]);
	allPhoneLM = Utilities::fileValidPathOrDie(config["YerFace"]["SphinxDriver"]["allPhoneLM"]);
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
	if((workingVideoFramesMutex = SDL_CreateMutex()) == NULL) {
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
	audioFrameCallback.audioFrameCallback = FFmpegDriverAudioFrameCallback;
	audioFrameCallback.isDrainedCallback = FFmpegDriverAudioIsDrainedCallback;
	ffmpegDriver->registerAudioFrameCallback(audioFrameCallback);

	FrameStatusChangeEventCallback frameStatusChangeCallback;
	frameStatusChangeCallback.userdata = (void *)this;
	frameStatusChangeCallback.callback = handleFrameStatusChange;
	frameStatusChangeCallback.newStatus = FRAME_STATUS_NEW;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	frameStatusChangeCallback.newStatus = FRAME_STATUS_MAPPING;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	if(!lowLatency) {
		frameStatusChangeCallback.newStatus = FRAME_STATUS_LATE_PROCESSING;
		frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	}
	frameStatusChangeCallback.newStatus = FRAME_STATUS_GONE;
	frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);

	recognizerRunning = true;
	recognizerDrained = false;
	WorkerPoolParameters workerPoolParameters;
	workerPoolParameters.name = "SphinxDriver.Recognition";
	workerPoolParameters.numWorkers = 1;
	workerPoolParameters.numWorkersPerCPU = 0.0;
	workerPoolParameters.initializer = NULL;
	workerPoolParameters.deinitializer = recognitionWorkerDeinitializer;
	workerPoolParameters.usrPtr = (void *)this;
	workerPoolParameters.handler = recognitionWorkerHandler;
	recognitionWorkerPool = new WorkerPool(config, status, frameServer, workerPoolParameters);

	//We also want to introduce a checkpoint so that frames cannot TRANSITION AWAY from the relevant statuses without our blessing.
	frameServer->registerFrameStatusCheckpoint(FRAME_STATUS_MAPPING, "sphinxDriver.ran");

	workerPoolParameters.name = "SphinxDriver.LipFlapping";
	workerPoolParameters.numWorkers = 1;
	workerPoolParameters.numWorkersPerCPU = 0.0;
	workerPoolParameters.initializer = NULL;
	workerPoolParameters.deinitializer = NULL;
	workerPoolParameters.usrPtr = (void *)this;
	workerPoolParameters.handler = lipFlappingWorkerHandler;
	lipFlappingWorkerPool = new WorkerPool(config, status, frameServer, workerPoolParameters);

	if(!lowLatency) {
		frameServer->registerFrameStatusCheckpoint(FRAME_STATUS_LATE_PROCESSING, "sphinxDriver.ran");

		workerPoolParameters.name = "SphinxDriver.PhonemeBreakdown";
		workerPoolParameters.numWorkers = 1;
		workerPoolParameters.numWorkersPerCPU = 0.0;
		workerPoolParameters.initializer = NULL;
		workerPoolParameters.deinitializer = NULL;
		workerPoolParameters.usrPtr = (void *)this;
		workerPoolParameters.handler = phonemeBreakdownWorkerHandler;
		phonemeBreakdownWorkerPool = new WorkerPool(config, status, frameServer, workerPoolParameters);
	}

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
	if(!lowLatency) {
		delete phonemeBreakdownWorkerPool;
	}

	YerFace_MutexLock(workingVideoFramesMutex);
	if(workingVideoFrames.size() > 0) {
		logger->error("Output Video frames are still pending! Woe is me!");
	}
	YerFace_MutexUnlock(workingVideoFramesMutex);

	YerFace_MutexLock(recognitionMutex);
	if(recognitionResults.size() > 0) {
		logger->error("Not all recognition results were consumed! Woe is me!");
	}
	if(phonemeBuffer.size() > 0) {
		logger->error("Not all phoneme buffer items were consumed! Woe is me!");
	}
	YerFace_MutexUnlock(recognitionMutex);

	SDL_DestroyMutex(recognitionMutex);
	recognitionMutex = NULL;
	SDL_DestroyMutex(workingVideoFramesMutex);
	workingVideoFramesMutex = NULL;

	ps_free(pocketSphinx);
	cmd_ln_free_r(pocketSphinxConfig);

	for(SphinxAudioFrame *audioFrame : audioFramesAllocated) {
		if(audioFrame->inUse) {
			logger->error("About to free an in-use audio frame! Uh oh!");
		}
		av_freep(&audioFrame->buf);
		delete audioFrame;
	}

	delete logger;
}

void SphinxDriver::renderPreviewHUD(Mat frame, FrameNumber frameNumber, int density) {
	static double vuMeterLastSetPeak = vuMeterPeakHoldSeconds * (-1.0);

	if(density > 0) {
		YerFace_MutexLock(workingVideoFramesMutex);
		double maxAmplitude = workingVideoFrames[frameNumber]->maxAmplitude;
		bool peak = workingVideoFrames[frameNumber]->peak;
		YerFace_MutexUnlock(workingVideoFramesMutex);

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

bool SphinxDriver::processPhonemeBreakdown(SphinxVideoFrame *videoFrame) {
	bool processedThroughFrameTime = false;
	YerFace_MutexLock(recognitionMutex);
	while(phonemeBuffer.size()) {
		double phonemeStartTime = phonemeBuffer.back().startTime;
		double phonemeEndTime = phonemeBuffer.back().endTime;
		TimeIntervalComparison comparison = Utilities::timeIntervalCompare(phonemeStartTime, phonemeEndTime, videoFrame->timestamps.startTimestamp, videoFrame->timestamps.estimatedEndTimestamp);
		// logger->verbose("COMPARISON... [A: %lf-%lf (%s)], [B: %lf-%lf], [doesAEndBeforeB: %d, doesAOccurBeforeB: %d, doesAOccurDuringB: %d, doesAOccurAfterB: %d, doesAStartAfterB: %d]", phonemeStartTime, phonemeEndTime, phonemeBuffer.back().pbPhoneme.c_str(), videoFrame->timestamps.startTimestamp, videoFrame->timestamps.estimatedEndTimestamp, comparison.doesAEndBeforeB, comparison.doesAOccurBeforeB, comparison.doesAOccurDuringB, comparison.doesAOccurAfterB, comparison.doesAStartAfterB);
		if(comparison.doesAEndBeforeB) {
			phonemeBuffer.pop_back();
			continue;
		}
		if(comparison.doesAOccurDuringB) {
			if(phonemeBuffer.back().pbPhoneme.length() > 0) {
				double currentPercent = 0.0;
				try {
					currentPercent = videoFrame->phonemes.percent.at(phonemeBuffer.back().pbPhoneme);
				} catch(exception &e) {
					logger->warn("Caught exception during phoneme mapping! %s", e.what());
					throw logic_error("Preston Blair mapping returned an unrecognized phoneme!");
				}
				double divisor = videoFrame->timestamps.estimatedEndTimestamp - videoFrame->timestamps.startTimestamp;
				if(phonemeStartTime < videoFrame->timestamps.startTimestamp) {
					phonemeStartTime = videoFrame->timestamps.startTimestamp;
				}
				if(phonemeEndTime > videoFrame->timestamps.estimatedEndTimestamp) {
					phonemeEndTime = videoFrame->timestamps.estimatedEndTimestamp;
				}
				double numerator = phonemeEndTime - phonemeStartTime;
				currentPercent = currentPercent + (numerator / divisor);
				// logger->verbose("pbPhenome %s is %.04lf / %.04lf = %.04lf", phonemeBuffer.back().pbPhoneme.c_str(), numerator, divisor, numerator / divisor);
				if(currentPercent > 1.0) {
					currentPercent = 1.0;
				}
				videoFrame->phonemes.percent[phonemeBuffer.back().pbPhoneme] = currentPercent;
			}
			if(!comparison.doesAOccurAfterB) {
				phonemeBuffer.pop_back();
			}
		}
		if(comparison.doesAOccurAfterB) {
			processedThroughFrameTime = true;
			break;
		}
	}
	bool frameSuccessfullyProcessed = processedThroughFrameTime || recognizerDrained;
	YerFace_MutexUnlock(recognitionMutex);
	return frameSuccessfullyProcessed;
}

void SphinxDriver::processUtteranceHypothesis(void) {
	int frameRate = cmd_ln_int32_r(pocketSphinxConfig, "-frate");
	ps_seg_t *segmentIterator = ps_seg_iter(pocketSphinx);
	bool addedPhonemes = false;
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
			phonemeBuffer.push_front(phoneme);
			addedPhonemes = true;
		} catch(nlohmann::detail::out_of_range &e) {
			logger->warn("Sphinx reported a phoneme (%s) which we don't have in our mapping. Error was: %s", symbol.c_str(), e.what());
		}

		segmentIterator = ps_seg_next(segmentIterator);
	}
	if(addedPhonemes) {
		phonemeBreakdownWorkerPool->sendWorkerSignal();
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
	double maxAmplitude = 0.0;
	bool inSpeech = false;
	bool peak = false;
	YerFace_MutexLock(recognitionMutex);
	while(recognitionResults.size()) {
		TimeIntervalComparison comparison = Utilities::timeIntervalCompare(recognitionResults.back().startTimestamp, recognitionResults.back().endTimestamp, videoFrame->timestamps.startTimestamp, videoFrame->timestamps.estimatedEndTimestamp);
		// logger->verbose("COMPARISON... doesAEndBeforeB: %d, doesAOccurBeforeB: %d, doesAOccurDuringB: %d, doesAOccurAfterB: %d, doesAStartAfterB: %d", comparison.doesAEndBeforeB, comparison.doesAOccurBeforeB, comparison.doesAOccurDuringB, comparison.doesAOccurAfterB, comparison.doesAStartAfterB);
		if(comparison.doesAEndBeforeB) {
			recognitionResults.pop_back();
			continue;
		}
		if(comparison.doesAOccurDuringB) {
			if(recognitionResults.back().maxAmplitude >= maxAmplitude) {
				maxAmplitude = recognitionResults.back().maxAmplitude;
			}
			if(recognitionResults.back().inSpeech) {
				inSpeech = true;
			}
			if(recognitionResults.back().peak) {
				peak = true;
			}
			if(!comparison.doesAOccurAfterB) {
				recognitionResults.pop_back();
			}
		}
		if(comparison.doesAOccurAfterB) {
			break;
		}
	}
	YerFace_MutexUnlock(recognitionMutex);

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
	audioFramesAllocated.push_front(audioFrame);
	YerFace_MutexUnlock(recognitionMutex);
	return audioFrame;
}

void SphinxDriver::FFmpegDriverAudioFrameCallback(void *userdata, uint8_t *buf, int audioSamples, int audioBytes, double timestamp) {
	SphinxDriver *self = (SphinxDriver *)userdata;
	if(self->recognitionMutex == NULL) {
		return;
	}
	YerFace_MutexLock(self->recognitionMutex);
	if(!self->recognizerRunning) {
		self->logger->error("==== Received an audio frame, but the recognition worker has already stopped! Dropping this audio frame! ====");
		YerFace_MutexUnlock(self->recognitionMutex);
		return;
	}
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

void SphinxDriver::FFmpegDriverAudioIsDrainedCallback(void *userdata) {
	SphinxDriver *self = (SphinxDriver *)userdata;
	if(self->recognitionMutex == NULL) {
		return;
	}
	self->logger->debug("Received notification that audio has drained.");
	YerFace_MutexLock(self->recognitionMutex);
	if(!self->recognizerRunning) {
		self->logger->error("==== Received notification that audio has drained, but the recognizer is already stopped! ====");
	}
	YerFace_MutexUnlock(self->recognitionMutex);
	self->recognitionWorkerPool->stopWorkerNow();
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
			videoFrame->isPhonemeBreakdownReady = false;
			videoFrame->isPhonemeBreakdownProcessed = self->lowLatency ? true : false;
			videoFrame->timestamps = frameTimestamps;
			YerFace_MutexLock(self->workingVideoFramesMutex);
			self->workingVideoFrames[frameNumber] = videoFrame;
			YerFace_MutexUnlock(self->workingVideoFramesMutex);
			break;
		case FRAME_STATUS_MAPPING:
			YerFace_MutexLock(self->workingVideoFramesMutex);
			// self->logger->verbose("handleFrameStatusChange() Frame #" YERFACE_FRAMENUMBER_FORMAT " waiting on Lip Flapping Worker. Queue depth is now %lu", frameNumber, self->workingVideoFrames.size());
			self->workingVideoFrames[frameNumber]->isLipFlappingReady = true;
			YerFace_MutexUnlock(self->workingVideoFramesMutex);
			self->lipFlappingWorkerPool->sendWorkerSignal();
			break;
		case FRAME_STATUS_LATE_PROCESSING:
			YerFace_MutexLock(self->workingVideoFramesMutex);
			// self->logger->verbose("handleFrameStatusChange() Frame #" YERFACE_FRAMENUMBER_FORMAT " waiting on Phoneme Breakdown Worker. Queue depth is now %lu", frameNumber, self->workingVideoFrames.size());
			self->workingVideoFrames[frameNumber]->isPhonemeBreakdownReady = true;
			YerFace_MutexUnlock(self->workingVideoFramesMutex);
			self->phonemeBreakdownWorkerPool->sendWorkerSignal();
			break;
		case FRAME_STATUS_GONE:
			YerFace_MutexLock(self->workingVideoFramesMutex);
			videoFrame = self->workingVideoFrames[frameNumber];
			if(!videoFrame->isLipFlappingProcessed) {
				throw logic_error("Frame is gone, but not yet processed by Lip Flapping worker!");
			}
			if(!videoFrame->isPhonemeBreakdownProcessed) {
				throw logic_error("Frame is gone, but not yet processed by Phoneme Breakdown worker!");
			}
			delete videoFrame;
			self->workingVideoFrames.erase(frameNumber);
			YerFace_MutexUnlock(self->workingVideoFramesMutex);
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
		result.maxAmplitude = 0.0;
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
				self->processUtteranceHypothesis();
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

void SphinxDriver::recognitionWorkerDeinitializer(WorkerPoolWorker *worker, void *usrPtr) {
	SphinxDriver *self = (SphinxDriver *)usrPtr;

	self->logger->debug("Recognition Worker Deinitializing...");
	YerFace_MutexLock(self->recognitionMutex);

	self->recognizerRunning = false;
	if(ps_end_utt(self->pocketSphinx) < 0) {
		throw runtime_error("Failed to end PocketSphinx utterance");
	}
	if(!self->lowLatency) {
		self->processUtteranceHypothesis();
	}
	self->recognizerDrained = true;

	YerFace_MutexUnlock(self->recognitionMutex);
}

bool SphinxDriver::lipFlappingWorkerHandler(WorkerPoolWorker *worker) {
	SphinxDriver *self = (SphinxDriver *)worker->ptr;

	bool didWork = false;
	static FrameNumber lastFrameNumber = -1;
	FrameNumber myFrameNumber = -1;
	SphinxVideoFrame *videoFrame = NULL;

	YerFace_MutexLock(self->workingVideoFramesMutex);
	//// CHECK FOR WORK ////
	for(auto pendingFramePair : self->workingVideoFrames) {
		if(myFrameNumber < 0 || pendingFramePair.first < myFrameNumber) {
			if(!self->workingVideoFrames[pendingFramePair.first]->isLipFlappingProcessed) {
				myFrameNumber = pendingFramePair.first;
			}
		}
	}
	if(myFrameNumber > 0) {
		videoFrame = self->workingVideoFrames[myFrameNumber];
	}
	if(videoFrame != NULL) {
		if(!videoFrame->isLipFlappingReady) {
			// self->logger->verbose("Lip Flapping BLOCKED on frame " YERFACE_FRAMENUMBER_FORMAT " because it is not ready!", myFrameNumber);
			myFrameNumber = -1;
			videoFrame = NULL;
		}
	}
	YerFace_MutexUnlock(self->workingVideoFramesMutex);

	//// DO THE WORK ////
	if(videoFrame != NULL) {
		// self->logger->verbose("Lip Flapping Worker Thread handling frame #" YERFACE_FRAMENUMBER_FORMAT, myFrameNumber);

		if(myFrameNumber <= lastFrameNumber) {
			throw logic_error("SphinxDriver Lip Flapping Worker handling frames out of order!");
		}
		lastFrameNumber = myFrameNumber;

		YerFace_MutexLock(self->workingVideoFramesMutex);
		self->processLipFlappingAudio(videoFrame);
		json percent = videoFrame->phonemes.percent;
		videoFrame->isLipFlappingProcessed = true;
		YerFace_MutexUnlock(self->workingVideoFramesMutex);

		if(self->lowLatency) {
			self->outputDriver->insertFrameData("phonemes", percent, myFrameNumber);
		}

		self->frameServer->setWorkingFrameStatusCheckpoint(myFrameNumber, FRAME_STATUS_MAPPING, "sphinxDriver.ran");

		didWork = true;
	}
	return didWork;
}

bool SphinxDriver::phonemeBreakdownWorkerHandler(WorkerPoolWorker *worker) {
	SphinxDriver *self = (SphinxDriver *)worker->ptr;

	bool didWork = false;
	static FrameNumber lastFrameNumber = -1;
	FrameNumber myFrameNumber = -1;
	SphinxVideoFrame *videoFrame = NULL;

	YerFace_MutexLock(self->workingVideoFramesMutex);
	//// CHECK FOR WORK ////
	for(auto pendingFramePair : self->workingVideoFrames) {
		if(myFrameNumber < 0 || pendingFramePair.first < myFrameNumber) {
			if(!self->workingVideoFrames[pendingFramePair.first]->isPhonemeBreakdownProcessed) {
				myFrameNumber = pendingFramePair.first;
			}
		}
	}
	if(myFrameNumber > 0) {
		videoFrame = self->workingVideoFrames[myFrameNumber];
	}
	if(videoFrame != NULL) {
		if(!videoFrame->isPhonemeBreakdownReady) {
			// self->logger->verbose("Phoneme Breakdown BLOCKED on frame " YERFACE_FRAMENUMBER_FORMAT " because it is not ready!", myFrameNumber);
			myFrameNumber = -1;
			videoFrame = NULL;
		}
	}
	YerFace_MutexUnlock(self->workingVideoFramesMutex);

	//// DO THE WORK ////
	if(videoFrame != NULL) {
		// self->logger->verbose("Phoneme Breakdown Worker Thread handling frame #" YERFACE_FRAMENUMBER_FORMAT, myFrameNumber);

		if(myFrameNumber <= lastFrameNumber) {
			throw logic_error("SphinxDriver Phoneme Breakdown Worker handling frames out of order!");
		}

		YerFace_MutexLock(self->workingVideoFramesMutex);
		bool processed = self->processPhonemeBreakdown(videoFrame);
		json percent;
		if(processed) {
			percent = videoFrame->phonemes.percent;
			videoFrame->isPhonemeBreakdownProcessed = true;
		}
		YerFace_MutexUnlock(self->workingVideoFramesMutex);
		if(processed) {
			if(!self->lowLatency) {
				self->outputDriver->insertFrameData("phonemes", percent, myFrameNumber);
			}

			self->frameServer->setWorkingFrameStatusCheckpoint(myFrameNumber, FRAME_STATUS_LATE_PROCESSING, "sphinxDriver.ran");
			lastFrameNumber = myFrameNumber;
			didWork = true;
		}
	}
	return didWork;
}

} //namespace YerFace
