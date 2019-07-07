
#include "FFmpegDriver.hpp"

#include "Utilities.hpp"

#include <exception>
#include <stdexcept>

using namespace std;
using namespace cv;

namespace YerFace {

MediaInputContext::MediaInputContext(void) {
	inputAudioChannelMap = CHANNELMAP_NONE;
	frame = NULL;
	formatContext = NULL;
	videoStreamPTSOffset = 0;
	videoMuxLastPTS = -1;
	videoMuxLastDTS = -1;
	videoDecoderContext = NULL;
	videoStreamIndex = -1;
	videoStream = NULL;
	audioStreamPTSOffset = 0;
	audioMuxLastPTS = -1;
	audioMuxLastDTS = -1;
	audioDecoderContext = NULL;
	audioStreamIndex = -1;
	audioStream = NULL;
	demuxerDraining = false;
	initialized = false;
}

MediaOutputContext::MediaOutputContext(void) {
	outputFormat = NULL;
	formatContext = NULL;
	videoStream = NULL;
	audioStream = NULL;
	initialized = false;
}

FFmpegDriver::FFmpegDriver(Status *myStatus, FrameServer *myFrameServer, bool myLowLatency, bool myListAllAvailableOptions) {
	videoCaptureWorkerPool = NULL;
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

	swsContext = NULL;
	newestVideoFrameTimestamp = -1.0;
	newestVideoFrameEstimatedEndTimestamp = 0.0;
	newestAudioFrameTimestamp = -1.0;
	audioCallbacksOkay = true;

	av_log_set_callback(FFmpegDriver::logAVCallback);
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
	if((demuxerThreadMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating demuxer mutex!");
	}

	demuxerThreadRunning = false;
	demuxerThread = NULL;
	videoContext.frameNumber = 0;
	audioContext.frameNumber = 0;

	if(myListAllAvailableOptions) {
		AVFormatContext *fmt;
		if((fmt = avformat_alloc_context()) == NULL) {
			throw runtime_error("Failed to avformat_alloc_context");
		}
		recursivelyListAllAVOptions((void *)fmt);
		avformat_free_context(fmt);
	}

	logger->debug1("FFmpegDriver object constructed and ready to go! Low Latency mode is %s.", lowLatency ? "ENABLED" : "DISABLED");
}

FFmpegDriver::~FFmpegDriver() noexcept(false) {
	logger->debug1("FFmpegDriver object destructing...");
	destroyDemuxerThread();

	if(outputContext.initialized && outputContext.formatContext != NULL) {
		logger->info("Closing output video file...");
		// logger->debug3("Calling av_write_trailer(outputContext.formatContext)");
		av_write_trailer(outputContext.formatContext);
		if(outputContext.formatContext && !(outputContext.outputFormat->flags & AVFMT_NOFILE)) {
			logger->debug3("Calling avio_close(outputContext.formatContext->pb)");
			avio_close(outputContext.formatContext->pb);
		}
		// logger->debug3("Calling avformat_free_context(outputContext.formatContext)");
		avformat_free_context(outputContext.formatContext);
		SDL_DestroyMutex(outputContext.multiplexerMutex);
		logger->info("All done closing output video file.");
	}

	SDL_DestroyMutex(videoFrameBufferMutex);
	SDL_DestroyMutex(videoStreamMutex);
	SDL_DestroyMutex(audioStreamMutex);
	for(MediaInputContext *context : {&videoContext, &audioContext}) {
		string contextName = "UNKNOWN";
		if(context == &videoContext) {
			contextName = "videoContext";
		} else if(context == &audioContext) {
			contextName = "audioContext";
		}
		if(context->videoDecoderContext != NULL) {
			// logger->debug3("Calling avcodec_free_context(&%s->videoDecoderContext)", contextName.c_str());
			avcodec_free_context(&context->videoDecoderContext);
		}
		if(context->audioDecoderContext != NULL) {
			// logger->debug3("Calling avcodec_free_context(&%s->audioDecoderContext)", contextName.c_str());
			avcodec_free_context(&context->audioDecoderContext);
		}
		if(context->formatContext != NULL) {
			// logger->debug3("Calling avformat_close_input(&%s->formatContext)", contextName.c_str());
			avformat_close_input(&context->formatContext);
		}
		// logger->debug3("Calling av_frame_free(&%s->frame)", contextName.c_str());
		av_frame_free(&context->frame);
	}
	logger->debug3("Calling av_free(videoDestData[0])");
	av_free(videoDestData[0]);
	for(VideoFrameBacking *backing : allocatedVideoFrameBackings) {
		// logger->debug3("Calling av_frame_free(&backing->frameBGR)");
		av_frame_free(&backing->frameBGR);
		// logger->debug3("Calling av_free(backing->buffer)");
		av_free(backing->buffer);
		delete backing;
	}
	for(AudioFrameHandler *handler : audioFrameHandlers) {
		while(handler->resampler.audioFrameBackings.size()) {
			AudioFrameBacking nextFrame = handler->resampler.audioFrameBackings.back();
			// logger->debug3("Calling av_freep(&nextFrame.bufferArray[0])");
			av_freep(&nextFrame.bufferArray[0]);
			// logger->debug3("Calling av_freep(&nextFrame.bufferArray)");
			av_freep(&nextFrame.bufferArray);
			handler->resampler.audioFrameBackings.pop_back();
		}
		if(handler->resampler.swrContext != NULL) {
			// logger->debug3("Calling swr_free(&handler->resampler.swrContext)");
			swr_free(&handler->resampler.swrContext);
		}
		delete handler;
	}
	// logger->debug3("Calling sws_freeContext(swsContext)");
	sws_freeContext(swsContext);
	delete logger;

	//This helps force the AV logs to flush. (Note the \n at the end of the line.)
	logAVWrapper(AV_LOG_INFO, "libav* should be completely shut down now.\n");
}

void FFmpegDriver::openInputMedia(string inFile, enum AVMediaType type, string inFormat, string inSize, string inChannels, string inRate, string inCodec, string inputAudioChannelMap, bool tryAudio) {
	int ret;
	if(inFile.length() < 1) {
		throw invalid_argument("specified input video/audio file must be a valid input filename");
	}
	logger->info("Opening input media %s...", inFile.c_str());

	AVInputFormat *inputFormat = NULL;
	if(inFormat.length() > 0) {
		inputFormat = av_find_input_format(inFormat.c_str());
		if(!inputFormat) {
			throw invalid_argument("specified input video/audio format could not be resolved");
		}
	}

	MediaInputContext *context = &videoContext;
	if(type == AVMEDIA_TYPE_AUDIO) {
		context = &audioContext;
	}
	if(context->initialized) {
		throw runtime_error("double initialization of media input context!");
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
		av_dict_set(&options, "fragment_size", "512", 0);
		av_dict_set(&options, "timestamps", "mono2abs", 0); // FIXME - should we be using "abs" or "mono2abs"? Can anybody explain this to me?
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
	if(inputAudioChannelMap.length() > 0) {
		if(inputAudioChannelMap == "left") {
			context->inputAudioChannelMap = CHANNELMAP_LEFT_ONLY;
		} else if(inputAudioChannelMap == "right") {
			context->inputAudioChannelMap = CHANNELMAP_RIGHT_ONLY;
		} else {
			throw invalid_argument("invalid inputAudioChannelMap specified!");
		}
	}

	if((ret = avformat_open_input(&context->formatContext, inFile.c_str(), inputFormat, &options)) < 0) {
		logAVErr("input file could not be opened", ret);
		throw runtime_error("input file could not be opened");
	}
	int count = av_dict_count(options);
	if(count) {
		logger->notice("avformat_open_input() rejected %d option(s)!", count);
		char *dictstring;
		if(av_dict_get_string(options, &dictstring, ',', ';') < 0) {
			logger->err("Failed generating dictionary string!");
		} else {
			logger->notice("Dictionary: %s", dictstring);
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
			logger->debug2("Audio Stream open with Time Base: %.08lf (%d/%d) seconds per unit", audioStreamTimeBase, context->audioStream->time_base.num, context->audioStream->time_base.den);
		} catch(exception &e) {
			logger->err("Failed to open audio stream in %s! Exception: %s", inFile.c_str(), e.what());
		}
	}

	if(type == AVMEDIA_TYPE_VIDEO) {
		if(videoContext.videoDecoderContext || audioContext.videoDecoderContext) {
			throw runtime_error("Trying to open a video context, but one is already open?!");
		}
		this->openCodecContext(&context->videoStreamIndex, &context->videoDecoderContext, context->formatContext, AVMEDIA_TYPE_VIDEO);
		context->videoStream = context->formatContext->streams[context->videoStreamIndex];
		videoStreamTimeBase = (double)context->videoStream->time_base.num / (double)context->videoStream->time_base.den;
		logger->debug2("Video Stream open with Time Base: %.08lf (%d/%d) seconds per unit", videoStreamTimeBase, context->videoStream->time_base.num, context->videoStream->time_base.den);

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

	context->initialized = true;

	//// At this point (probably not before) we can assume formatContext.start_time has been populated with meaningful information.
	double formatStartSeconds = (double)context->formatContext->start_time / (double)AV_TIME_BASE;
	if(context->videoDecoderContext) {
		context->videoStreamPTSOffset = (int64_t)((double)(formatStartSeconds) / (double)videoStreamTimeBase);
	}
	if(context->audioDecoderContext) {
		context->audioStreamPTSOffset = (int64_t)((double)(formatStartSeconds) / (double)audioStreamTimeBase);
	}
}

void FFmpegDriver::openOutputMedia(string outFile) {
	int ret;
	if(outFile.length() < 1) {
		throw invalid_argument("specified output video/audio file must be a valid input filename");
	}
	logger->info("Opening output media %s...", outFile.c_str());

	if(outputContext.initialized) {
		throw runtime_error("double initialization of media output context!");
	}

	if((outputContext.multiplexerMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating multiplexer mutex!");
	}

	avformat_alloc_output_context2(&outputContext.formatContext, NULL, NULL, outFile.c_str());
	if(!outputContext.formatContext) {
		throw runtime_error("failed initializing format context for output media!");
	}
	outputContext.outputFormat = outputContext.formatContext->oformat;

	int outputStreamIndex = 0;

	for(MediaInputContext *inputContext : {&videoContext, &audioContext }) {
		MediaInputOutputStreamPair videoPair, audioPair;
		videoPair.in = &inputContext->videoStream;
		videoPair.out = &outputContext.videoStream;
		videoPair.outStreamIndex = &outputContext.videoStreamIndex;
		audioPair.in = &inputContext->audioStream;
		audioPair.out = &outputContext.audioStream;
		audioPair.outStreamIndex = &outputContext.audioStreamIndex;
		for(MediaInputOutputStreamPair streamPair : { videoPair, audioPair }) {
			if(*streamPair.in != NULL) {
				if(*streamPair.out != NULL) {
					throw runtime_error("trying to output two media streams of the same type?");
				}
				AVCodecParameters *inputCodecParameters = (*streamPair.in)->codecpar;
				*streamPair.out = avformat_new_stream(outputContext.formatContext, NULL);
				if(*streamPair.out == NULL) {
					throw runtime_error("failed allocating output stream!");
				}
				ret = avcodec_parameters_copy((*streamPair.out)->codecpar, inputCodecParameters);
				if(ret < 0) {
					throw runtime_error("failed to copy codec context from input stream to output stream!");
				}
				(*streamPair.out)->codecpar->codec_tag = 0; // FIXME - why?
				*streamPair.outStreamIndex = outputStreamIndex;
				outputStreamIndex++;
			}
		}
	}

	if(outputContext.videoStream == NULL) {
		throw runtime_error("Tried to open an output video file, but we couldn't copy a video stream from the input!");
	}
	if(outputContext.audioStream == NULL) {
		logger->warning("NO AUDIO STREAM IS BEING COPIED TO THE OUTPUT!");
	}

	av_dump_format(outputContext.formatContext, 0, outFile.c_str(), 1);
	if(!(outputContext.outputFormat->flags & AVFMT_NOFILE)) {
		ret = avio_open(&outputContext.formatContext->pb, outFile.c_str(), AVIO_FLAG_WRITE);
		if(ret < 0) {
			throw runtime_error("failed opening output file for output media!");
		}
	}
	ret = avformat_write_header(outputContext.formatContext, NULL);
	if(ret < 0) {
		throw runtime_error("failed writing output media header to file!");
	}

	outputContext.initialized = true;
}

void FFmpegDriver::setVideoCaptureWorkerPool(WorkerPool *workerPool) {
	videoCaptureWorkerPool = workerPool;
}

void FFmpegDriver::openCodecContext(int *streamIndex, AVCodecContext **decoderContext, AVFormatContext *myFormatContext, enum AVMediaType type) {
	int myStreamIndex, ret;
	AVStream *stream;
	AVCodec *decoder = NULL;
	AVDictionary *options = NULL;

	if((myStreamIndex = av_find_best_stream(myFormatContext, type, -1, -1, NULL, 0)) < 0) {
		logger->warning("failed to find %s stream in input file", av_get_media_type_string(type));
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
	YerFace_MutexLock(videoFrameBufferMutex);
	VideoFrame result;
	logger->debug4("getNextVideoFrame() current readyVideoFrameBuffer.size() is %lu", readyVideoFrameBuffer.size());
	if(readyVideoFrameBuffer.size() > 0) {
		result = readyVideoFrameBuffer.back();
		readyVideoFrameBuffer.pop_back();
	} else {
		YerFace_MutexUnlock(videoFrameBufferMutex);
		throw runtime_error("getNextVideoFrame() was called, but no video frames are pending");
	}
	YerFace_MutexUnlock(videoFrameBufferMutex);
	return result;
}

// Sets *videoFrame to an invalid VideoFrame if a frame is not (yet) available. Otherwise sets *videoFrame to the next valid VideoFrame.
// Returns TRUE if the demuxer is still running, FALSE otherwise.
bool FFmpegDriver::pollForNextVideoFrame(VideoFrame *videoFrame) {
	bool ret = true;
	YerFace_MutexLock(demuxerThreadMutex);
	ret = demuxerThreadRunning;
	YerFace_MutexUnlock(demuxerThreadMutex);

	YerFace_MutexLock(videoFrameBufferMutex);
	if(getIsVideoFrameBufferEmpty()) {
		VideoFrame invalid;
		invalid.valid = false;
		invalid.frameBacking = NULL;
		*videoFrame = invalid;
	} else {
		*videoFrame = getNextVideoFrame();
	}
	YerFace_MutexUnlock(videoFrameBufferMutex);

	return ret;
}

void FFmpegDriver::releaseVideoFrame(VideoFrame videoFrame) {
	YerFace_MutexLock(videoFrameBufferMutex);
	videoFrame.frameBacking->inUse = false;
	YerFace_MutexUnlock(videoFrameBufferMutex);
}

void FFmpegDriver::registerAudioFrameCallback(AudioFrameCallback audioFrameCallback) {
	YerFace_MutexLock(demuxerThreadMutex);

	AudioFrameHandler *handler = new AudioFrameHandler();
	handler->drained = false;
	handler->audioFrameCallback = audioFrameCallback;
	handler->resampler.swrContext = NULL;
	handler->resampler.audioFrameBackings.clear();
	audioFrameHandlers.push_back(handler);

	YerFace_MutexUnlock(demuxerThreadMutex);
}

void FFmpegDriver::logAVErr(string msg, int err) {
	char errbuf[128];
	av_strerror(err, errbuf, 128);
	logger->err("%s AVERROR: (%d) %s", msg.c_str(), err, errbuf);
}

VideoFrameBacking *FFmpegDriver::getNextAvailableVideoFrameBacking(void) {
	YerFace_MutexLock(videoFrameBufferMutex);
	VideoFrameBacking *myBacking = NULL;
	unsigned int availableBackings = 0;
	for(VideoFrameBacking *backing : allocatedVideoFrameBackings) {
		if(!backing->inUse) {
			availableBackings++;
			if(myBacking == NULL) {
				backing->inUse = true;
				myBacking = backing;
			}
		}
	}
	logger->debug4("getNextAvailableVideoFrameBacking() total backings: %lu, available backings: %u", allocatedVideoFrameBackings.size(), availableBackings);
	if(myBacking == NULL) {
		logger->notice("Out of spare frames in the video frame buffer! Allocating a new one.");
		myBacking = allocateNewVideoFrameBacking();
		myBacking->inUse = true;
	}
	YerFace_MutexUnlock(videoFrameBufferMutex);
	return myBacking;
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

bool FFmpegDriver::decodePacket(MediaInputContext *context, int streamIndex, bool drain) {
	int ret;

	if(context->videoStream != NULL && streamIndex == context->videoStreamIndex) {
		logger->debug3("Got video %s. Sending to codec...", drain ? "flush call" : "packet");
		if(avcodec_send_packet(context->videoDecoderContext, drain ? NULL : &context->packet) < 0) {
			logger->err("Error decoding video frame");
			return false;
		}

		while(avcodec_receive_frame(context->videoDecoderContext, context->frame) == 0) {
			if(context->frame->width != width || context->frame->height != height || context->frame->format != pixelFormat) {
				logger->crit("We cannot handle runtime changes to video width, height, or pixel format. Unfortunately, the width, height or pixel format of the input video has changed: old [ width = %d, height = %d, format = %s ], new [ width = %d, height = %d, format = %s ]", width, height, av_get_pix_fmt_name(pixelFormat), context->frame->width, context->frame->height, av_get_pix_fmt_name((AVPixelFormat)context->frame->format));
				av_frame_unref(context->frame);
				return false;
			}

			context->frameNumber++;

			VideoFrame videoFrame;
			videoFrame.timestamp = resolveFrameTimestamp(context, AVMEDIA_TYPE_VIDEO);
			videoFrame.timestamp.frameNumber = context->frameNumber;
			videoFrame.frameBacking = getNextAvailableVideoFrameBacking();
			videoFrame.valid = true;

			YerFace_MutexLock(videoStreamMutex);
			newestVideoFrameTimestamp = videoFrame.timestamp.startTimestamp;
			newestVideoFrameEstimatedEndTimestamp = videoFrame.timestamp.estimatedEndTimestamp;
			YerFace_MutexUnlock(videoStreamMutex);
			logger->debug4("Inserted a VideoFrame with timestamps: %.04lf - (estimated) %.04lf", videoFrame.timestamp.startTimestamp, videoFrame.timestamp.estimatedEndTimestamp);

			sws_scale(swsContext, context->frame->data, context->frame->linesize, 0, height, videoFrame.frameBacking->frameBGR->data, videoFrame.frameBacking->frameBGR->linesize);
			videoFrame.frameCV = Mat(height, width, CV_8UC3, videoFrame.frameBacking->frameBGR->data[0]);

			YerFace_MutexLock(videoFrameBufferMutex);
			if(lowLatency) {
				int dropCount = 0;
				while(readyVideoFrameBuffer.size() > 0) {
					releaseVideoFrame(readyVideoFrameBuffer.back());
					readyVideoFrameBuffer.pop_back();
					dropCount++;
				}
				if(dropCount) {
					logger->info("Dropped %d frame(s)!", dropCount);
				}
			}
			readyVideoFrameBuffer.push_front(videoFrame);
			YerFace_MutexUnlock(videoFrameBufferMutex);

			av_frame_unref(context->frame);
		}
	}
	if(context->audioStream != NULL && streamIndex == context->audioStreamIndex) {
		logger->debug3("Got audio %s. Sending to codec...", drain ? "flush call" : "packet");
		if((ret = avcodec_send_packet(context->audioDecoderContext, drain ? NULL : &context->packet)) < 0) {
			logAVErr("Sending packet to audio codec.", ret);
			return false;
		}

		while(avcodec_receive_frame(context->audioDecoderContext, context->frame) == 0) {
			FrameTimestamps timestamps = resolveFrameTimestamp(context, AVMEDIA_TYPE_AUDIO);

			YerFace_MutexLock(audioStreamMutex);
			newestAudioFrameTimestamp = timestamps.startTimestamp;
			newestAudioFrameEstimatedEndTimestamp = timestamps.estimatedEndTimestamp;
			YerFace_MutexUnlock(audioStreamMutex);
			for(AudioFrameHandler *handler : audioFrameHandlers) {
				if(handler->resampler.swrContext == NULL) {
					int64_t inputChannelLayout = context->audioStream->codecpar->channel_layout;
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
					if(context->inputAudioChannelMap != CHANNELMAP_NONE) {
						if(context->inputAudioChannelMap == CHANNELMAP_LEFT_ONLY) {
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
				audioFrameBacking.timestamp = timestamps.startTimestamp;
				audioFrameBacking.bufferArray = NULL;
				//bufferSamples represents the expected number of samples produced by swr_convert() *PER CHANNEL*
				audioFrameBacking.bufferSamples = (int)av_rescale_rnd(swr_get_delay(handler->resampler.swrContext, context->audioStream->codecpar->sample_rate) + context->frame->nb_samples, handler->audioFrameCallback.sampleRate, context->audioStream->codecpar->sample_rate, AV_ROUND_UP);

				if(av_samples_alloc_array_and_samples(&audioFrameBacking.bufferArray, &bufferLineSize, handler->resampler.numChannels, audioFrameBacking.bufferSamples, handler->audioFrameCallback.sampleFormat, 1) < 0) {
					throw runtime_error("Failed allocating audio buffer!");
				}

				if((audioFrameBacking.audioSamples = swr_convert(handler->resampler.swrContext, audioFrameBacking.bufferArray, audioFrameBacking.bufferSamples, (const uint8_t **)context->frame->data, context->frame->nb_samples)) < 0) {
					throw runtime_error("Failed running swr_convert() for audio resampling");
				}

				audioFrameBacking.audioBytes = audioFrameBacking.audioSamples * handler->resampler.numChannels * av_get_bytes_per_sample(handler->audioFrameCallback.sampleFormat);
				
				handler->resampler.audioFrameBackings.push_front(audioFrameBacking);
				logger->debug3("Pushed a resampled audio frame for handler. Frame queue depth is %lu", handler->resampler.audioFrameBackings.size());
			}
			av_frame_unref(context->frame);
		}
	}

	return true;
}

void FFmpegDriver::rollDemuxerThread(void) {
	YerFace_MutexLock(demuxerThreadMutex);
	if(demuxerThread != NULL) {
		YerFace_MutexUnlock(demuxerThreadMutex);
		throw runtime_error("rollDemuxerThread was called, but demuxer was already set rolling!");
	}
	demuxerThreadRunning = true;
	demuxerThread = SDL_CreateThread(FFmpegDriver::runOuterDemuxerLoop, "Demuxer", (void *)this);
	if(demuxerThread == NULL) {
		YerFace_MutexUnlock(demuxerThreadMutex);
		throw runtime_error("Failed starting thread!");
	}
	YerFace_MutexUnlock(demuxerThreadMutex);
}

void FFmpegDriver::destroyDemuxerThread(void) {
	YerFace_MutexLock(audioStreamMutex);
	audioCallbacksOkay = false;
	YerFace_MutexUnlock(audioStreamMutex);

	YerFace_MutexLock(demuxerThreadMutex);
	demuxerThreadRunning = false;
	videoContext.demuxerDraining = true;
	audioContext.demuxerDraining = true;

	YerFace_MutexUnlock(demuxerThreadMutex);
	if(demuxerThread != NULL) {
		SDL_WaitThread(demuxerThread, NULL);
	}

	SDL_DestroyMutex(demuxerThreadMutex);
}

int FFmpegDriver::runOuterDemuxerLoop(void *ptr) {
	FFmpegDriver *driver = (FFmpegDriver *)ptr;
	try {
		driver->logger->debug1("Media Demuxer Thread alive!");
		if(!driver->getIsAudioInputPresent()) {
			driver->logger->notice("NO AUDIO STREAM IS PRESENT! We can still proceed, but mouth shapes won't be informed by audible speech.");
		}
		int ret = driver->innerDemuxerLoop();
		driver->logger->debug1("Media Demuxer Thread quitting...");
		return ret;
	} catch(exception &e) {
		driver->logger->emerg("Uncaught exception in worker thread: %s\n", e.what());
		driver->status->setEmergency();
	}
	return 1;
}

int FFmpegDriver::innerDemuxerLoop(void) {
	bool blockedWarning = false;

	YerFace_MutexLock(demuxerThreadMutex);
	while(demuxerThreadRunning) {
		// logger->debug4("Demuxer thread top-of-loop.");
		// Handle pausing
		if(status->getIsPaused() && status->getIsRunning()) {
			YerFace_MutexUnlock(demuxerThreadMutex);
			SDL_Delay(100);
			YerFace_MutexLock(demuxerThreadMutex);
			continue;
		}

		// Handle blocking
		if(getIsAllocatedVideoFrameBackingsFull()) {
			if(!blockedWarning) {
				logger->warning("Demuxer Thread is BLOCKED because our internal frame buffer is full. If this happens a lot, consider some tuning.");
				blockedWarning = true;
			}
			YerFace_MutexUnlock(demuxerThreadMutex);
			SDL_Delay(10);
			YerFace_MutexLock(demuxerThreadMutex);
			continue;
		} else {
			blockedWarning = false;
		}

		// Optionally balance demuxer pumping to keep video and audio in sync
		bool pumpVideo = true, pumpAudio = true;
		if(videoContext.formatContext != NULL && audioContext.formatContext != NULL) {
			double videoTimestamp, audioTimestamp;
			YerFace_MutexLock(videoStreamMutex);
			videoTimestamp = newestVideoFrameEstimatedEndTimestamp;
			YerFace_MutexUnlock(videoStreamMutex);
			YerFace_MutexLock(audioStreamMutex);
			audioTimestamp = newestAudioFrameEstimatedEndTimestamp;
			YerFace_MutexUnlock(audioStreamMutex);
			if(videoTimestamp >= audioTimestamp) {
				pumpVideo = false;
			} else {
				pumpAudio = false;
			}
			logger->debug3("Balanced Demuxer Pumping... videoTimestamp: %lf, audioTimestamp: %lf, pumpVideo: %s, pumpAudio: %s", videoTimestamp, audioTimestamp, pumpVideo ? "TRUE" : "FALSE", pumpAudio ? "TRUE" : "FALSE");
		}
		
		// Handle video
		if(pumpVideo && videoContext.videoStream != NULL) {
			if(!getIsVideoDraining()) {
				logger->debug3("Pumping VIDEO stream.");
				pumpDemuxer(&videoContext, AVMEDIA_TYPE_VIDEO);
				logger->debug3("Finished pumping VIDEO stream.");
			}
		}

		// Handle audio
		if(pumpAudio && getIsAudioInputPresent()) {
			MediaInputContext *myAudioContext = &videoContext;
			if(audioContext.audioStream != NULL) {
				myAudioContext = &audioContext;
			}
			if(!getIsAudioDraining()) {
				logger->debug3("Pumping AUDIO stream.");
				pumpDemuxer(myAudioContext, AVMEDIA_TYPE_AUDIO);
				logger->debug3("Finished pumping AUDIO stream.");
			}

			flushAudioHandlers(getIsAudioDraining());
		}

		// Handle thread wakeups
		if(videoCaptureWorkerPool != NULL && !getIsVideoFrameBufferEmpty()) {
			logger->debug4("Sending a signal to the Video Capture thread!");
			videoCaptureWorkerPool->sendWorkerSignal();
		}

		// Handle draining
		if(getIsVideoDraining() && getIsAudioDraining()) {
			logger->info("Draining Demuxers complete. Demuxer thread terminating...");
			demuxerThreadRunning = false;
		}

		// Sleep
		if(demuxerThreadRunning) {
			YerFace_MutexUnlock(demuxerThreadMutex);
			// logger->debug4("Demuxer thread going to sleep.");
			SDL_Delay(0); //All we want to do here is relinquish our execution thread. We still want to get it back asap.
			// logger->debug4("Demuxer thread awake!");
			YerFace_MutexLock(demuxerThreadMutex);
		}
	}

	// Notify audio handlers that audio is drained.
	if(getIsAudioInputPresent()) {
		for(AudioFrameHandler *handler : audioFrameHandlers) {
			if(handler->drained) {
				throw runtime_error("Audio handler drained more than once?!");
			}
			if(handler->audioFrameCallback.isDrainedCallback != NULL) {
				handler->audioFrameCallback.isDrainedCallback(handler->audioFrameCallback.userdata);
			}
			handler->drained = true;
		}
	}

	YerFace_MutexUnlock(demuxerThreadMutex);
	return 0;
}

void FFmpegDriver::pumpDemuxer(MediaInputContext *context, enum AVMediaType type) {
	Uint32 pumpStart = SDL_GetTicks();
	int ret;
	av_init_packet(&context->packet);
	try {
		Uint32 readStart = SDL_GetTicks();
		ret = av_read_frame(context->formatContext, &context->packet);
		Uint32 readEnd = SDL_GetTicks();
		if(readEnd - readStart > 10 && lowLatency) {
			logger->warning("av_read_frame() %s took longer than expected! (%.04lfs) This will cause all sorts of problems.", type == AVMEDIA_TYPE_VIDEO ? "VIDEO" : "AUDIO", ((double)readEnd - (double)readStart) / (double)1000.0);
		}
		if(ret < 0) {
			logger->info("Demuxer thread encountered End of Stream! Going into draining mode...");
			SDL_mutex *streamMutex = videoStreamMutex;
			if(type == AVMEDIA_TYPE_AUDIO) {
				streamMutex = audioStreamMutex;
			}
			YerFace_MutexLock(streamMutex);
			context->demuxerDraining = true;
			YerFace_MutexUnlock(streamMutex);

			if(context->videoStream != NULL) {
				decodePacket(context, context->videoStreamIndex, true);
			}
			if(context->audioStream != NULL) {
				decodePacket(context, context->audioStreamIndex, true);
			}
		} else {
			if(!decodePacket(context, context->packet.stream_index, false)) {
				logger->err("Demuxer thread encountered a corrupted packet in the stream!");
			}

			if(outputContext.initialized) {
				YerFace_MutexLock(outputContext.multiplexerMutex);
				AVStream *in = NULL, *out = NULL;
				int outputStreamIndex = -1;
				int64_t *ptsOffset = NULL;
				int64_t *lastPTS = NULL, *lastDTS = NULL;
				if(context->videoStream != NULL && outputContext.videoStream != NULL && context->packet.stream_index == context->videoStreamIndex) {
					in = context->videoStream;
					out = outputContext.videoStream;
					outputStreamIndex = outputContext.videoStreamIndex;
					ptsOffset = &context->videoStreamPTSOffset;
					lastPTS = &context->videoMuxLastPTS;
					lastDTS = &context->videoMuxLastDTS;
				} else if(context->audioStream != NULL && outputContext.audioStream != NULL && context->packet.stream_index == context->audioStreamIndex) {
					in = context->audioStream;
					out = outputContext.audioStream;
					outputStreamIndex = outputContext.audioStreamIndex;
					ptsOffset = &context->audioStreamPTSOffset;
					lastPTS = &context->audioMuxLastPTS;
					lastDTS = &context->audioMuxLastDTS;
				}
				if(in != NULL && out != NULL) {
					context->packet.stream_index = outputStreamIndex;
					logger->debug4("INPUT %s PACKET: [ time_base: %d / %d, pts: %ld, dts: %ld, duration: %ld ]", context->packet.stream_index == context->videoStreamIndex ? "VIDEO" : "AUDIO", in->time_base.num, in->time_base.den, context->packet.pts, context->packet.dts, context->packet.duration);
					context->packet.pts = av_rescale_q_rnd(
						applyPTSOffset(context->packet.pts, *ptsOffset),
						in->time_base,
						out->time_base,
						(AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
					context->packet.dts = av_rescale_q_rnd(
						applyPTSOffset(context->packet.dts, *ptsOffset),
						in->time_base,
						out->time_base,
						(AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
					context->packet.duration = av_rescale_q(context->packet.duration, in->time_base, out->time_base);
					context->packet.pos = -1;
					logger->debug4("OUTPUT %s PACKET: [ time_base: %d / %d, pts: %ld, dts: %ld, duration: %ld ]", context->packet.stream_index == context->videoStreamIndex ? "VIDEO" : "AUDIO", out->time_base.num, out->time_base.den, context->packet.pts, context->packet.dts, context->packet.duration);
					if(context->packet.pts <= *lastPTS || context->packet.dts <= *lastDTS) {
						logger->crit("Trying to multiplex output media, but %s packet appeared out of order or with bad timestamps! PACKET LOST!", context->packet.stream_index == context->videoStreamIndex ? "VIDEO" : "AUDIO");
					} else {
						*lastPTS = context->packet.pts;
						*lastDTS = context->packet.dts;
						ret = av_interleaved_write_frame(outputContext.formatContext, &context->packet);
						if(ret < 0) {
							YerFace_MutexUnlock(outputContext.multiplexerMutex);
							throw runtime_error("Failed during packet multiplexing!");
						}
					}
				}
				YerFace_MutexUnlock(outputContext.multiplexerMutex);
			}

			av_packet_unref(&context->packet);
		}
	} catch(exception &e) {
		logger->emerg("Caught Exception: %s", e.what());
		status->setEmergency();
		demuxerThreadRunning = false;
	}

	Uint32 pumpEnd = SDL_GetTicks();
	if(pumpEnd - pumpStart > 10 && lowLatency) {
		logger->warning("Pumping %s took longer than expected! (%.04lfs) This will cause all sorts of problems.", type == AVMEDIA_TYPE_VIDEO ? "VIDEO" : "AUDIO", ((double)pumpEnd - (double)pumpStart) / (double)1000.0);
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
			// logger->debug3("======== AUDIO FRAME TIMESTAMP: %lf, VIDEO FRAME END TIMESTAMP: %lf", nextFrame.timestamp, myNewestVideoFrameEstimatedEndTimestamp);
			if(nextFrame.timestamp < myNewestVideoFrameEstimatedEndTimestamp || draining || lowLatency) {
				if(callbacksOkay) {
					// logger->debug3("======== FIRING AUDIO CALLBACK (0x%lX) w/Timestamp %lf", (uint64_t)handler->audioFrameCallback.userdata, nextFrame.timestamp);
					handler->audioFrameCallback.audioFrameCallback(handler->audioFrameCallback.userdata, nextFrame.bufferArray[0], nextFrame.audioSamples, nextFrame.audioBytes, nextFrame.timestamp);
				}
				av_freep(&nextFrame.bufferArray[0]);
				av_freep(&nextFrame.bufferArray);
				handler->resampler.audioFrameBackings.pop_back();
			} else {
				logger->debug3("======== HOLDING AUDIO FRAME FOR LATER");
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
	if(!status->getIsRunning()) {
		return true;
	}
	if(!getIsAudioInputPresent()) {
		return true;
	}
	YerFace_MutexLock(videoStreamMutex);
	YerFace_MutexLock(audioStreamMutex);
	bool ret = false;
	if(audioContext.audioStream != NULL) {
		ret = audioContext.demuxerDraining;
	} else if(videoContext.audioStream != NULL) {
		ret = videoContext.demuxerDraining;
	}
	YerFace_MutexUnlock(audioStreamMutex);
	YerFace_MutexUnlock(videoStreamMutex);
	return ret;
}

bool FFmpegDriver::getIsVideoDraining(void) {
	if(!status->getIsRunning()) {
		return true;
	}
	YerFace_MutexLock(videoStreamMutex);
	bool ret = videoContext.demuxerDraining;
	YerFace_MutexUnlock(videoStreamMutex);
	return ret;
}

FrameTimestamps FFmpegDriver::resolveFrameTimestamp(MediaInputContext *context, enum AVMediaType type) {
	FrameTimestamps timestamps;
	double *timeBase = &videoStreamTimeBase;
	int64_t *ptsOffset = &context->videoStreamPTSOffset;
	if(type != AVMEDIA_TYPE_VIDEO) {
		timeBase = &audioStreamTimeBase;
		ptsOffset = &context->audioStreamPTSOffset;
	}

	int64_t correctedPTS = applyPTSOffset(context->frame->pts, *ptsOffset);
	timestamps.startTimestamp = (double)correctedPTS * *timeBase;
	double estimatedDuration = context->frame->pkt_duration * *timeBase;
	if(estimatedDuration <= 0.0) {
		logger->warning("We're getting bad frame durations within the %s stream. If this happens a lot it will be a problem!", type == AVMEDIA_TYPE_VIDEO ? "VIDEO" : "AUDIO");
		estimatedDuration = 0.001;
	}
	timestamps.estimatedEndTimestamp = timestamps.startTimestamp + estimatedDuration;
	logger->debug3("%s Frame Timestamps: startTimestamp %.04lf, estimatedEndTimestamp: %.04lf (original pts: %ld, ptsOffset: %ld, correctedPTS: %ld)", type == AVMEDIA_TYPE_VIDEO ? "VIDEO" : "AUDIO", timestamps.startTimestamp, timestamps.estimatedEndTimestamp, context->frame->pts, *ptsOffset, correctedPTS);

	return timestamps;
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
		logger->info("%s %s AVOption: %s (%s)", depth.c_str(), c->class_name, opt->name, opt->help);
	}
	const AVClass *childobjclass = NULL;
	while((childobjclass = av_opt_child_class_next(c, childobjclass)) != NULL) {
		void *childobj = &childobjclass;
		recursivelyListAllAVOptions(childobj, "  " + depth);
	}
}

bool FFmpegDriver::getIsAllocatedVideoFrameBackingsFull(void) {
	bool isFull = true;
	YerFace_MutexLock(videoFrameBufferMutex);
	for(VideoFrameBacking *backing : allocatedVideoFrameBackings) {
		if(!backing->inUse) {
			isFull = false;
			break;
		}
	}
	YerFace_MutexUnlock(videoFrameBufferMutex);
	return isFull;
}

int64_t FFmpegDriver::applyPTSOffset(int64_t pts, int64_t offset) {
	int64_t newPTS = pts - offset;
	if(newPTS < 0) {
		logger->notice("PTS/DTS correction resulted in a negative PTS/DTS!");
		newPTS = 0;
	}
	return newPTS;
}

void FFmpegDriver::logAVCallback(void *ptr, int level, const char *fmt, va_list args) {
	if(level < YERFACE_AVLOG_LEVELMAP_MIN || level > YERFACE_AVLOG_LEVELMAP_MAX) {
		return;
	}

	YerFace_MutexLock(avLoggerMutex);

	LogMessageSeverity severity = LOG_SEVERITY_INFO;
	if(level < YERFACE_AVLOG_LEVELMAP_ALERT) {
		severity = LOG_SEVERITY_ALERT;
	} else if(level < YERFACE_AVLOG_LEVELMAP_CRIT) {
		severity = LOG_SEVERITY_CRIT;
	} else if(level < YERFACE_AVLOG_LEVELMAP_ERR) {
		severity = LOG_SEVERITY_ERR;
	} else if(level < YERFACE_AVLOG_LEVELMAP_WARNING) {
		severity = LOG_SEVERITY_WARNING;
	} else if(level < YERFACE_AVLOG_LEVELMAP_INFO) {
		severity = LOG_SEVERITY_INFO;
	}

	//Delightfully, libav likes to present partial log lines and demarcate them with newlines.
	static int lastSeverity = -1;
	static string logBuffer = "";
	static string previousLogLine = "";
	static unsigned long previousLogSuppressionCount = 0;

	//If the new log line content is obviously "for" a different log line, flush the buffer before proceeding.
	if(lastSeverity != (int)severity) {
		if(logBuffer.length() > 0) {
			avLogger->err("UNEXPECTED END OF AVLOG CONTENT!");
			logBuffer = Utilities::stringTrimRight(logBuffer);
			avLogger->log((LogMessageSeverity)lastSeverity, "%s", logBuffer.c_str());
			previousLogLine = logBuffer;
			previousLogSuppressionCount = 0;
			logBuffer = "";
		}
		lastSeverity = severity;
	}

	//Build up the log line.
	if(logBuffer == "") {
		char intermediateClassPrefixBufferA[512];
		char intermediateClassPrefixBufferB[512];
		intermediateClassPrefixBufferA[0] = '\0';
		intermediateClassPrefixBufferB[0] = '\0';
		AVClass *avc = ptr ? *(AVClass **)ptr : NULL;
		if(avc != NULL) {
			if(avc->parent_log_context_offset) {
				AVClass** parent = *(AVClass ***) (((uint8_t *)ptr) + avc->parent_log_context_offset);
				if(parent && *parent) {
					snprintf(intermediateClassPrefixBufferA, sizeof(intermediateClassPrefixBufferA), "[%s @ %p] ", (*parent)->item_name(parent), parent);
				}
			}
			snprintf(intermediateClassPrefixBufferB, sizeof(intermediateClassPrefixBufferB), "[%s @ %p] ", avc->item_name(ptr), ptr);
		}
		logBuffer += (string)intermediateClassPrefixBufferA + (string)intermediateClassPrefixBufferB;
	}
	char intermediateBuffer[512];
	vsnprintf(intermediateBuffer, sizeof(intermediateBuffer), fmt, args);
	logBuffer += (string)intermediateBuffer;
	if(logBuffer.length() > 0) {
		bool suppress = false;
		if(previousLogLine == logBuffer) {
			if(previousLogSuppressionCount < 100) {
				suppress = true;
				previousLogSuppressionCount++;
				logBuffer = "";
			}
		}
		if(!suppress) {
			if(previousLogSuppressionCount > 0) {
				avLogger->log(LOG_SEVERITY_INFO, "Suppressed duplicate log entry %lu times(s).", previousLogSuppressionCount);
				previousLogSuppressionCount = 0;
			}
			if(logBuffer.back() == '\n') {
				avLogger->log((LogMessageSeverity)lastSeverity, "%s", Utilities::stringTrimRight(logBuffer).c_str());
				previousLogLine = logBuffer;
				previousLogSuppressionCount = 0;
				logBuffer = "";
			}
		}
	}

	YerFace_MutexUnlock(avLoggerMutex);
}

void FFmpegDriver::logAVWrapper(int level, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	logAVCallback(NULL, level, fmt, args);
	va_end(args);
}

Logger *FFmpegDriver::avLogger = new Logger("AVLib");
SDL_mutex *FFmpegDriver::avLoggerMutex = SDL_CreateMutex();

}; //namespace YerFace
