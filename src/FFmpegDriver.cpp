
#include "FFmpegDriver.hpp"

#include "Utilities.hpp"

#include <exception>
#include <stdexcept>

using namespace std;

namespace YerFace {

MediaContext::MediaContext(void) {
	outAudioChannelMap = CHANNELMAP_NONE;
	frame = NULL;
	formatContext = NULL;
	videoDecoderContext = NULL;
	videoStreamIndex = -1;
	videoStream = NULL;
	audioDecoderContext = NULL;
	videoStreamIndex = -1;
	audioStream = NULL;
	demuxerDraining = false;
	scanning = false;
}

FFmpegDriver::FFmpegDriver(Status *myStatus, FrameServer *myFrameServer, bool myLowLatency, double myFrom, double myUntil, bool myListAllAvailableOptions) {
	logger = new Logger("FFmpegDriver");

	status = myStatus;
	if(status == NULL) {
		throw invalid_argument("status cannot be NULL");
	}
	frameServer = myFrameServer;
	if(frameServer == NULL) {
		throw invalid_argument("frameServer cannot be NULL");
	}
	lowLatency = myLowLatency;
	from = myFrom;
	until = myUntil;

	swsContext = NULL;
	readyVideoFrameBufferEmptyWarning = false;
	videoStreamInitialTimestampSet = false;
	audioStreamInitialTimestampSet = false;
	newestVideoFrameTimestamp = -1.0;
	newestVideoFrameEstimatedEndTimestamp = 0.0;
	newestAudioFrameTimestamp = 0.0;
	audioCallbacksOkay = true;

	av_log_set_flags(AV_LOG_SKIP_REPEATED);
	av_log_set_level(AV_LOG_INFO);
	av_log_set_callback(av_log_default_callback);
	avdevice_register_all();
	#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
		av_register_all();
	#endif
	avformat_network_init();

	if((videoFrameBufferMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating video frame buffer mutex!");
	}
	if((videoStreamMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((audioStreamMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	initializeDemuxerThread(&videoContext, AVMEDIA_TYPE_VIDEO);
	initializeDemuxerThread(&audioContext, AVMEDIA_TYPE_AUDIO);

	if(myListAllAvailableOptions) {
		AVFormatContext *fmt;
		if((fmt = avformat_alloc_context()) == NULL) {
			throw runtime_error("Failed to avformat_alloc_context");
		}
		recursivelyListAllAVOptions((void *)fmt);
		avformat_free_context(fmt);
	}

	logger->debug("FFmpegDriver object constructed and ready to go! Low Latency mode is %s.", lowLatency ? "ENABLED" : "DISABLED");
}

FFmpegDriver::~FFmpegDriver() {
	logger->debug("FFmpegDriver object destructing...");
	destroyDemuxerThreads();
	SDL_DestroyMutex(videoFrameBufferMutex);
	SDL_DestroyMutex(videoStreamMutex);
	SDL_DestroyMutex(audioStreamMutex);
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
		av_frame_free(&context.frame);
	}
	av_free(videoDestData[0]);
	for(VideoFrameBacking *backing : allocatedVideoFrameBackings) {
		av_frame_free(&backing->frameBGR);
		av_free(backing->buffer);
		delete backing;
	}
	for(AudioFrameHandler *handler : audioFrameHandlers) {
		while(handler->resampler.audioFrameBackings.size()) {
			AudioFrameBacking nextFrame = handler->resampler.audioFrameBackings.back();
			av_freep(&nextFrame.bufferArray[0]);
			av_freep(&nextFrame.bufferArray);
			handler->resampler.audioFrameBackings.pop_back();
		}
		if(handler->resampler.swrContext != NULL) {
			swr_free(&handler->resampler.swrContext);
		}
		delete handler;
	}
	delete logger;
}

void FFmpegDriver::openInputMedia(string inFile, enum AVMediaType type, String inFormat, String inSize, String inChannels, String inRate, String inCodec, String outAudioChannelMap, bool tryAudio) {
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

	if(!(context->frame = av_frame_alloc())) {
		throw runtime_error("failed allocating frame");
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
		av_dict_set(&options, "probesize", "32", 0);
		av_dict_set(&options, "analyzeduration", "100000", 0);
		av_dict_set(&options, "avioflags", "direct", 0);
		av_dict_set(&options, "fflags", "nobuffer", 0);
		av_dict_set(&options, "flush_packets", "1", 0);
	}

	if(type == AVMEDIA_TYPE_VIDEO) {
		if(inSize.length() > 0) {
			av_dict_set(&options, "video_size", inSize.c_str(), 0);
		}
		if(inRate.length() > 0) {
			av_dict_set(&options, "framerate", inRate.c_str(), 0);
		}
	} else {
		if(inRate.length() > 0) {
			av_dict_set(&options, "sample_rate", inRate.c_str(), 0);
		}
		if(inChannels.length() > 0) {
			av_dict_set(&options, "channels", inChannels.c_str(), 0);
		}
	}
	if(outAudioChannelMap.length() > 0) {
		if(outAudioChannelMap == "left") {
			context->outAudioChannelMap = CHANNELMAP_LEFT_ONLY;
		} else if(outAudioChannelMap == "right") {
			context->outAudioChannelMap = CHANNELMAP_RIGHT_ONLY;
		} else {
			throw invalid_argument("invalid outAudioChannelMap specified!");
		}
	}

	if((ret = avformat_open_input(&context->formatContext, inFile.c_str(), inputFormat, &options)) < 0) {
		logAVErr("input file could not be opened", ret);
		throw runtime_error("input file could not be opened");
	}
	int count = av_dict_count(options);
	if(count) {
		logger->warn("avformat_open_input() rejected %d option(s)!", count);
		char *dictstring;
		if(av_dict_get_string(options, &dictstring, ',', ';') < 0) {
			logger->error("Failed generating dictionary string!");
		} else {
			logger->warn("Dictionary: %s", dictstring);
		}
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
			// logger->verbose("Audio Stream open with... Time Base: %.08lf (%d/%d) seconds per unit", audioStreamTimeBase, context->audioStream->time_base.num, context->audioStream->time_base.den);
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
		// logger->verbose("Video Stream open with... Time Base: %.08lf (%d/%d) seconds per unit", videoStreamTimeBase, context->videoStream->time_base.num, context->videoStream->time_base.den);

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
	YerFace_MutexLock(videoContext.demuxerMutex);
	YerFace_MutexLock(videoFrameBufferMutex);
	VideoFrame result;
	if(readyVideoFrameBuffer.size() > 0) {
		result = readyVideoFrameBuffer.back();
		readyVideoFrameBuffer.pop_back();
	} else {
		YerFace_MutexUnlock(videoFrameBufferMutex);
		YerFace_MutexUnlock(videoContext.demuxerMutex);
		throw runtime_error("getNextVideoFrame() was called, but no video frames are pending");
	}
	YerFace_MutexUnlock(videoFrameBufferMutex);
	YerFace_MutexUnlock(videoContext.demuxerMutex);
	return result;
}

bool FFmpegDriver::waitForNextVideoFrame(VideoFrame *videoFrame) {
	YerFace_MutexLock(videoContext.demuxerMutex);
	while(getIsVideoFrameBufferEmpty()) {
		if(!videoContext.demuxerRunning) {
			YerFace_MutexUnlock(videoContext.demuxerMutex);
			return false;
		}

		YerFace_MutexUnlock(videoContext.demuxerMutex);

		//FIXME - this is actually quite bad... need to get more insight into this situation.
		// if(!readyVideoFrameBufferEmptyWarning) {
			logger->warn("======== waitForNextVideoFrame() Caller is trapped in an expensive polling loop! ========");
			// readyVideoFrameBufferEmptyWarning = true;
		// }
		SDL_Delay(10);
		YerFace_MutexLock(videoContext.demuxerMutex);
	}
	*videoFrame = getNextVideoFrame();
	YerFace_MutexUnlock(videoContext.demuxerMutex);
	return true;
}

void FFmpegDriver::releaseVideoFrame(VideoFrame videoFrame) {
	YerFace_MutexLock(videoFrameBufferMutex);
	videoFrame.frameBacking->inUse = false;
	YerFace_MutexUnlock(videoFrameBufferMutex);
}

void FFmpegDriver::registerAudioFrameCallback(AudioFrameCallback audioFrameCallback) {
	YerFace_MutexLock(videoContext.demuxerMutex);
	YerFace_MutexLock(audioContext.demuxerMutex);

	AudioFrameHandler *handler = new AudioFrameHandler();
	handler->audioFrameCallback = audioFrameCallback;
	handler->resampler.swrContext = NULL;
	handler->resampler.audioFrameBackings.clear();
	audioFrameHandlers.push_back(handler);

	YerFace_MutexUnlock(audioContext.demuxerMutex);
	YerFace_MutexUnlock(videoContext.demuxerMutex);
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
	double timestamp;

	if(context->videoStream != NULL && streamIndex == context->videoStreamIndex) {
		// logger->verbose("Got video %s. Sending to codec...", packet ? "packet" : "flush call");
		if(avcodec_send_packet(context->videoDecoderContext, packet) < 0) {
			logger->warn("Error decoding video frame");
			return false;
		}

		while(avcodec_receive_frame(context->videoDecoderContext, context->frame) == 0) {
			if(context->frame->width != width || context->frame->height != height || context->frame->format != pixelFormat) {
				logger->warn("We cannot handle runtime changes to video width, height, or pixel format. Unfortunately, the width, height or pixel format of the input video has changed: old [ width = %d, height = %d, format = %s ], new [ width = %d, height = %d, format = %s ]", width, height, av_get_pix_fmt_name(pixelFormat), context->frame->width, context->frame->height, av_get_pix_fmt_name((AVPixelFormat)context->frame->format));
				av_frame_unref(context->frame);
				return false;
			}

			context->frameNumber++;

			timestamp = resolveFrameTimestamp(context, context->frame, AVMEDIA_TYPE_VIDEO);
			if(!handleScanning(context, &timestamp)) {
				av_frame_unref(context->frame);
				continue;
			}

			VideoFrame videoFrame;
			videoFrame.timestamp.startTimestamp = timestamp;
			videoFrame.timestamp.estimatedEndTimestamp = calculateEstimatedEndTimestamp(videoFrame.timestamp.startTimestamp);
			videoFrame.timestamp.frameNumber = context->frameNumber;
			videoFrame.frameBacking = getNextAvailableVideoFrameBacking();

			YerFace_MutexLock(videoStreamMutex);
			newestVideoFrameTimestamp = videoFrame.timestamp.startTimestamp;
			newestVideoFrameEstimatedEndTimestamp = videoFrame.timestamp.estimatedEndTimestamp;
			YerFace_MutexUnlock(videoStreamMutex);
			// logger->verbose("Inserted a VideoFrame with timestamps: %.04lf - (estimated) %.04lf", videoFrame.timestamp, videoFrame.estimatedEndTimestamp);

			sws_scale(swsContext, context->frame->data, context->frame->linesize, 0, height, videoFrame.frameBacking->frameBGR->data, videoFrame.frameBacking->frameBGR->linesize);
			videoFrame.frameCV = Mat(height, width, CV_8UC3, videoFrame.frameBacking->frameBGR->data[0]);

			YerFace_MutexLock(videoFrameBufferMutex);
			readyVideoFrameBuffer.push_front(videoFrame);
			YerFace_MutexUnlock(videoFrameBufferMutex);

			av_frame_unref(context->frame);
		}
	}
	if(context->audioStream != NULL && streamIndex == context->audioStreamIndex) {
		// logger->verbose("Got audio %s. Sending to codec...", packet ? "packet" : "flush call");
		if((ret = avcodec_send_packet(context->audioDecoderContext, packet)) < 0) {
			logAVErr("Sending packet to audio codec.", ret);
			return false;
		}

		while(avcodec_receive_frame(context->audioDecoderContext, context->frame) == 0) {
			timestamp = resolveFrameTimestamp(context, context->frame, AVMEDIA_TYPE_AUDIO);
			if(!handleScanning(context, &timestamp)) {
				av_frame_unref(context->frame);
				continue;
			}

			YerFace_MutexLock(audioStreamMutex);
			newestAudioFrameTimestamp = timestamp;
			YerFace_MutexUnlock(audioStreamMutex);
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
					handler->resampler.numChannels = av_get_channel_layout_nb_channels(handler->audioFrameCallback.channelLayout);
					if(handler->resampler.numChannels > 2) {
						throw runtime_error("Somebody asked us to generate an unsupported number of audio channels.");
					}
					if(context->outAudioChannelMap != CHANNELMAP_NONE) {
						if(context->outAudioChannelMap == CHANNELMAP_LEFT_ONLY) {
							handler->resampler.channelMapping[0] = 0;
							handler->resampler.channelMapping[1] = 0;
						} else {
							handler->resampler.channelMapping[0] = 1;
							handler->resampler.channelMapping[1] = 1;
						}
						if((ret = swr_set_channel_mapping(handler->resampler.swrContext, handler->resampler.channelMapping)) < 0) {
							logAVErr("Failed setting channel mapping.", ret);
							throw runtime_error("Failed setting channel mapping!");
						}
					}
					if(swr_init(handler->resampler.swrContext) < 0) {
						throw runtime_error("Failed initializing swr context!");
					}
				}

				int bufferLineSize;
				AudioFrameBacking audioFrameBacking;
				audioFrameBacking.timestamp = timestamp;
				audioFrameBacking.bufferArray = NULL;
				//bufferSamples represents the expected number of samples produced by swr_convert() *PER CHANNEL*
				audioFrameBacking.bufferSamples = av_rescale_rnd(swr_get_delay(handler->resampler.swrContext, context->audioStream->codecpar->sample_rate) + context->frame->nb_samples, handler->audioFrameCallback.sampleRate, context->audioStream->codecpar->sample_rate, AV_ROUND_UP);

				if(av_samples_alloc_array_and_samples(&audioFrameBacking.bufferArray, &bufferLineSize, handler->resampler.numChannels, audioFrameBacking.bufferSamples, handler->audioFrameCallback.sampleFormat, 1) < 0) {
					throw runtime_error("Failed allocating audio buffer!");
				}

				// logger->info("Allocated a new audio buffer, %d samples (%d bytes) in size.", handler->resampler.bufferSamples, handler->resampler.bufferLineSize);

				if((audioFrameBacking.audioSamples = swr_convert(handler->resampler.swrContext, audioFrameBacking.bufferArray, audioFrameBacking.bufferSamples, (const uint8_t **)context->frame->data, context->frame->nb_samples)) < 0) {
					throw runtime_error("Failed running swr_convert() for audio resampling");
				}

				audioFrameBacking.audioBytes = audioFrameBacking.audioSamples * handler->resampler.numChannels * av_get_bytes_per_sample(handler->audioFrameCallback.sampleFormat);
				
				handler->resampler.audioFrameBackings.push_front(audioFrameBacking);
				// logger->verbose("Pushed a resampled audio frame for handler. Frame queue depth is %d", handler->resampler.audioFrameBackings.size());
			}
			av_frame_unref(context->frame);
		}
	}

	return true;
}

void FFmpegDriver::initializeDemuxerThread(MediaContext *context, enum AVMediaType type) {
	context->demuxerRunning = false;
	context->demuxerThread = NULL;
	context->demuxerMutex = NULL;
	context->frameNumber = 0;

	if((context->demuxerMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating demuxer mutex!");
	}

	if(from != -1.0 && from > 0.0) {
		context->scanning = true;
	}
}

void FFmpegDriver::rollDemuxerThreads(void) {
	rollDemuxerThread(&videoContext, AVMEDIA_TYPE_VIDEO);
	if(audioContext.audioStream != NULL) {
		rollDemuxerThread(&audioContext, AVMEDIA_TYPE_AUDIO);
	}
}

void FFmpegDriver::rollDemuxerThread(MediaContext *context, enum AVMediaType type) {
	YerFace_MutexLock(context->demuxerMutex);
	if(context->demuxerThread != NULL) {
		YerFace_MutexUnlock(context->demuxerMutex);
		throw runtime_error("rollDemuxerThread was called, but demuxer was already set rolling!");
	}
	context->demuxerRunning = true;
	if(type == AVMEDIA_TYPE_VIDEO) {
		context->demuxerThread = SDL_CreateThread(FFmpegDriver::runVideoDemuxerLoop, "VideoDemuxer", (void *)this);
	} else {
		context->demuxerThread = SDL_CreateThread(FFmpegDriver::runAudioDemuxerLoop, "AudioDemuxer", (void *)this);
	}
	if(context->demuxerThread == NULL) {
		YerFace_MutexUnlock(context->demuxerMutex);
		throw runtime_error("Failed starting thread!");
	}
	YerFace_MutexUnlock(context->demuxerMutex);
}

void FFmpegDriver::destroyDemuxerThreads(void) {
	YerFace_MutexLock(audioStreamMutex);
	audioCallbacksOkay = false;
	YerFace_MutexUnlock(audioStreamMutex);

	destroyDemuxerThread(&videoContext, AVMEDIA_TYPE_VIDEO);
	destroyDemuxerThread(&audioContext, AVMEDIA_TYPE_AUDIO);
}

void FFmpegDriver::destroyDemuxerThread(MediaContext *context, enum AVMediaType type) {
	YerFace_MutexLock(context->demuxerMutex);
	context->demuxerRunning = false;
	context->demuxerDraining = true;
	YerFace_MutexUnlock(context->demuxerMutex);

	if(context->demuxerThread != NULL) {
		SDL_WaitThread(context->demuxerThread, NULL);
	}

	SDL_DestroyMutex(context->demuxerMutex);
}

int FFmpegDriver::runVideoDemuxerLoop(void *ptr) {
	FFmpegDriver *driver = (FFmpegDriver *)ptr;
	driver->logger->verbose("Video Demuxer Thread alive!");

	if(!driver->getIsAudioInputPresent()) {
		driver->logger->warn("==== NO AUDIO STREAM IS PRESENT! We can still proceed, but mouth shapes won't be informed by audible speech. ====");
	}

	bool includeAudio = false;
	if(driver->videoContext.audioStream != NULL) {
		includeAudio = true;
	}
	int ret = driver->innerDemuxerLoop(&driver->videoContext, AVMEDIA_TYPE_VIDEO, includeAudio);
	driver->logger->verbose("Video Demuxer Thread quitting...");
	return ret;
}

int FFmpegDriver::runAudioDemuxerLoop(void *ptr) {
	FFmpegDriver *driver = (FFmpegDriver *)ptr;
	driver->logger->verbose("Audio Demuxer Thread alive!");
	int ret = driver->innerDemuxerLoop(&driver->audioContext, AVMEDIA_TYPE_AUDIO, true);
	driver->logger->verbose("Audio Demuxer Thread quitting...");
	return ret;
}

int FFmpegDriver::innerDemuxerLoop(MediaContext *context, enum AVMediaType type, bool includeAudio) {
	AVPacket packet;
	av_init_packet(&packet);

	YerFace_MutexLock(context->demuxerMutex);
	while(context->demuxerRunning) {
		if(status->getIsPaused() && status->getIsRunning()) {
			YerFace_MutexUnlock(context->demuxerMutex);
			SDL_Delay(100);
			YerFace_MutexLock(context->demuxerMutex);
			continue;
		}
		
		if(type == AVMEDIA_TYPE_VIDEO) {
			if(!getIsVideoDraining()) {
				// logger->verbose("Pumping VIDEO stream.");
				pumpDemuxer(context, &packet, type);
			}

			if(getIsVideoDraining() && getIsVideoFrameBufferEmpty()) {
				logger->verbose("Draining Video complete. Demuxer thread terminating...");
				context->demuxerRunning = false;
			}
		} else {
			if(!getIsAudioDraining()) {
				// logger->verbose("Pumping AUDIO stream.");
				pumpDemuxer(context, &packet, type);
			}
		}

		if(type == AVMEDIA_TYPE_AUDIO || includeAudio) {
			flushAudioHandlers(getIsAudioDraining());
		}

		if(context->demuxerRunning) {
			YerFace_MutexUnlock(context->demuxerMutex);
			SDL_Delay(0); //FIXME - CPU Starvation?
			YerFace_MutexLock(context->demuxerMutex);
		}
	}

	YerFace_MutexUnlock(context->demuxerMutex);
	return 0;
}

void FFmpegDriver::pumpDemuxer(MediaContext *context, AVPacket *packet, enum AVMediaType type) {
	try {
		if(av_read_frame(context->formatContext, packet) < 0) {
			logger->verbose("Demuxer thread encountered End of Stream! Going into draining mode...");
			SDL_mutex *streamMutex = videoStreamMutex;
			if(type == AVMEDIA_TYPE_AUDIO) {
				streamMutex = audioStreamMutex;
			}
			YerFace_MutexLock(streamMutex);
			context->demuxerDraining = true;
			YerFace_MutexUnlock(streamMutex);

			if(context->videoStream != NULL) {
				decodePacket(context, NULL, context->videoStreamIndex);
			}
			if(context->audioStream != NULL) {
				decodePacket(context, NULL, context->audioStreamIndex);
			}
		} else {
			if(!decodePacket(context, packet, packet->stream_index)) {
				logger->warn("Demuxer thread encountered a corrupted packet in the stream!");
			}
			av_packet_unref(packet);
		}
	} catch(exception &e) {
		logger->critical("Caught Exception: %s", e.what());
		logger->critical("Going down in flames...");
		context->demuxerRunning = false;
	}
}

bool FFmpegDriver::flushAudioHandlers(bool draining) {
	bool completelyFlushed = true;
	for(AudioFrameHandler *handler : audioFrameHandlers) {
		while(handler->resampler.audioFrameBackings.size()) {
			YerFace_MutexLock(videoStreamMutex);
			double myNewestVideoFrameEstimatedEndTimestamp = newestVideoFrameEstimatedEndTimestamp;
			YerFace_MutexUnlock(videoStreamMutex);
			YerFace_MutexLock(audioStreamMutex);
			bool callbacksOkay = audioCallbacksOkay;
			YerFace_MutexUnlock(audioStreamMutex);

			AudioFrameBacking nextFrame = handler->resampler.audioFrameBackings.back();
			if(nextFrame.timestamp < myNewestVideoFrameEstimatedEndTimestamp || draining || lowLatency) {
				if(callbacksOkay) {
					handler->audioFrameCallback.callback(handler->audioFrameCallback.userdata, nextFrame.bufferArray[0], nextFrame.audioSamples, nextFrame.audioBytes, nextFrame.timestamp);
				}
				av_freep(&nextFrame.bufferArray[0]);
				av_freep(&nextFrame.bufferArray);
				handler->resampler.audioFrameBackings.pop_back();
			} else {
				completelyFlushed = false;
				break;
			}
		}
	}
	return completelyFlushed;
}

bool FFmpegDriver::getIsAudioInputPresent(void) {
	return (videoContext.audioStream != NULL) || (audioContext.audioStream != NULL);
}

bool FFmpegDriver::getIsAudioDraining(void) {
	//// WARNING! Do *NOT* call this function with either videoStreamMutex or audioStreamMutex locked!
	YerFace_MutexLock(videoStreamMutex);
	YerFace_MutexLock(audioStreamMutex);
	bool ret = (audioContext.formatContext != NULL && audioContext.demuxerDraining) || \
		(videoContext.formatContext != NULL && videoContext.demuxerDraining);
	YerFace_MutexUnlock(audioStreamMutex);
	YerFace_MutexUnlock(videoStreamMutex);
	return ret;
}

bool FFmpegDriver::getIsVideoDraining(void) {
	YerFace_MutexLock(videoStreamMutex);
	bool ret = videoContext.demuxerDraining;
	YerFace_MutexUnlock(videoStreamMutex);
	return ret;
}

double FFmpegDriver::calculateEstimatedEndTimestamp(double startTimestamp) {
	frameStartTimes.push_back(startTimestamp);
	while(frameStartTimes.size() > YERFACE_FRAME_DURATION_ESTIMATE_BUFFER) {
		frameStartTimes.pop_front();
	}
	int count = 0, deltaCount = 0;
	double lastTimestamp, delta, accum = 0.0;
	for(double timestamp : frameStartTimes) {
		if(count > 0) {
			delta = (timestamp - lastTimestamp);
			accum += delta;
			deltaCount++;
		}
		lastTimestamp = timestamp;
		count++;
	}
	if(deltaCount == 0) {
		return startTimestamp + (1.0 / 120.0);
	}
	return startTimestamp + (accum / (double)deltaCount);
}

double FFmpegDriver::resolveFrameTimestamp(MediaContext *context, AVFrame *frame, enum AVMediaType type) {
	//// WARNING! Do *NOT* call this function with either videoStreamMutex or audioStreamMutex locked!
	YerFace_MutexLock(videoStreamMutex);
	YerFace_MutexLock(audioStreamMutex);
	double *timeBase = &videoStreamTimeBase;
	double *initialFrameTimestamp = &videoStreamInitialTimestamp;
	bool *initialFrameTimestampSet = &videoStreamInitialTimestampSet;
	if(type == AVMEDIA_TYPE_AUDIO) {
		timeBase = &audioStreamTimeBase;
		initialFrameTimestamp = &audioStreamInitialTimestamp;
		initialFrameTimestampSet = &audioStreamInitialTimestampSet;
	}

	double timestamp = (double)frame->pts * *timeBase;

	// logger->verbose("Calculating timestamp for %s frame with timestamp %.04lf (%ld units)!", type == AVMEDIA_TYPE_VIDEO ? "VIDEO" : "AUDIO", timestamp, frame->pts);

	if(!*initialFrameTimestampSet) {
		// logger->verbose("Setting initial %s timestamp to %.04lf", type == AVMEDIA_TYPE_VIDEO ? "VIDEO" : "AUDIO", timestamp);
		*initialFrameTimestamp = timestamp;
		*initialFrameTimestampSet = true;
	}
	timestamp = timestamp - *initialFrameTimestamp;
	// logger->verbose("After compensating for initial offset, %s frame timestamp is calculated to be %.04lf", type == AVMEDIA_TYPE_VIDEO ? "VIDEO" : "AUDIO", timestamp);

	YerFace_MutexUnlock(audioStreamMutex);
	YerFace_MutexUnlock(videoStreamMutex);

	return timestamp;
}

void FFmpegDriver::stopAudioCallbacksNow(void) {
	YerFace_MutexLock(audioStreamMutex);
	audioCallbacksOkay = false;
	YerFace_MutexUnlock(audioStreamMutex);
}

void FFmpegDriver::recursivelyListAllAVOptions(void *obj, string depth) {
	const AVClass *c;
	if(!obj) {
		return;
	}
	c = *(const AVClass**)obj;
	const AVOption *opt = NULL;
	while((opt = av_opt_next(obj, opt)) != NULL) {
		logger->verbose("%s %s AVOption: %s (%s)", depth.c_str(), c->class_name, opt->name, opt->help);
	}
	const AVClass *childobjclass = NULL;
	while((childobjclass = av_opt_child_class_next(c, childobjclass)) != NULL) {
		void *childobj = &childobjclass;
		recursivelyListAllAVOptions(childobj, "  " + depth);
	}
}

bool FFmpegDriver::handleScanning(MediaContext *context, double *timestamp) {
	if((from != -1.0 && *timestamp < from) || (until != -1.0 && *timestamp > until)) {
		if(until != -1.0 && *timestamp > until) {
			context->demuxerDraining = true;
		}
		logger->verbose("Throwing away a frame at (unadjusted) timestamp %.04lf to handle scanning. (From and Until)", *timestamp);
		return false;
	}
	context->scanning = false;
	if(from != -1.0) {
		*timestamp = *timestamp - from;
	}
	return true;
}

}; //namespace YerFace
