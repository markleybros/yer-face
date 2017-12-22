
#include "FFmpegDriver.hpp"

#include "Utilities.hpp"

#include <exception>
#include <stdexcept>

using namespace std;

namespace YerFace {

FFmpegDriver::FFmpegDriver(FrameDerivatives *myFrameDerivatives, string myInputFilename) {
	logger = new Logger("FFmpegDriver");

	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	inputFilename = myInputFilename;
	if(inputFilename.length() < 1) {
		throw invalid_argument("inputFilename must be a valid input filename");
	}

	formatContext = NULL;
	videoDecoderContext = NULL;
	videoStream = NULL;
	frame = NULL;
	frameBGR = NULL;
	swsContext = NULL;

	av_log_set_level(AV_LOG_INFO);
	av_log_set_callback(av_log_default_callback);
	av_register_all();

	logger->info("Opening media file %s...", inputFilename.c_str());

	if(avformat_open_input(&formatContext, inputFilename.c_str(), NULL, NULL) < 0) {
		throw runtime_error("inputFilename could not be opened");
	}

	if(avformat_find_stream_info(formatContext, NULL) < 0) {
		throw runtime_error("failed finding input stream information for inputFilename");
	}

	this->openCodecContext(&videoStreamIndex, &videoDecoderContext, formatContext, AVMEDIA_TYPE_VIDEO);

	videoStream = formatContext->streams[videoStreamIndex];

	width = videoDecoderContext->width;
	height = videoDecoderContext->height;
	pixelFormat = videoDecoderContext->pix_fmt;
	if((videoDestBufSize = av_image_alloc(videoDestData, videoDestLineSize, width, height, pixelFormat, 1)) < 0) {
		throw runtime_error("failed allocating memory for decoded frame");
	}

	av_dump_format(formatContext, 0, inputFilename.c_str(), 0);

	if(!(frame = av_frame_alloc())) {
		throw runtime_error("failed allocating frame");
	}
	if(!(frameBGR = av_frame_alloc())) {
		throw runtime_error("failed allocating frameBGR");
	}

	initializeDemuxerThread();

	logger->debug("FFmpegDriver object constructed and ready to go!");
}

FFmpegDriver::~FFmpegDriver() {
	logger->debug("FFmpegDriver object destructing...");
	destroyDemuxerThread();
	avcodec_free_context(&videoDecoderContext);
	avformat_close_input(&formatContext);
	av_free(videoDestData[0]);
	av_free(frame);
	av_free(frameBGR);
	delete logger;
}

void FFmpegDriver::openCodecContext(int *streamIndex, AVCodecContext **decoderContext, AVFormatContext *myFormatContext, enum AVMediaType type) {
	int myStreamIndex;
	AVStream *stream;
	AVCodec *decoder = NULL;
	AVDictionary *options = NULL;

	if((myStreamIndex = av_find_best_stream(myFormatContext, type, -1, -1, NULL, 0)) < 0) {
		logger->critical("failed to find %s stream in input file", av_get_media_type_string(type));
		throw runtime_error("failed to openCodecContext()");
	}

	stream = myFormatContext->streams[myStreamIndex];

	if(!(decoder = avcodec_find_decoder(stream->codecpar->codec_id))) {
		throw runtime_error("failed to find decoder codec");
	}

	if(!(*decoderContext = avcodec_alloc_context3(decoder))) {
		throw runtime_error("failed to allocate decoder context");
	}

	if(avcodec_parameters_to_context(*decoderContext, stream->codecpar) < 0) {
		throw runtime_error("failed to copy codec parameters to decoder context");
	}

	av_dict_set(&options, "refcounted_frames", "1", 0);
	if(avcodec_open2(*decoderContext, decoder, &options) < 0) {
		throw runtime_error("failed to open codec");
	}

	*streamIndex = myStreamIndex;
}

bool FFmpegDriver::getIsFrameBufferEmpty(void) {
	return false;
}

bool FFmpegDriver::decodePacket(const AVPacket *packet) {
	if(packet->stream_index == videoStreamIndex) {
		logger->verbose("Got video packet. Sending to codec...");
		if(avcodec_send_packet(videoDecoderContext, packet) < 0) {
			logger->warn("Error decoding video frame");
			return false;
		}

		while(avcodec_receive_frame(videoDecoderContext, frame) == 0) {
			logger->verbose("Received decoded video frame!");
			if(frame->width != width || frame->height != height || frame->format != pixelFormat) {
				logger->warn("We cannot handle runtime changes to video width, height, or pixel format. Unfortunately, the width, height or pixel format of the input video has changed: old [ width = %d, height = %d, format = %s ], new [ width = %d, height = %d, format = %s ]", width, height, av_get_pix_fmt_name(pixelFormat), frame->width, frame->height, av_get_pix_fmt_name((AVPixelFormat)frame->format));
				return false;
			}
			if((swsContext = sws_getCachedContext(swsContext, width, height, pixelFormat, width, height, AV_PIX_FMT_BGR24, SWS_BICUBIC, NULL, NULL, NULL)) == NULL) {
				logger->error("SWS Context failed!");
				return false;
			}
			
			// logger->verbose("dumping ffmpeg video frame into new opencv mat with wxh %dx%d and linesize %d", width, height, ((AVPicture *)frameBGR)->linesize);
			// Mat frameCV = Mat(height, width, CV_8UC3);
			// //sws_scale(swsContext, ((AVPicture*)frame)->data, ((AVPicture*)frame)->linesize, 0, height, ((AVPicture *)frameBGR)->data, ((AVPicture *)frameBGR)->linesize);
			// const uint8_t *dst = frameCV.data;
			// sws_scale(swsContext, ((AVPicture*)frame)->data, ((AVPicture*)frame)->linesize, 0, height, dst, ((AVPicture *)frameBGR)->linesize);
		}
        av_frame_unref(frame);
	}

	return true;
}

void FFmpegDriver::initializeDemuxerThread(void) {
	demuxerRunning = true;
	demuxerStillReading = true;
	
	if((demuxerMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
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
	SDL_CondSignal(demuxerCond);
	YerFace_MutexUnlock(demuxerMutex);

	SDL_WaitThread(demuxerThread, NULL);

	SDL_DestroyCond(demuxerCond);
	SDL_DestroyMutex(demuxerMutex);
}

int FFmpegDriver::runDemuxerLoop(void *ptr) {
	FFmpegDriver *driver = (FFmpegDriver *)ptr;
	driver->logger->verbose("Demuxer Thread is alive!");

	AVPacket packet;
	av_init_packet(&packet);

	YerFace_MutexLock(driver->demuxerMutex);
	while(driver->demuxerRunning) {
		while(driver->demuxerStillReading && driver->getIsFrameBufferEmpty()) {
			driver->logger->verbose("Demuxer thread attempting to read an A/V packet out of the container!");
			if(av_read_frame(driver->formatContext, &packet) < 0) {
				driver->logger->verbose("Demuxer thread encountered end of stream!");
				driver->demuxerStillReading = false;
			} else {
				if(!driver->decodePacket(&packet)) {
					driver->logger->warn("Demuxer thread encountered a corrupted packet in the stream!");
						break;
				}
				av_packet_unref(&packet);
			}
		}

		driver->logger->verbose("Demuxer Thread going to sleep, waiting for work.");
		if(SDL_CondWait(driver->demuxerCond, driver->demuxerMutex) < 0) {
			throw runtime_error("Failed waiting on condition.");
		}
		driver->logger->verbose("Demuxer Thread is awake now!");
	}
	YerFace_MutexUnlock(driver->demuxerMutex);
	driver->logger->verbose("Demuxer Thread quitting...");
	return 0;
}

}; //namespace YerFace
