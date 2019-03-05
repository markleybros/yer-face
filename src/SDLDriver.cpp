
#include "SDLDriver.hpp"

#include "Utilities.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>

#include "SDL.h"

using namespace std;

namespace YerFace {

SDLDriver::SDLDriver(json config, Status *myStatus, FrameServer *myFrameServer, FFmpegDriver *myFFmpegDriver, bool myHeadless, bool myAudioPreview) {
	logger = new Logger("SDLDriver");

	if((previewPositionInFrameMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((previewDebugDensityMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((audioFramesMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((onBasisFlagCallbacksMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	previewRatio = config["YerFace"]["SDLDriver"]["PreviewHUD"]["previewRatio"];
	previewWidthPercentage = config["YerFace"]["SDLDriver"]["PreviewHUD"]["previewWidthPercentage"];
	previewCenterHeightPercentage = config["YerFace"]["SDLDriver"]["PreviewHUD"]["previewCenterHeightPercentage"];

	setPreviewPositionInFrame(BottomRight);
	setPreviewDebugDensity(2);
	previewWindow.window = NULL;
	previewWindow.renderer = NULL;
	previewTexture = NULL;
	onBasisFlagCallbacks.clear();

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
	headless = myHeadless;
	audioPreview = myAudioPreview;

	Uint32 sdlInitFlags = 0;
	if(!headless) {
		sdlInitFlags |= SDL_INIT_VIDEO;
		if(audioPreview) {
			sdlInitFlags |= SDL_INIT_AUDIO;
		}
	}
	if(SDL_Init(sdlInitFlags) != 0) {
		logger->error("Unable to initialize SDL: %s", SDL_GetError());
	}
	atexit(SDL_Quit);

	audioDevice.opened = false;
	if(!headless && audioPreview) {
		SDL_zero(audioDevice.desired);
		audioDevice.desired.freq = 44100;
		audioDevice.desired.format = AUDIO_S16SYS;
		audioDevice.desired.channels = 2;
		audioDevice.desired.samples = 4096;
		audioDevice.desired.userdata = (void *)this;
		audioDevice.desired.callback = SDLDriver::SDLAudioCallback;
		audioDevice.deviceID = SDL_OpenAudioDevice(NULL, 0, &audioDevice.desired, &audioDevice.obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
		if(audioDevice.deviceID == 0) {
			logger->error("SDL error opening audio device: %s", SDL_GetError());
			throw runtime_error("failed opening audio device!");
		}
		audioDevice.opened = true;
		logger->info("Opened Audio Output << %d hz, %d channels, %d-bit %s %s %s samples >>", (int)audioDevice.obtained.freq, (int)audioDevice.obtained.channels, (int)SDL_AUDIO_BITSIZE(audioDevice.obtained.format), SDL_AUDIO_ISSIGNED(audioDevice.obtained.format) ? "signed" : "unsigned", SDL_AUDIO_ISBIGENDIAN(audioDevice.obtained.format) ? "big-endian" : "little-endian", SDL_AUDIO_ISFLOAT(audioDevice.obtained.format) ? "float" : "int");
		SDL_PauseAudioDevice(audioDevice.deviceID, 0);

		AudioFrameCallback audioFrameCallback;
		audioFrameCallback.userdata = (void *)this;
		if(audioDevice.obtained.channels == 1) {
			audioFrameCallback.channelLayout = AV_CH_LAYOUT_MONO;
		} else if(audioDevice.obtained.channels == 2) {
			audioFrameCallback.channelLayout = AV_CH_LAYOUT_STEREO;
		} else {
			throw runtime_error("encountered unsupported audio channel layout");
		}
		if(audioDevice.obtained.format == AUDIO_U8) {
			audioFrameCallback.sampleFormat = AV_SAMPLE_FMT_U8;
		} else if(audioDevice.obtained.format == AUDIO_S16SYS) {
			audioFrameCallback.sampleFormat = AV_SAMPLE_FMT_S16;
		} else if(audioDevice.obtained.format == AUDIO_S32SYS) {
			audioFrameCallback.sampleFormat = AV_SAMPLE_FMT_S32;
		} else {
			throw runtime_error("encountered unsupported audio sample format");
		}
		audioFrameCallback.sampleRate = audioDevice.obtained.freq;
		audioFrameCallback.callback = FFmpegDriverAudioFrameCallback;
		ffmpegDriver->registerAudioFrameCallback(audioFrameCallback);
	}

	logger->debug("SDLDriver object constructed and ready to go!");
}

SDLDriver::~SDLDriver() {
	logger->debug("SDLDriver object destructing...");
	if(audioDevice.opened) {
		SDL_PauseAudioDevice(audioDevice.deviceID, 1);
		SDL_CloseAudioDevice(audioDevice.deviceID);
	}
	if(previewWindow.window != NULL) {
		SDL_DestroyWindow(previewWindow.window);
	}
	if(previewWindow.renderer != NULL) {
		SDL_DestroyRenderer(previewWindow.renderer);
	}
	SDL_DestroyMutex(previewPositionInFrameMutex);
	previewPositionInFrameMutex = NULL;
	SDL_DestroyMutex(previewDebugDensityMutex);
	previewDebugDensityMutex = NULL;
	SDL_DestroyMutex(audioFramesMutex);
	audioFramesMutex = NULL;
	SDL_DestroyMutex(onBasisFlagCallbacksMutex);
	onBasisFlagCallbacksMutex = NULL;
	for(SDLAudioFrame *audioFrame : audioFramesAllocated) {
		if(audioFrame->buf != NULL) {
			av_freep(&audioFrame->buf);
		}
		delete audioFrame;
	}
	delete logger;
}

SDLWindowRenderer SDLDriver::createPreviewWindow(int width, int height) {
	if(headless) {
		logger->verbose("SDLDriver::createPreviewWindow() called, but we're running in headless mode. This is an NOP.");
		return previewWindow;
	}
	if(previewWindow.window != NULL || previewWindow.renderer != NULL) {
		throw logic_error("SDL Driver asked to create a preview window, but one already exists!");
	}
	logger->info("Creating Preview SDL Window <%dx%d>", width, height);
	previewWindow.window = SDL_CreateWindow("YerFace! Preview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_SHOWN);
	if(previewWindow.window == NULL) {
		throw runtime_error("SDL Driver tried to create a preview window and failed.");
	}
	logger->verbose("Creating Preview SDL Renderer.");
	previewWindow.renderer = SDL_CreateRenderer(previewWindow.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if(previewWindow.renderer == NULL) {
		throw runtime_error("SDL Driver tried to create a preview renderer and failed.");
	}
	return previewWindow;
}

SDLWindowRenderer SDLDriver::getPreviewWindow(void) {
	return previewWindow;
}

SDL_Texture *SDLDriver::getPreviewTexture(Size textureSize) {
	if(headless) {
		logger->verbose("SDLDriver::getPreviewTexture() called, but we're running in headless mode. This is an NOP.");
		return previewTexture;
	}
	if(previewTexture != NULL) {
		int actualWidth, actualHeight;
		SDL_QueryTexture(previewTexture, NULL, NULL, &actualWidth, &actualHeight);
		if(actualWidth != textureSize.width || actualHeight != textureSize.height) {
			logger->error("Requested texture size (%dx%d) does not match previous texture size (%dx%d)! Somebody has yanked the rug out from under us.", textureSize.width, textureSize.height, actualWidth, actualHeight);
			throw runtime_error("Requested vs. Actual texture size mismatch! Somebody has yanked the rug out from under us.");
		}
		return previewTexture;
	}
	if(previewWindow.renderer == NULL) {
		throw logic_error("SDLDriver::getPreviewTexture() was called, but there is no preview window renderer!");
	}
	logger->verbose("Creating Preview SDL Texture <%dx%d>", textureSize.width, textureSize.height);
	previewTexture = SDL_CreateTexture(previewWindow.renderer, SDL_PIXELFORMAT_BGR24, SDL_TEXTUREACCESS_STREAMING, textureSize.width, textureSize.height);
	if(previewTexture == NULL) {
		throw runtime_error("SDL Driver was not able to create a preview texture!");
	}
	return previewTexture;
}

void SDLDriver::doRenderPreviewFrame(Mat previewFrame) {
	if(headless) {
		logger->verbose("SDLDriver::doRenderPreviewFrame() called, but we're running in headless mode. This is an NOP.");
		return;
	}
	if(previewWindow.window == NULL || previewWindow.renderer == NULL) {
		throw logic_error("SDL Driver asked to render preview frame, but no preview window/renderer exists!");
	}

	Size previewFrameSize = previewFrame.size();
	SDL_Texture *texture = getPreviewTexture(previewFrameSize);

	unsigned char * textureData = NULL;
	int texturePitch = 0;
	SDL_LockTexture(texture, 0, (void **)&textureData, &texturePitch);
	memcpy(textureData, (void *)previewFrame.data, previewFrameSize.width * previewFrameSize.height * previewFrame.channels());
	SDL_UnlockTexture(texture);

	SDL_RenderClear(previewWindow.renderer);
	SDL_RenderCopy(previewWindow.renderer, texture, NULL, NULL);
	SDL_RenderPresent(previewWindow.renderer);
}

void SDLDriver::doHandleEvents(void) {
	SDL_Event event;
	while(SDL_PollEvent(&event)){
		switch(event.type) {
			case SDL_QUIT:
				status->setIsRunning(false);
				break;
			case SDL_KEYDOWN:
				switch(event.key.keysym.sym) {
					case SDLK_ESCAPE:
						status->setIsRunning(false);
						break;
					case SDLK_SPACE:
						status->toggleIsPaused();
						break;
					case SDLK_LEFT:
						movePreviewPositionInFrame(MoveLeft);
						break;
					case SDLK_UP:
						movePreviewPositionInFrame(MoveUp);
						break;
					case SDLK_RIGHT:
						movePreviewPositionInFrame(MoveRight);
						break;
					case SDLK_DOWN:
						movePreviewPositionInFrame(MoveDown);
						break;
					case SDLK_d:
						incrementPreviewDebugDensity();
						break;
					case SDLK_RETURN:
						logger->verbose("Received Basis Flag keyboard event. Rebroadcasting...");
						YerFace_MutexLock(onBasisFlagCallbacksMutex);
						for(auto callback : onBasisFlagCallbacks) {
							callback();
						}
						YerFace_MutexUnlock(onBasisFlagCallbacksMutex);
						break;
				}
				break;
		}
	}
}

void SDLDriver::setPreviewPositionInFrame(PreviewPositionInFrame newPosition) {
	YerFace_MutexLock(previewPositionInFrameMutex);
	previewPositionInFrame = newPosition;
	YerFace_MutexUnlock(previewPositionInFrameMutex);
}

PreviewPositionInFrame SDLDriver::movePreviewPositionInFrame(PreviewPositionInFrameDirection moveDirection) {
	YerFace_MutexLock(previewPositionInFrameMutex);
	switch(moveDirection) {
		case MoveLeft:
			previewPositionInFrame = BottomLeft;
			break;
		case MoveUp:
			previewPositionInFrame = TopRight;
			break;
		case MoveRight:
			if(previewPositionInFrame == BottomLeft) {
				previewPositionInFrame = BottomRight;
			}
			break;
		case MoveDown:
			if(previewPositionInFrame == TopRight) {
				previewPositionInFrame = BottomRight;
			}
			break;
	}
	PreviewPositionInFrame status = previewPositionInFrame;
	YerFace_MutexUnlock(previewPositionInFrameMutex);
	return status;
}

PreviewPositionInFrame SDLDriver::getPreviewPositionInFrame(void) {
	YerFace_MutexLock(previewPositionInFrameMutex);
	PreviewPositionInFrame status = previewPositionInFrame;
	YerFace_MutexUnlock(previewPositionInFrameMutex);
	return status;
}

void SDLDriver::setPreviewDebugDensity(int newDensity) {
	YerFace_MutexLock(previewDebugDensityMutex);
	if(newDensity < 0) {
		previewDebugDensity = 0;
	} else if(newDensity > YERFACE_PREVIEW_DEBUG_DENSITY_MAX) {
		previewDebugDensity = YERFACE_PREVIEW_DEBUG_DENSITY_MAX;
	} else {
		previewDebugDensity = newDensity;
	}
	logger->debug("Preview Debug Density set to %d", previewDebugDensity);
	YerFace_MutexUnlock(previewDebugDensityMutex);
}

int SDLDriver::incrementPreviewDebugDensity(void) {
	YerFace_MutexLock(previewDebugDensityMutex);
	previewDebugDensity++;
	if(previewDebugDensity > YERFACE_PREVIEW_DEBUG_DENSITY_MAX) {
		previewDebugDensity = 0;
	}
	int status = previewDebugDensity;
	logger->debug("Preview Debug Density set to %d", previewDebugDensity);
	YerFace_MutexUnlock(previewDebugDensityMutex);
	return status;
}

int SDLDriver::getPreviewDebugDensity(void) {
	YerFace_MutexLock(previewDebugDensityMutex);
	int status = previewDebugDensity;
	YerFace_MutexUnlock(previewDebugDensityMutex);
	return status;
}

void SDLDriver::createPreviewHUDRectangle(Size frameSize, Rect2d *previewRect, Point2d *previewCenter) {
	previewRect->width = frameSize.width * previewWidthPercentage;
	previewRect->height = previewRect->width * previewRatio;
	PreviewPositionInFrame previewPosition = getPreviewPositionInFrame();
	if(previewPosition == BottomRight || previewPosition == TopRight) {
		previewRect->x = frameSize.width - previewRect->width;
	} else {
		previewRect->x = 0;
	}
	if(previewPosition == BottomLeft || previewPosition == BottomRight) {
		previewRect->y = frameSize.height - previewRect->height;
	} else {
		previewRect->y = 0;
	}
	*previewCenter = Utilities::centerRect(*previewRect);
	previewCenter->y -= previewRect->height * previewCenterHeightPercentage;
}

void SDLDriver::onBasisFlagEvent(function<void(void)> callback) {
	YerFace_MutexLock(onBasisFlagCallbacksMutex);
	onBasisFlagCallbacks.push_back(callback);
	YerFace_MutexUnlock(onBasisFlagCallbacksMutex);
}

SDLAudioFrame *SDLDriver::getNextAvailableAudioFrame(int desiredBufferSize) {
	YerFace_MutexLock(audioFramesMutex);
	for(SDLAudioFrame *audioFrame : audioFramesAllocated) {
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
			YerFace_MutexUnlock(audioFramesMutex);
			return audioFrame;
		}
	}
	SDLAudioFrame *audioFrame = new SDLAudioFrame();
	if((audioFrame->buf = (uint8_t *)av_malloc(desiredBufferSize)) == NULL) {
		throw runtime_error("unable to allocate memory for audio frame");
	}
	audioFrame->bufferSize = desiredBufferSize;
	audioFrame->pos = 0;
	audioFrame->inUse = true;
	YerFace_MutexUnlock(audioFramesMutex);
	return audioFrame;
}

void SDLDriver::SDLAudioCallback(void* userdata, Uint8* stream, int len) {
	SDLDriver *self = (SDLDriver *)userdata;
	YerFace_MutexLock(self->audioFramesMutex);
	int streamPos = 0;
	int frameDiscards = 0, frameFills = 0;
	// self->logger->verbose("Audio Callback Fired");
	FrameTimestamps frameTimestamps;
	// FIXME
	// try {
	// 	frameTimestamps = self->frameServer->getCompletedFrameTimestamps();
	// } catch(exception &e) {
		frameTimestamps.startTimestamp = 0.0;
		frameTimestamps.estimatedEndTimestamp = 0.0;
	// }
	while(len - streamPos > 0) {
		int remaining = len - streamPos;
		// self->logger->verbose("Audio Callback... Length: %d, streamPos: %d, Remaining: %d, Frame Start: %lf, Frame End: %lf", len, streamPos, remaining, frameTimestamps.startTimestamp, frameTimestamps.estimatedEndTimestamp);

		double audioLateGraceTimestamp = frameTimestamps.startTimestamp - YERFACE_AUDIO_LATE_GRACE;
		while(self->audioFrameQueue.size() > 0 && self->audioFrameQueue.back()->timestamp < audioLateGraceTimestamp) {
			self->logger->verbose("==== AUDIO IS LATE! (Video Frame Start Time: %.04lf, Audio Frame Start Time: %.04lf, Grace Period: %.04lf) Discarding one audio frame. ====", frameTimestamps.startTimestamp, self->audioFrameQueue.back()->timestamp, YERFACE_AUDIO_LATE_GRACE);
			self->audioFrameQueue.back()->inUse = false;
			self->audioFrameQueue.pop_back();
			frameDiscards++;
		}

		if(self->audioFrameQueue.size() > 0) {
			// self->logger->verbose("Filling audio buffer from frame in audio frame queue...");
			int consumeBytes = remaining;
			int frameRemainingBytes = self->audioFrameQueue.back()->audioBytes - self->audioFrameQueue.back()->pos;
			if(frameRemainingBytes < consumeBytes) {
				consumeBytes = frameRemainingBytes;
				// self->logger->verbose("This frame won't fill our whole buffer...");
			}
			memcpy(stream + streamPos, self->audioFrameQueue.back()->buf + self->audioFrameQueue.back()->pos, consumeBytes);
			self->audioFrameQueue.back()->pos += consumeBytes;
			if(self->audioFrameQueue.back()->pos >= self->audioFrameQueue.back()->audioBytes) {
				// self->logger->verbose("Popped audio frame off the back of the queue.");
				self->audioFrameQueue.back()->inUse = false;
				self->audioFrameQueue.pop_back();
			}
			streamPos += consumeBytes;
			frameFills++;
		} else {
			// self->logger->verbose("Out of audio frames! Filling the rest of the buffer with silence.");
			memset(stream + streamPos, self->audioDevice.obtained.silence, remaining);
			streamPos += remaining;
			if(!frameFills && frameDiscards > 0) {
				self->logger->warn("====== Discarded %d unsuitable input audio frames and used zero input audio frames! Audio preview buffer is full of silence! ======", frameDiscards);
			}
		}
	}
	YerFace_MutexUnlock(self->audioFramesMutex);
}

void SDLDriver::FFmpegDriverAudioFrameCallback(void *userdata, uint8_t *buf, int audioSamples, int audioBytes, double timestamp) {
	SDLDriver *self = (SDLDriver *)userdata;
	if(self->audioFramesMutex == NULL) {
		return;
	}
	// self->logger->verbose("AudioFrameCallback fired! Frame timestamp is %lf.", timestamp);
	YerFace_MutexLock(self->audioFramesMutex);
	SDLAudioFrame *audioFrame = self->getNextAvailableAudioFrame(audioBytes);
	memcpy(audioFrame->buf, buf, audioBytes);
	audioFrame->audioSamples = audioSamples;
	audioFrame->audioBytes = audioBytes;
	audioFrame->timestamp = timestamp;
	self->audioFrameQueue.push_front(audioFrame);
	YerFace_MutexUnlock(self->audioFramesMutex);
}

void SDLDriver::stopAudioDriverNow(void) {
	if(audioDevice.opened) {
		SDL_PauseAudioDevice(audioDevice.deviceID, 1);
	}
}

}; //namespace YerFace
