
#include "FFmpegDriver.hpp"

#include "Utilities.hpp"

#include <exception>
#include <stdexcept>

using namespace std;

namespace YerFace {

MediaContext::MediaContext(void) {
	formatContext = NULL;
	videoDecoderContext = NULL;
	videoStreamIndex = -1;
	videoStream = NULL;
	audioDecoderContext = NULL;
	videoStreamIndex = -1;
	audioStream = NULL;
	demuxerDraining = false;
}

FFmpegDriver::FFmpegDriver(FrameDerivatives *myFrameDerivatives, bool myFrameDrop, bool myLowLatency) {
	logger = new Logger("FFmpegDriver");

	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	frameDrop = myFrameDrop;
	lowLatency = myLowLatency;

	frame = NULL;
	swsContext = NULL;
	readyVideoFrameBufferEmptyWarning = false;

	av_log_set_flags(AV_LOG_SKIP_REPEATED);
	av_log_set_level(AV_LOG_INFO);
	av_log_set_callback(av_log_default_callback);
	avdevice_register_all();
	#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
		av_register_all();
	#endif
	avformat_network_init();

	if(!(frame = av_frame_alloc())) {
		throw runtime_error("failed allocating frame");
	}

	if((videoFrameBufferMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating video frame buffer mutex!");
	}

	initializeDemuxerThread();

	logger->debug("FFmpegDriver object constructed and ready to go! Low Latency mode is %s.", lowLatency ? "ENABLED" : "DISABLED");
}

FFmpegDriver::~FFmpegDriver() {
	logger->debug("FFmpegDriver object destructing...");
	destroyDemuxerThread();
	SDL_DestroyMutex(videoFrameBufferMutex);
	for(MediaContext context : {videoContext, audioContext}) {
		if(context.videoDecoderContext != NULL) {
			avcodec_free_context(&context.videoDecoderContext);
		}
		if(context.audioDecoderContext != NULL) {
			avcodec_free_context(&context.audioDecoderContext);
		}
		if(context.formatContext != NULL) {
			avformat_close_input(&context.formatContext);
			avformat_free_context(context.formatContext);
		}
	}
	av_free(videoDestData[0]);
	av_frame_free(&frame);
	for(VideoFrameBacking *backing : allocatedVideoFrameBackings) {
		av_frame_free(&backing->frameBGR);
		av_free(backing->buffer);
		delete backing;
	}
	for(AudioFrameHandler *handler : audioFrameHandlers) {
		if(handler->resampler.bufferArray != NULL) {
			av_freep(&handler->resampler.bufferArray[0]);
			av_freep(&handler->resampler.bufferArray);
		}
		if(handler->resampler.swrContext != NULL) {
			swr_free(&handler->resampler.swrContext);
		}
		delete handler;
	}
	delete logger;
}

void FFmpegDriver::openInputMedia(string inFile, enum AVMediaType type, String inFormat, String inSize, String inRate, String inCodec, bool tryAudio) {
	int ret;
	if(inFile.length() < 1) {
		throw invalid_argument("specified input video/audio file must be a valid input filename");
	}
	logger->info("Opening media %s...", inFile.c_str());

	AVInputFormat *inputFormat = NULL;
	if(inFormat.length() > 0) {
		inputFormat = av_find_input_format(inFormat.c_str());
		if(!inputFormat) {
			throw invalid_argument("specified input video/audio format could not be resolved");
		}
	}

	MediaContext *context = &videoContext;
	if(type == AVMEDIA_TYPE_AUDIO) {
		context = &audioContext;
	}

	if((context->formatContext = avformat_alloc_context()) == NULL) {
		throw runtime_error("Failed to avformat_alloc_context");
	}
	AVDictionary *options = NULL;

	if(inCodec.length() > 0) {
		AVCodec *codec = avcodec_find_decoder_by_name(inCodec.c_str());
		if(!codec) {
			throw invalid_argument("specified input video/audio codec could not be resolved");
		}
		if(type == AVMEDIA_TYPE_VIDEO) {
			context->formatContext->video_codec = codec;
			context->formatContext->video_codec_id = codec->id;
		} else if(type == AVMEDIA_TYPE_AUDIO) {
			context->formatContext->audio_codec = codec;
			context->formatContext->audio_codec_id = codec->id;
		}
	}

	if(lowLatency) {
		context->formatContext->probesize = 32;
		context->formatContext->flags |= AVFMT_FLAG_NOBUFFER;
	}

	if(inSize.length() > 0) {
		av_dict_set(&options, "video_size", inSize.c_str(), 0);
	}
	if(inRate.length() > 0) {
		av_dict_set(&options, "framerate", inRate.c_str(), 0);
	}

	if((ret = avformat_open_input(&context->formatContext, inFile.c_str(), inputFormat, &options)) < 0) {
		logAVErr("input file could not be opened", ret);
		throw runtime_error("input file could not be opened");
	}
	av_dict_free(&options);

	if((ret = avformat_find_stream_info(context->formatContext, NULL)) < 0) {
		logAVErr("failed finding input stream information for input video/audio", ret);
		throw runtime_error("failed finding input stream information for input video/audio");
	}

	if(type == AVMEDIA_TYPE_AUDIO || (type == AVMEDIA_TYPE_VIDEO && tryAudio)) {
		if(videoContext.audioDecoderContext || audioContext.audioDecoderContext) {
			throw runtime_error("Trying to open an audio context, but one is already open?!");
		}
		try {
			this->openCodecContext(&context->audioStreamIndex, &context->audioDecoderContext, context->formatContext, AVMEDIA_TYPE_AUDIO);
			context->audioStream = context->formatContext->streams[context->audioStreamIndex];
			audioStreamTimeBase = (double)context->audioStream->time_base.num / (double)context->audioStream->time_base.den;
			// logger->verbose("Audio Stream open with Time Base: %.08lf (%d/%d) seconds per unit", audioStreamTimeBase, audioStream->time_base.num, audioStream->time_base.den);
		} catch(exception &e) {
			logger->warn("Failed to open audio stream in %s!", inFile.c_str());
		}
	}

	if(type == AVMEDIA_TYPE_VIDEO) {
		if(videoContext.videoDecoderContext || audioContext.audioDecoderContext) {
			throw runtime_error("Trying to open a video context, but one is already open?!");
		}
		this->openCodecContext(&context->videoStreamIndex, &context->videoDecoderContext, context->formatContext, AVMEDIA_TYPE_VIDEO);
		context->videoStream = context->formatContext->streams[context->videoStreamIndex];
		videoStreamTimeBase = (double)context->videoStream->time_base.num / (double)context->videoStream->time_base.den;
		// logger->verbose("Video Stream open with Time Base: %.08lf (%d/%d) seconds per unit", videoStreamTimeBase, videoStream->time_base.num, videoStream->time_base.den);

		width = context->videoDecoderContext->width;
		height = context->videoDecoderContext->height;
		pixelFormat = context->videoDecoderContext->pix_fmt;
		if((videoDestBufSize = av_image_alloc(videoDestData, videoDestLineSize, width, height, pixelFormat, 1)) < 0) {
			throw runtime_error("failed allocating memory for decoded frame");
		}

		pixelFormatBacking = AV_PIX_FMT_BGR24;
		if((swsContext = sws_getContext(width, height, pixelFormat, width, height, pixelFormatBacking, SWS_BICUBIC, NULL, NULL, NULL)) == NULL) {
			throw runtime_error("failed creating software scaling context");
		}

		for(int i = 0; i < YERFACE_INITIAL_VIDEO_BACKING_FRAMES; i++) {
			allocateNewVideoFrameBacking();
		}
	}

	av_dump_format(context->formatContext, 0, inFile.c_str(), 0);
}

void FFmpegDriver::openCodecContext(int *streamIndex, AVCodecContext **decoderContext, AVFormatContext *myFormatContext, enum AVMediaType type) {
	int myStreamIndex, ret;
	AVStream *stream;
	AVCodec *decoder = NULL;
	AVDictionary *options = NULL;

	if((myStreamIndex = av_find_best_stream(myFormatContext, type, -1, -1, NULL, 0)) < 0) {
		logger->error("failed to find %s stream in input file", av_get_media_type_string(type));
		logAVErr("Error was...", myStreamIndex);
		throw runtime_error("failed to openCodecContext()");
	}

	stream = myFormatContext->streams[myStreamIndex];

	if(!(decoder = avcodec_find_decoder(stream->codecpar->codec_id))) {
		throw runtime_error("failed to find decoder codec");
	}

	if(!(*decoderContext = avcodec_alloc_context3(decoder))) {
		throw runtime_error("failed to allocate decoder context");
	}

	if((ret = avcodec_parameters_to_context(*decoderContext, stream->codecpar)) < 0) {
		logAVErr("failed to copy codec parameters to decoder context", ret);
		throw runtime_error("failed to copy codec parameters to decoder context");
	}

	av_dict_set(&options, "refcounted_frames", "1", 0);
	if((ret = avcodec_open2(*decoderContext, decoder, &options)) < 0) {
		logAVErr("failed to open codec", ret);
		throw runtime_error("failed to open codec");
	}

	*streamIndex = myStreamIndex;
}

bool FFmpegDriver::getIsVideoFrameBufferEmpty(void) {
	YerFace_MutexLock(videoFrameBufferMutex);
	bool status = (readyVideoFrameBuffer.size() < 1);
	YerFace_MutexUnlock(videoFrameBufferMutex);
	return status;
}

VideoFrame FFmpegDriver::getNextVideoFrame(void) {
	YerFace_MutexLock(demuxerMutex);
	YerFace_MutexLock(videoFrameBufferMutex);
	VideoFrame result;
	if(readyVideoFrameBuffer.size() > 0) {
		if(frameDrop) {
			int dropCount = 0;
			while(readyVideoFrameBuffer.size() > 1) {
				releaseVideoFrame(readyVideoFrameBuffer.back());
				readyVideoFrameBuffer.pop_back();
				dropCount++;
			}
			// logger->warn("Dropped %d frame(s)!", dropCount);
		}
		result = readyVideoFrameBuffer.back();
		readyVideoFrameBuffer.pop_back();
		SDL_CondSignal(demuxerCond);
	} else {
		YerFace_MutexUnlock(videoFrameBufferMutex);
		YerFace_MutexUnlock(demuxerMutex);
		throw runtime_error("getNextVideoFrame() was called, but no video frames are pending");
	}
	YerFace_MutexUnlock(videoFrameBufferMutex);
	YerFace_MutexUnlock(demuxerMutex);
	return result;
}

bool FFmpegDriver::waitForNextVideoFrame(VideoFrame *videoFrame) {
	YerFace_MutexLock(demuxerMutex);
	while(getIsVideoFrameBufferEmpty()) {
		if(!demuxerRunning) {
			YerFace_MutexUnlock(demuxerMutex);
			return false;
		}

		//Wait for the demuxer thread to generate more frames. Usually this only happens in realtime scenarios with --frameDrop
		YerFace_MutexUnlock(demuxerMutex);

		if(!readyVideoFrameBufferEmptyWarning) {
			logger->warn("======== waitForNextVideoFrame() Caller is trapped in an expensive polling loop! ========");
			readyVideoFrameBufferEmptyWarning = true;
		}
		SDL_Delay(1);
		YerFace_MutexLock(demuxerMutex);
	}
	*videoFrame = getNextVideoFrame();
	YerFace_MutexUnlock(demuxerMutex);
	return true;
}

void FFmpegDriver::releaseVideoFrame(VideoFrame videoFrame) {
	YerFace_MutexLock(videoFrameBufferMutex);
	videoFrame.frameBacking->inUse = false;
	YerFace_MutexUnlock(videoFrameBufferMutex);
}

void FFmpegDriver::registerAudioFrameCallback(AudioFrameCallback audioFrameCallback) {
	YerFace_MutexLock(demuxerMutex);

	AudioFrameHandler *handler = new AudioFrameHandler();
	handler->audioFrameCallback = audioFrameCallback;
	handler->resampler.swrContext = NULL;
	handler->resampler.bufferArray = NULL;
	handler->resampler.bufferSamples = 0;
	audioFrameHandlers.push_back(handler);

	YerFace_MutexUnlock(demuxerMutex);
}

void FFmpegDriver::logAVErr(String msg, int err) {
	char errbuf[128];
	av_strerror(err, errbuf, 128);
	logger->error("%s AVERROR: (%d) %s", msg.c_str(), err, errbuf);
}

VideoFrameBacking *FFmpegDriver::getNextAvailableVideoFrameBacking(void) {
	YerFace_MutexLock(videoFrameBufferMutex);
	for(VideoFrameBacking *backing : allocatedVideoFrameBackings) {
		if(!backing->inUse) {
			backing->inUse = true;
			YerFace_MutexUnlock(videoFrameBufferMutex);
			return backing;
		}
	}
	logger->warn("Out of spare frames in the video frame buffer! Allocating a new one.");
	VideoFrameBacking *newBacking = allocateNewVideoFrameBacking();
	newBacking->inUse = true;
	YerFace_MutexUnlock(videoFrameBufferMutex);
	return newBacking;
}

VideoFrameBacking *FFmpegDriver::allocateNewVideoFrameBacking(void) {
	VideoFrameBacking *backing = new VideoFrameBacking();
	backing->inUse = false;
	if(!(backing->frameBGR = av_frame_alloc())) {
		throw runtime_error("failed allocating backing video frame");
	}
	int bufferSize = av_image_get_buffer_size(pixelFormatBacking, width, height, 1);
	if((backing->buffer = (uint8_t *)av_malloc(bufferSize*sizeof(uint8_t))) == NULL) {
		throw runtime_error("failed allocating buffer for backing video frame");
	}
	if(av_image_fill_arrays(backing->frameBGR->data, backing->frameBGR->linesize, backing->buffer, pixelFormatBacking, width, height, 1) < 0) {
		throw runtime_error("failed assigning buffer for backing video frame");
	}
	backing->frameBGR->width = width;
	backing->frameBGR->height = height;
	backing->frameBGR->format = pixelFormat;
	allocatedVideoFrameBackings.push_front(backing);
	return backing;
}

bool FFmpegDriver::decodePacket(MediaContext *context, const AVPacket *packet, int streamIndex) {
	int ret;

	if(context->videoStream != NULL && streamIndex == context->videoStreamIndex) {
		// logger->verbose("Got video %s. Sending to codec...", packet ? "packet" : "flush call");
		if(avcodec_send_packet(context->videoDecoderContext, packet) < 0) {
			logger->warn("Error decoding video frame");
			return false;
		}

		while(avcodec_receive_frame(context->videoDecoderContext, frame) == 0) {
			// logger->verbose("Received decoded video frame with timestamp %.04lf (%ld units)!", frame->pts * videoStreamTimeBase, frame->pts);
			if(frame->width != width || frame->height != height || frame->format != pixelFormat) {
				logger->warn("We cannot handle runtime changes to video width, height, or pixel format. Unfortunately, the width, height or pixel format of the input video has changed: old [ width = %d, height = %d, format = %s ], new [ width = %d, height = %d, format = %s ]", width, height, av_get_pix_fmt_name(pixelFormat), frame->width, frame->height, av_get_pix_fmt_name((AVPixelFormat)frame->format));
				return false;
			}

			VideoFrame videoFrame;
			videoFrame.timestamp = (double)frame->pts * videoStreamTimeBase;
			videoFrame.frameBacking = getNextAvailableVideoFrameBacking();
			sws_scale(swsContext, frame->data, frame->linesize, 0, height, videoFrame.frameBacking->frameBGR->data, videoFrame.frameBacking->frameBGR->linesize);
			videoFrame.frameCV = Mat(height, width, CV_8UC3, videoFrame.frameBacking->frameBGR->data[0]);

			YerFace_MutexLock(videoFrameBufferMutex);
			readyVideoFrameBuffer.push_front(videoFrame);
			YerFace_MutexUnlock(videoFrameBufferMutex);

			av_frame_unref(frame);
		}
	}
	if(context->audioStream != NULL && streamIndex == context->audioStreamIndex) {
		// logger->verbose("Got audio %s. Sending to codec...", packet ? "packet" : "flush call");
		if((ret = avcodec_send_packet(context->audioDecoderContext, packet)) < 0) {
			logAVErr("Sending packet to audio codec.", ret);
			return false;
		}

		while(avcodec_receive_frame(context->audioDecoderContext, frame) == 0) {
			double frameTimestamp = frame->pts * audioStreamTimeBase;
			int frameNumSamples = frame->nb_samples * frame->channels;
			// logger->verbose("Received decoded audio frame with %d samples and timestamp %.04lf seconds!", frameNumSamples, frameTimestamp);
			for(AudioFrameHandler *handler : audioFrameHandlers) {
				if(handler->resampler.swrContext == NULL) {
					int inputChannelLayout = context->audioStream->codecpar->channel_layout;
					if(inputChannelLayout == 0) {
						if(context->audioStream->codecpar->channels == 1) {
							inputChannelLayout = AV_CH_LAYOUT_MONO;
						} else if(context->audioStream->codecpar->channels == 2) {
							inputChannelLayout = AV_CH_LAYOUT_STEREO;
						} else {
							throw runtime_error("Unsupported number of channels and/or channel layout!");
						}
					}
					handler->resampler.swrContext = swr_alloc_set_opts(NULL, handler->audioFrameCallback.channelLayout, handler->audioFrameCallback.sampleFormat, handler->audioFrameCallback.sampleRate, inputChannelLayout, (enum AVSampleFormat)context->audioStream->codecpar->format, context->audioStream->codecpar->sample_rate, 0, NULL);
					if(handler->resampler.swrContext == NULL) {
						throw runtime_error("Failed generating a swr context!");
					}
					if(swr_init(handler->resampler.swrContext) < 0) {
						throw runtime_error("Failed initializing swr context!");
					}
					handler->resampler.numChannels = av_get_channel_layout_nb_channels(handler->audioFrameCallback.channelLayout);
				}
				int expectedOutputSamples = swr_get_out_samples(handler->resampler.swrContext, frameNumSamples);
				if(expectedOutputSamples < 0) {
					throw runtime_error("Internal error in FFmpeg software resampler!");
					return false;
				}
				if(handler->resampler.bufferArray == NULL || handler->resampler.bufferSamples < expectedOutputSamples) {
					handler->resampler.bufferSamples = av_rescale_rnd(swr_get_delay(handler->resampler.swrContext, context->audioStream->codecpar->sample_rate) + frameNumSamples, handler->audioFrameCallback.sampleRate, context->audioStream->codecpar->sample_rate, AV_ROUND_UP);
					handler->resampler.bufferSamples += YERFACE_RESAMPLE_BUFFER_HEADROOM;

					if(handler->resampler.bufferArray != NULL) {
						av_freep(&handler->resampler.bufferArray[0]);
						av_freep(&handler->resampler.bufferArray);
					}

					if(av_samples_alloc_array_and_samples(&handler->resampler.bufferArray, &handler->resampler.bufferLineSize, handler->resampler.numChannels, handler->resampler.bufferSamples, handler->audioFrameCallback.sampleFormat, 1) < 0) {
						throw runtime_error("Failed allocating audio buffer!");
					}

					logger->info("Allocated a new audio buffer, %d samples (%d bytes) in size.", handler->resampler.bufferSamples, handler->resampler.bufferLineSize);
				}

				int audioSamples;
				if((audioSamples = swr_convert(handler->resampler.swrContext, handler->resampler.bufferArray, handler->resampler.bufferSamples, (const uint8_t **)frame->data, frame->nb_samples)) < 0) {
					throw runtime_error("Failed running swr_convert() for audio resampling");
				}

				int audioBytes;
				if((audioBytes = av_samples_get_buffer_size(NULL, handler->resampler.numChannels, audioSamples, handler->audioFrameCallback.sampleFormat, 0)) < 0) {
					throw runtime_error("Failed calculating output buffer size");
				}
				
				handler->audioFrameCallback.callback(handler->audioFrameCallback.userdata, handler->resampler.bufferArray[0], audioSamples, audioBytes, handler->resampler.bufferLineSize, frameTimestamp);
			}
			av_frame_unref(frame);
		}
	}

	return true;
}

void FFmpegDriver::initializeDemuxerThread(void) {
	demuxerRunning = true;
	demuxerThread = NULL;
	
	if((demuxerMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating demuxer mutex!");
	}
	if((demuxerCond = SDL_CreateCond()) == NULL) {
		throw runtime_error("Failed creating condition!");
	}
}

void FFmpegDriver::rollDemuxerThread(void) {
	YerFace_MutexLock(demuxerMutex);
	if(demuxerThread != NULL) {
		YerFace_MutexUnlock(demuxerMutex);
		throw runtime_error("rollDemuxerThread was called, but demuxer was already set rolling!");
	}
	if((demuxerThread = SDL_CreateThread(FFmpegDriver::runDemuxerLoop, "DemuxerLoop", (void *)this)) == NULL) {
		YerFace_MutexUnlock(demuxerMutex);
		throw runtime_error("Failed starting thread!");
	}
	YerFace_MutexUnlock(demuxerMutex);
}

void FFmpegDriver::destroyDemuxerThread(void) {
	YerFace_MutexLock(demuxerMutex);
	demuxerRunning = false;
	videoContext.demuxerDraining = true;
	audioContext.demuxerDraining = true;
	SDL_CondSignal(demuxerCond);
	YerFace_MutexUnlock(demuxerMutex);

	SDL_WaitThread(demuxerThread, NULL);

	SDL_DestroyCond(demuxerCond);
	SDL_DestroyMutex(demuxerMutex);
}

int FFmpegDriver::runDemuxerLoop(void *ptr) {
	FFmpegDriver *driver = (FFmpegDriver *)ptr;
	driver->logger->verbose("Demuxer Thread alive!");

	if(!driver->getIsAudioInputPresent()) {
		driver->logger->warn("==== NO AUDIO STREAM IS PRESENT! We can still proceed, but mouth shapes won't be informed by audible speech. ====");
	}

	AVPacket packet;
	av_init_packet(&packet);

	YerFace_MutexLock(driver->demuxerMutex);
	while(driver->demuxerRunning) {
		if(driver->audioContext.formatContext != NULL) {
			while(!driver->audioContext.demuxerDraining && !driver->getIsVideoFrameBufferEmpty() && driver->demuxerRunning) {
				driver->pumpDemuxer(&driver->audioContext, &packet);
			}
		}
		if(driver->videoContext.formatContext != NULL) {
			while(!driver->videoContext.demuxerDraining && (driver->getIsVideoFrameBufferEmpty() || driver->frameDrop) && driver->demuxerRunning) {
				driver->pumpDemuxer(&driver->videoContext, &packet);
				if(driver->frameDrop) {
					YerFace_MutexUnlock(driver->demuxerMutex);
					SDL_Delay(0);
					YerFace_MutexLock(driver->demuxerMutex);
				}
			}
		}

		if(driver->videoContext.demuxerDraining && driver->getIsVideoFrameBufferEmpty()) {
			driver->logger->verbose("Draining complete. Demuxer thread terminating...");
			driver->demuxerRunning = false;
		}
		
		if(driver->demuxerRunning) {
			if(driver->frameDrop) {
				YerFace_MutexUnlock(driver->demuxerMutex);
				SDL_Delay(0);
				YerFace_MutexLock(driver->demuxerMutex);
			} else {
				// driver->logger->verbose("Demuxer Thread going to sleep, waiting for work.");
				if(SDL_CondWait(driver->demuxerCond, driver->demuxerMutex) < 0) {
					throw runtime_error("Failed waiting on condition.");
				}
				// driver->logger->verbose("Demuxer Thread is awake now!");
			}
		}
	}

	YerFace_MutexUnlock(driver->demuxerMutex);
	driver->logger->verbose("Demuxer Thread quitting...");
	return 0;
}

void FFmpegDriver::pumpDemuxer(MediaContext *context, AVPacket *packet) {
	if(av_read_frame(context->formatContext, packet) < 0) {
		logger->verbose("Demuxer thread encountered End of Stream! Going into draining mode...");
		context->demuxerDraining = true;
		if(context->videoStream != NULL) {
			decodePacket(context, NULL, context->videoStreamIndex);
		}
		if(context->audioStream != NULL) {
			decodePacket(context, NULL, context->audioStreamIndex);
		}
	} else {
		try {
			if(!decodePacket(context, packet, packet->stream_index)) {
				logger->warn("Demuxer thread encountered a corrupted packet in the stream!");
			}
		} catch(exception &e) {
			logger->critical("Caught Exception: %s", e.what());
			logger->critical("Going down in flames...");
			demuxerRunning = false;
		}
		av_packet_unref(packet);
	}
}

bool FFmpegDriver::getIsAudioInputPresent(void) {
	return (videoContext.audioStream != NULL) || (audioContext.audioStream != NULL);
}

}; //namespace YerFace
