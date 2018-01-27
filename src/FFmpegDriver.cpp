
#include "FFmpegDriver.hpp"

#include "Utilities.hpp"

#include <exception>
#include <stdexcept>

using namespace std;

namespace YerFace {

FFmpegDriver::FFmpegDriver(FrameDerivatives *myFrameDerivatives, string myInputFilename, bool myFrameDrop) {
	int ret;
	logger = new Logger("FFmpegDriver");

	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	inputFilename = myInputFilename;
	if(inputFilename.length() < 1) {
		throw invalid_argument("inputFilename must be a valid input filename");
	}
	frameDrop = myFrameDrop;

	formatContext = NULL;
	videoDecoderContext = NULL;
	videoStream = NULL;
	audioDecoderContext = NULL;
	audioStream = NULL;
	frame = NULL;
	swsContext = NULL;

	av_log_set_flags(AV_LOG_SKIP_REPEATED);
	av_log_set_level(AV_LOG_INFO);
	av_log_set_callback(av_log_default_callback);
	avdevice_register_all();
	av_register_all();
	avformat_network_init();

	logger->info("Opening media file %s...", inputFilename.c_str());

	if((ret = avformat_open_input(&formatContext, inputFilename.c_str(), NULL, NULL)) < 0) {
		logAVErr("inputFilename could not be opened", ret);
		throw runtime_error("inputFilename could not be opened");
	}

	if((ret = avformat_find_stream_info(formatContext, NULL)) < 0) {
		logAVErr("failed finding input stream information for inputFilename", ret);
		throw runtime_error("failed finding input stream information for inputFilename");
	}

	try {
		this->openCodecContext(&audioStreamIndex, &audioDecoderContext, formatContext, AVMEDIA_TYPE_AUDIO);
		audioStream = formatContext->streams[audioStreamIndex];
		audioStreamTimeBase = (double)audioStream->time_base.num / (double)audioStream->time_base.den;
		logger->verbose("Audio Stream open with Time Base: %.08lf (%d/%d) seconds per unit", audioStreamTimeBase, audioStream->time_base.num, audioStream->time_base.den);
	} catch(exception &e) {
		logger->warn("Failed to open audio stream! We can still proceed, but mouth shapes won't be informed by audible speech.");
	}

	this->openCodecContext(&videoStreamIndex, &videoDecoderContext, formatContext, AVMEDIA_TYPE_VIDEO);
	videoStream = formatContext->streams[videoStreamIndex];
	videoStreamTimeBase = (double)videoStream->time_base.num / (double)videoStream->time_base.den;
	logger->verbose("Video Stream open with Time Base: %.08lf (%d/%d) seconds per unit", videoStreamTimeBase, videoStream->time_base.num, videoStream->time_base.den);

	width = videoDecoderContext->width;
	height = videoDecoderContext->height;
	pixelFormat = videoDecoderContext->pix_fmt;
	if((videoDestBufSize = av_image_alloc(videoDestData, videoDestLineSize, width, height, pixelFormat, 1)) < 0) {
		throw runtime_error("failed allocating memory for decoded frame");
	}

	av_dump_format(formatContext, 0, inputFilename.c_str(), 0);

	pixelFormatBacking = AV_PIX_FMT_BGR24;
	if((swsContext = sws_getContext(width, height, pixelFormat, width, height, pixelFormatBacking, SWS_BICUBIC, NULL, NULL, NULL)) == NULL) {
		throw runtime_error("failed creating software scaling context");
	}

	if(!(frame = av_frame_alloc())) {
		throw runtime_error("failed allocating frame");
	}

	for(int i = 0; i < YERFACE_INITIAL_BACKING_FRAMES; i++) {
		allocateNewFrameBacking();
	}

	if((videoFrameBufferMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating video frame buffer mutex!");
	}
	initializeDemuxerThread();

	logger->debug("FFmpegDriver object constructed and ready to go! Frame Drop is %s.", frameDrop ? "ENABLED" : "DISABLED");
}

FFmpegDriver::~FFmpegDriver() {
	logger->debug("FFmpegDriver object destructing...");
	destroyDemuxerThread();
	SDL_DestroyMutex(videoFrameBufferMutex);
	avcodec_free_context(&videoDecoderContext);
	avformat_close_input(&formatContext);
	av_free(videoDestData[0]);
	av_frame_free(&frame);
	for(VideoFrameBacking *backing : allocatedFrameBackings) {
		av_frame_free(&backing->frameBGR);
		av_free(backing->buffer);
		delete backing;
	}
	delete logger;
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

bool FFmpegDriver::waitForNextVideoFrame(VideoFrame *videoFrame) {
	YerFace_MutexLock(demuxerMutex);
	while(getIsFrameBufferEmpty()) {
		if(!demuxerRunning) {
			YerFace_MutexUnlock(demuxerMutex);
			return false;
		}

		//Wait for the demuxer thread to generate more frames. In practice I have never actually seen this happen.
		YerFace_MutexUnlock(demuxerMutex);
		logger->warn("======== Caller is trapped in an expensive polling loop! ========");
		SDL_Delay(50);
		YerFace_MutexLock(demuxerMutex);
	}
	*videoFrame = getNextVideoFrame();
	YerFace_MutexUnlock(demuxerMutex);
	return true;
}

bool FFmpegDriver::getIsFrameBufferEmpty(void) {
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
			logger->warn("Dropped %d frame(s)!", dropCount);
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

void FFmpegDriver::releaseVideoFrame(VideoFrame videoFrame) {
	YerFace_MutexLock(videoFrameBufferMutex);
	videoFrame.frameBacking->inUse = false;
	YerFace_MutexUnlock(videoFrameBufferMutex);
}

void FFmpegDriver::logAVErr(String msg, int err) {
	char errbuf[128];
	av_strerror(err, errbuf, 128);
	logger->error("%s AVERROR: (%d) %s", msg.c_str(), err, errbuf);
}

VideoFrameBacking *FFmpegDriver::getNextAvailableVideoFrameBacking(void) {
	YerFace_MutexLock(videoFrameBufferMutex);
	for(VideoFrameBacking *backing : allocatedFrameBackings) {
		if(!backing->inUse) {
			backing->inUse = true;
			YerFace_MutexUnlock(videoFrameBufferMutex);
			return backing;
		}
	}
	logger->warn("Out of spare frames in the frame buffer! Allocating a new one.");
	VideoFrameBacking *newBacking = allocateNewFrameBacking();
	newBacking->inUse = true;
	YerFace_MutexUnlock(videoFrameBufferMutex);
	return newBacking;
}

VideoFrameBacking *FFmpegDriver::allocateNewFrameBacking(void) {
	VideoFrameBacking *backing = new VideoFrameBacking();
	backing->inUse = false;
	if(!(backing->frameBGR = av_frame_alloc())) {
		throw runtime_error("failed allocating backing frame");
	}
	int bufferSize = av_image_get_buffer_size(pixelFormatBacking, width, height, 1);
	if((backing->buffer = (uint8_t *)av_malloc(bufferSize*sizeof(uint8_t))) == NULL) {
		throw runtime_error("failed allocating buffer for backing frame");
	}
	if(av_image_fill_arrays(backing->frameBGR->data, backing->frameBGR->linesize, backing->buffer, pixelFormatBacking, width, height, 1) < 0) {
		throw runtime_error("failed assigning buffer for backing frame");
	}
	backing->frameBGR->width = width;
	backing->frameBGR->height = height;
	backing->frameBGR->format = pixelFormat;
	allocatedFrameBackings.push_front(backing);
	return backing;
}

bool FFmpegDriver::decodePacket(const AVPacket *packet, int streamIndex) {
	int ret;
	if(streamIndex == videoStreamIndex) {
		logger->verbose("Got video %s. Sending to codec...", packet ? "packet" : "flush call");
		if(avcodec_send_packet(videoDecoderContext, packet) < 0) {
			logger->warn("Error decoding video frame");
			return false;
		}

		while(avcodec_receive_frame(videoDecoderContext, frame) == 0) {
			logger->verbose("Received decoded video frame with timestamp %.04lf (%ld units)!", frame->pts * videoStreamTimeBase, frame->pts);
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
	} else if(audioStream != NULL && streamIndex == audioStreamIndex) {
		logger->verbose("Got audio %s. Sending to codec...", packet ? "packet" : "flush call");
		if((ret = avcodec_send_packet(audioDecoderContext, packet)) < 0) {
			logAVErr("Sending packet to audio codec.", ret);
			return false;
		}

		while(avcodec_receive_frame(audioDecoderContext, frame) == 0) {
			logger->verbose("Received decoded audio frame with timestamp %.04lf (%ld units)!", frame->pts * audioStreamTimeBase, frame->pts);
			av_frame_unref(frame);
		}
	}

	return true;
}

void FFmpegDriver::initializeDemuxerThread(void) {
	demuxerRunning = true;
	demuxerDraining = false;
	
	if((demuxerMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating demuxer mutex!");
	}
	if((demuxerCond = SDL_CreateCond()) == NULL) {
		throw runtime_error("Failed creating condition!");
	}
	if((demuxerThread = SDL_CreateThread(FFmpegDriver::runDemuxerLoop, "DemuxerLoop", (void *)this)) == NULL) {
		throw runtime_error("Failed starting thread!");
	}
}

void FFmpegDriver::destroyDemuxerThread(void) {
	YerFace_MutexLock(demuxerMutex);
	demuxerRunning = false;
	demuxerDraining = true;
	SDL_CondSignal(demuxerCond);
	YerFace_MutexUnlock(demuxerMutex);

	SDL_WaitThread(demuxerThread, NULL);

	SDL_DestroyCond(demuxerCond);
	SDL_DestroyMutex(demuxerMutex);
}

int FFmpegDriver::runDemuxerLoop(void *ptr) {
	FFmpegDriver *driver = (FFmpegDriver *)ptr;
	driver->logger->verbose("Demuxer Thread alive!");

	AVPacket packet;
	av_init_packet(&packet);

	YerFace_MutexLock(driver->demuxerMutex);
	while(driver->demuxerRunning) {
		while((driver->getIsFrameBufferEmpty() || driver->frameDrop) && driver->demuxerRunning && !driver->demuxerDraining) {
			if(av_read_frame(driver->formatContext, &packet) < 0) {
				driver->logger->verbose("Demuxer thread encountered End of Stream! Going into draining mode...");
				driver->demuxerDraining = true;
				driver->decodePacket(NULL, driver->videoStreamIndex);
				if(driver->audioStream != NULL) {
					driver->decodePacket(NULL, driver->audioStreamIndex);
				}
			} else {
				try {
					if(!driver->decodePacket(&packet, packet.stream_index)) {
						driver->logger->warn("Demuxer thread encountered a corrupted packet in the stream!");
					}
				} catch(exception &e) {
					driver->logger->critical("Caught Exception: %s", e.what());
					driver->logger->critical("Going down in flames...");
					driver->demuxerRunning = false;
				}
				av_packet_unref(&packet);
			}

			if(driver->frameDrop) {
				YerFace_MutexUnlock(driver->demuxerMutex);
				SDL_Delay(0);
				YerFace_MutexLock(driver->demuxerMutex);
			}
		}

		if(driver->demuxerDraining && driver->getIsFrameBufferEmpty()) {
			driver->logger->verbose("Draining complete. Demuxer thread terminating...");
			driver->demuxerRunning = false;
		}
		
		if(driver->frameDrop) {
			YerFace_MutexUnlock(driver->demuxerMutex);
			SDL_Delay(0);
			YerFace_MutexLock(driver->demuxerMutex);
		}
		
		if(driver->demuxerRunning && !driver->frameDrop) {
			driver->logger->verbose("Demuxer Thread going to sleep, waiting for work.");
			if(SDL_CondWait(driver->demuxerCond, driver->demuxerMutex) < 0) {
				throw runtime_error("Failed waiting on condition.");
			}
			// driver->logger->verbose("Demuxer Thread is awake now!");
		}
	}

	YerFace_MutexUnlock(driver->demuxerMutex);
	driver->logger->verbose("Demuxer Thread quitting...");
	return 0;
}

}; //namespace YerFace
