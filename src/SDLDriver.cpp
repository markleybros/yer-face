
#include "SDLDriver.hpp"

#include "Utilities.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>

#include "SDL.h"

using namespace std;
using namespace cv;

namespace YerFace {

SDLDriver::SDLDriver(json config, Status *myStatus, FrameServer *myFrameServer, FFmpegDriver *myFFmpegDriver, bool myHeadless, bool myAudioPreview) {
	logger = new Logger("SDLDriver");

	joystickEnabled = config["YerFace"]["SDLDriver"]["joystick"]["enabled"];
	joystickEventsRaw = config["YerFace"]["SDLDriver"]["joystick"]["eventsRaw"];

	if((audioFramesMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((callbacksMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((frameTimestampsNowMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	frameTimestampsNow.startTimestamp = 0.0;
	frameTimestampsNow.estimatedEndTimestamp = 0.0;
	previewWindow.window = NULL;
	previewWindow.renderer = NULL;
	previewTextures.videoTexture = NULL;
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
		if(joystickEnabled) {
			sdlInitFlags |= SDL_INIT_JOYSTICK;
		}
	}
	if(SDL_Init(sdlInitFlags) != 0) {
		logger->crit("Unable to initialize SDL: %s", SDL_GetError());
		throw runtime_error("Unable to initialize SDL!");
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
			logger->err("SDL error opening audio device: %s", SDL_GetError());
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
		audioFrameCallback.audioFrameCallback = FFmpegDriverAudioFrameCallback;
		audioFrameCallback.isDrainedCallback = NULL;
		ffmpegDriver->registerAudioFrameCallback(audioFrameCallback);

		FrameStatusChangeEventCallback frameStatusChangeCallback;
		frameStatusChangeCallback.userdata = (void *)this;
		frameStatusChangeCallback.callback = handleFrameStatusChange;
		frameStatusChangeCallback.newStatus = FRAME_STATUS_PREVIEW_DISPLAY;
		frameServer->onFrameStatusChangeEvent(frameStatusChangeCallback);
	}

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

	if(joystickEnabled) {
		SDLJoystickDevice joystick;
		int numJoysticks = SDL_NumJoysticks();
		logger->debug1("Joystick support enabled. Detected number of joysticks: %d", numJoysticks);
		for(int x = 0; x < numJoysticks; x++) {
			joystick.index = x;
			joystick.joystick = SDL_JoystickOpen(joystick.index);
			if(joystick.joystick) {
				joystick.id = SDL_JoystickInstanceID(joystick.joystick);
				joystick.name = SDL_JoystickNameForIndex(joystick.index);
				joystick.axesNum = SDL_JoystickNumAxes(joystick.joystick);
				joystick.axesValues.assign(joystick.axesNum, -1000.0);
				joystick.buttonsNum = SDL_JoystickNumButtons(joystick.joystick);
				joystick.buttonsPressedTime.assign(joystick.buttonsNum, 0);
				joystick.hatsNum = SDL_JoystickNumHats(joystick.joystick);

				json controllerSettingsList = config["YerFace"]["SDLDriver"]["joystick"]["controllerSettings"];
				json controllerSettings = controllerSettingsList["default"];

				bool controllerSettingsFound = false;
				if(controllerSettingsList.contains(joystick.name)) {
					controllerSettings = controllerSettingsList[joystick.name];
					controllerSettingsFound = true;
				} else {
					for(json settings : controllerSettingsList) {
						if(settings.contains("alternateNames")) {
							for(std::string alternateName : settings["alternateNames"]) {
								if(joystick.name == alternateName) {
									controllerSettings = settings;
									controllerSettingsFound = true;
									break;
								}
							}
						}
						if(controllerSettingsFound) {
							break;
						}
					}
				}

				joystick.buttonEventMappingBasis = -1;
				joystick.buttonEventMappingPreviewDebugDensity = -1;
				if(controllerSettings.contains("buttonEventMapping")) {
					if(controllerSettings["buttonEventMapping"].contains("basis")) {
						joystick.buttonEventMappingBasis = controllerSettings["buttonEventMapping"]["basis"];
					}
					if(controllerSettings["buttonEventMapping"].contains("previewDebugDensity")) {
						joystick.buttonEventMappingPreviewDebugDensity = controllerSettings["buttonEventMapping"]["previewDebugDensity"];
					}
				}
				joystick.axisMin = 10;
				joystick.axisMax = 32767;
				if(controllerSettings.contains("axisSettings")) {
					if(controllerSettings["axisSettings"].contains("min")) {
						joystick.axisMin = controllerSettings["axisSettings"]["min"];
						joystick.axisMax = controllerSettings["axisSettings"]["max"];
					}
				}

				joysticks[joystick.id] = joystick;
				logger->debug1("Opened Joystick %d [%s] with InstanceID %d, %d Axes, %d Hats, and %d Buttons. Joystick configuration %s found.", joystick.index, joystick.name.c_str(), joystick.id, joystick.axesNum, joystick.hatsNum, joystick.buttonsNum, controllerSettingsFound ? "WAS" : "WAS NOT");
			}
		}
		SDL_JoystickEventState(SDL_ENABLE);
	}

	logger->debug1("SDLDriver object constructed and ready to go!");
}

SDLDriver::~SDLDriver() noexcept(false) {
	logger->debug1("SDLDriver object destructing...");
	if(joystickEnabled) {
		std::unordered_map<SDL_JoystickID, SDLJoystickDevice>::iterator it = joysticks.begin();
		while(it != joysticks.end()) {
			SDL_JoystickClose(it->second.joystick);
			it++;
		}
	}
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
	SDL_DestroyMutex(frameTimestampsNowMutex);
	frameTimestampsNowMutex = NULL;
	SDL_DestroyMutex(audioFramesMutex);
	audioFramesMutex = NULL;
	SDL_DestroyMutex(callbacksMutex);
	callbacksMutex = NULL;
	for(SDLAudioFrame *audioFrame : audioFramesAllocated) {
		if(audioFrame->buf != NULL) {
			av_freep(&audioFrame->buf);
		}
		delete audioFrame;
	}
	delete logger;
}

SDLWindowRenderer SDLDriver::createPreviewWindow(int width, int height, string windowTitle) {
	if(headless) {
		logger->warning("SDLDriver::createPreviewWindow() called, but we're running in headless mode. This is an NOP.");
		return previewWindow;
	}
	if(previewWindow.window != NULL || previewWindow.renderer != NULL) {
		throw logic_error("SDL Driver asked to create a preview window, but one already exists!");
	}
	previewWindowTitle = windowTitle;
	logger->info("Creating Preview SDL Window <%dx%d> \"%s\"", width, height, previewWindowTitle.c_str());
	previewWindow.window = SDL_CreateWindow(previewWindowTitle.c_str(),
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
		SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if(previewWindow.window == NULL) {
		throw runtime_error("SDL Driver tried to create a preview window and failed.");
	}
	logger->debug1("Creating Preview SDL Renderer.");
	previewWindow.renderer = SDL_CreateRenderer(previewWindow.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if(previewWindow.renderer == NULL) {
		throw runtime_error("SDL Driver tried to create a preview renderer and failed.");
	}
	if(SDL_GetRendererInfo(previewWindow.renderer, &previewWindow.rendererInfo) != 0) {
		throw runtime_error("Was not able to query properties of our new SDL Renderer");
	}
	logger->debug1("Got SDL Renderer %s with mode %s, %s and max texture dimensions %dx%d.",
		previewWindow.rendererInfo.name,
		previewWindow.rendererInfo.flags & SDL_RENDERER_SOFTWARE ? "SOFTWARE" : "HARDWARE",
		previewWindow.rendererInfo.flags & SDL_RENDERER_ACCELERATED ? "ACCELERATED" : "NON-ACCELERATED",
		previewWindow.rendererInfo.max_texture_width, previewWindow.rendererInfo.max_texture_height);
	return previewWindow;
}

SDLWindowRenderer SDLDriver::getPreviewWindow(void) {
	return previewWindow;
}

void SDLDriver::initializePreviewTextures(cv::Size textureSize) {
	if(headless) {
		logger->warning("SDLDriver::initializePreviewTextures() called, but we're running in headless mode. This is an NOP.");
		return;
	}

	// If we already have a texture, validate that its dimensions area correct.
	if(previewTextures.videoTexture != NULL) {
		int actualWidth, actualHeight;
		SDL_QueryTexture(previewTextures.videoTexture, NULL, NULL, &actualWidth, &actualHeight);
		if(actualWidth != textureSize.width || actualHeight != textureSize.height) {
			logger->crit("Requested texture size (%dx%d) does not match previous texture size (%dx%d)! Somebody has yanked the rug out from under us.", textureSize.width, textureSize.height, actualWidth, actualHeight);
			throw runtime_error("Requested vs. Actual texture size mismatch! Somebody has yanked the rug out from under us.");
		}
	}

	if(previewTextures.videoTexture == NULL) {
		// Check our renderer settings.
		if(previewWindow.renderer == NULL) {
			throw logic_error("SDLDriver::initializePreviewTextures() was called, but there is no preview window renderer!");
		}
		if(previewWindow.rendererInfo.max_texture_width < textureSize.width || previewWindow.rendererInfo.max_texture_height < textureSize.height) {
			logger->warning("Our SDL Renderer %s reports maximum texture dimensions %dx%d, but the input video has dimensions %dx%d! This will not end well...", previewWindow.rendererInfo.name, previewWindow.rendererInfo.max_texture_width, previewWindow.rendererInfo.max_texture_height, textureSize.width, textureSize.height);
		}

		// Acquire textures for video.
		logger->info("Creating Preview Video SDL Texture <%dx%d>", textureSize.width, textureSize.height);
		previewTextures.videoTexture = SDL_CreateTexture(previewWindow.renderer, SDL_PIXELFORMAT_BGR24, SDL_TEXTUREACCESS_STREAMING, textureSize.width, textureSize.height);
		if(previewTextures.videoTexture == NULL) {
			throw runtime_error("SDL Driver was not able to create a preview video texture!");
		}
	}
}

void SDLDriver::doRenderPreviewFrame(Mat previewFrame) {
	if(headless) {
		logger->warning("SDLDriver::doRenderPreviewFrame() called, but we're running in headless mode. This is an NOP.");
		return;
	}
	if(previewWindow.window == NULL || previewWindow.renderer == NULL) {
		throw logic_error("SDL Driver asked to render preview frame, but no preview window/renderer exists!");
	}

	// Make sure the textures are ready.
	Size previewFrameSize = previewFrame.size();
	initializePreviewTextures(previewFrameSize);

	// Handle letterboxing of content within the window.
	SDL_Rect viewport;
	SDL_RenderGetViewport(previewWindow.renderer, &viewport);
	double sourceAspect = (double)previewFrameSize.width / (double)previewFrameSize.height;
	double destAspect = (double)viewport.w / (double)viewport.h;
	if(destAspect < sourceAspect) {
		double newHeight = (double)viewport.w / sourceAspect;
		double newY = ((double)viewport.h - newHeight) / 2.0;
		viewport.h = (int)round(newHeight);
		viewport.y = (int)round(newY);
	} else if(destAspect > sourceAspect) {
		double newWidth = (double)viewport.h * sourceAspect;
		double newX = ((double)viewport.w - newWidth) / 2.0;
		viewport.w = (int)round(newWidth);
		viewport.x = (int)round(newX);
	}

	// Update preview frame texture.
	unsigned char *textureData = NULL;
	int texturePitch = 0;
	SDL_LockTexture(previewTextures.videoTexture, 0, (void **)&textureData, &texturePitch);
	memcpy(textureData, (void *)previewFrame.data, previewFrameSize.width * previewFrameSize.height * previewFrame.channels());
	SDL_UnlockTexture(previewTextures.videoTexture);

	// Draw
	SDL_RenderClear(previewWindow.renderer);
	SDL_RenderCopy(previewWindow.renderer, previewTextures.videoTexture, NULL, &viewport);
	SDL_RenderPresent(previewWindow.renderer);
}

void SDLDriver::doHandleEvents(void) {
	SDL_Event event;
	SDLJoystickDevice *joystick;
	double axisValue, buttonHeldSeconds;
	int hatXValue, hatYValue;
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
						status->movePreviewPositionInFrame(MoveLeft);
						break;
					case SDLK_UP:
						status->movePreviewPositionInFrame(MoveUp);
						break;
					case SDLK_RIGHT:
						status->movePreviewPositionInFrame(MoveRight);
						break;
					case SDLK_DOWN:
						status->movePreviewPositionInFrame(MoveDown);
						break;
					case SDLK_d:
						status->incrementPreviewDebugDensity();
						break;
					case SDLK_RETURN:
						logger->info("Received Basis Flag keyboard event. Rebroadcasting...");
						YerFace_MutexLock(callbacksMutex);
						for(auto callback : onBasisFlagCallbacks) {
							callback();
						}
						YerFace_MutexUnlock(callbacksMutex);
						break;
				}
				break;
			case SDL_JOYBUTTONDOWN:
			case SDL_JOYBUTTONUP:
				joystick = &joysticks[event.jbutton.which];
				logger->debug2("Joystick ID %d, Button %d, Status: %s", joystick->id, event.jbutton.button, event.jbutton.state == SDL_PRESSED ? "PRESSED" : "RELEASED");

				if(event.jbutton.state == SDL_PRESSED) {
					buttonHeldSeconds = -1.0;
					joystick->buttonsPressedTime[event.jbutton.button] = event.jbutton.timestamp;

					if(event.jbutton.button == joystick->buttonEventMappingBasis) {
						logger->info("Received Basis Flag joystick event. Rebroadcasting...");
						YerFace_MutexLock(callbacksMutex);
						for(auto callback : onBasisFlagCallbacks) {
							callback();
						}
						YerFace_MutexUnlock(callbacksMutex);
					} else if(event.jbutton.button == joystick->buttonEventMappingPreviewDebugDensity) {
						status->incrementPreviewDebugDensity();
					}
				} else {
					buttonHeldSeconds = (double)(event.jbutton.timestamp - joystick->buttonsPressedTime[event.jbutton.button]) / 1000.0;
					if(buttonHeldSeconds < 0.0) {
						buttonHeldSeconds = 0.0;
					}
					logger->debug4("Released button was held for %.02f seconds.", buttonHeldSeconds);
				}

				if(joystickEventsRaw) {
					YerFace_MutexLock(callbacksMutex);
					for(auto callback : onJoystickButtonEventCallbacks) {
						callback(event.jbutton.timestamp, joystick->id, event.jbutton.button, event.jbutton.state == SDL_PRESSED, buttonHeldSeconds);
					}
					YerFace_MutexUnlock(callbacksMutex);
				}

				break;
			case SDL_JOYAXISMOTION:
				joystick = &joysticks[event.jaxis.which];
				axisValue = 0.0;
				if(abs(event.jaxis.value) > joystick->axisMin) {
					axisValue = (double)event.jaxis.value / (double)joystick->axisMax;
				}
				if(axisValue > 1.0) {
					axisValue = 1.0;
				} else if(axisValue < -1.0) {
					axisValue = -1.0;
				}
				if(axisValue != joystick->axesValues[event.jaxis.axis]) {
					joystick->axesValues[event.jaxis.axis] = axisValue;
					logger->debug2("Joystick ID %d, Axis %d, Value: %.02f (%d)", joystick->id, event.jaxis.axis, axisValue, event.jaxis.value);

					if(joystickEventsRaw) {
						YerFace_MutexLock(callbacksMutex);
						for(auto callback : onJoystickAxisEventCallbacks) {
							callback(event.jaxis.timestamp, joystick->id, event.jaxis.axis, axisValue);
						}
						YerFace_MutexUnlock(callbacksMutex);
					}
				}
				break;
			case SDL_JOYHATMOTION:
				joystick = &joysticks[event.jbutton.which];
				hatXValue = 0;
				hatYValue = 0;
				if(event.jhat.value == SDL_HAT_LEFTUP || event.jhat.value == SDL_HAT_LEFT || event.jhat.value == SDL_HAT_LEFTDOWN) {
					hatXValue = -1;
				} else if(event.jhat.value == SDL_HAT_RIGHTUP || event.jhat.value == SDL_HAT_RIGHT || event.jhat.value == SDL_HAT_RIGHTDOWN) {
					hatXValue = 1;
				}
				if(event.jhat.value == SDL_HAT_LEFTUP || event.jhat.value == SDL_HAT_UP || event.jhat.value == SDL_HAT_RIGHTUP) {
					hatYValue = -1;
				} else if(event.jhat.value == SDL_HAT_LEFTDOWN || event.jhat.value == SDL_HAT_DOWN || event.jhat.value == SDL_HAT_RIGHTDOWN) {
					hatYValue = 1;
				}
				logger->debug2("Joystick ID %d, Hat %d, X: %d, Y: %d", joystick->id, event.jhat.which, hatXValue, hatYValue);

				if(joystickEventsRaw) {
					YerFace_MutexLock(callbacksMutex);
					for(auto callback : onJoystickHatEventCallbacks) {
						callback(event.jhat.timestamp, joystick->id, event.jhat.hat, hatXValue, hatYValue);
					}
					YerFace_MutexUnlock(callbacksMutex);
				}
				break;
		}
	}
}

void SDLDriver::onBasisFlagEvent(function<void(void)> callback) {
	YerFace_MutexLock(callbacksMutex);
	onBasisFlagCallbacks.push_back(callback);
	YerFace_MutexUnlock(callbacksMutex);
}

void SDLDriver::onJoystickButtonEvent(function<void(Uint32 relativeTimestamp, int deviceId, int button, bool pressed, double heldSeconds)> callback) {
	YerFace_MutexLock(callbacksMutex);
	onJoystickButtonEventCallbacks.push_back(callback);
	YerFace_MutexUnlock(callbacksMutex);
}

void SDLDriver::onJoystickAxisEvent(function<void(Uint32 relativeTimestamp, int deviceId, int axis, double value)> callback) {
	YerFace_MutexLock(callbacksMutex);
	onJoystickAxisEventCallbacks.push_back(callback);
	YerFace_MutexUnlock(callbacksMutex);
}

void SDLDriver::onJoystickHatEvent(function<void(Uint32 relativeTimestamp, int deviceId, int hat, int x, int y)> callback) {
	YerFace_MutexLock(callbacksMutex);
	onJoystickHatEventCallbacks.push_back(callback);
	YerFace_MutexUnlock(callbacksMutex);
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

	YerFace_MutexLock(self->frameTimestampsNowMutex);
	FrameTimestamps frameTimestamps = self->frameTimestampsNow;
	YerFace_MutexUnlock(self->frameTimestampsNowMutex);

	YerFace_MutexLock(self->audioFramesMutex);
	int streamPos = 0;
	int frameDiscards = 0, frameFills = 0;
	self->logger->debug4("SDL Audio Device Callback Fired");

	while(len - streamPos > 0) {
		int remaining = len - streamPos;
		self->logger->debug4("Audio Callback Buffer Filling Loop... Length: %d, streamPos: %d, Remaining: %d, Frame Start: %lf, Frame End: %lf", len, streamPos, remaining, frameTimestamps.startTimestamp, frameTimestamps.estimatedEndTimestamp);

		double audioLateGraceTimestamp = frameTimestamps.startTimestamp - YERFACE_AUDIO_LATE_GRACE;
		while(self->audioFrameQueue.size() > 0 && self->audioFrameQueue.back()->timestamp < audioLateGraceTimestamp) {
			self->logger->debug4("AUDIO IS LATE! (Video Frame Start Time: %.04lf, Audio Frame Start Time: %.04lf, Grace Period: %.04lf) Discarding one audio frame.", frameTimestamps.startTimestamp, self->audioFrameQueue.back()->timestamp, YERFACE_AUDIO_LATE_GRACE);
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
			self->logger->debug4("Ran out of audio frames! Filling the rest of the buffer with silence.");
			memset(stream + streamPos, self->audioDevice.obtained.silence, remaining);
			streamPos += remaining;
			if(!frameFills && frameDiscards > 0) {
				self->logger->warning("Discarded %d unsuitable input audio frames and used zero input audio frames! Audio preview buffer is full of silence!", frameDiscards);
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
	self->logger->debug4("FFmpegDriver passed us an audio frame! Frame timestamp is %lf.", timestamp);
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

void SDLDriver::handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps) {
	SDLDriver *self = (SDLDriver *)userdata;
	switch(newStatus) {
		default:
			throw logic_error("Handler passed unsupported frame status change event!");
		case FRAME_STATUS_PREVIEW_DISPLAY:
			YerFace_MutexLock(self->frameTimestampsNowMutex);
			self->frameTimestampsNow = frameTimestamps;
			YerFace_MutexUnlock(self->frameTimestampsNowMutex);
			break;
	}
}

}; //namespace YerFace
