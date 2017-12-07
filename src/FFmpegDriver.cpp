
#include "FFmpegDriver.hpp"

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

	av_register_all();

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

	logger->debug("FFmpegDriver object constructed and ready to go! <'%s' %dx%d %s>", inputFilename.c_str(), width, height, av_get_pix_fmt_name(pixelFormat));
}

FFmpegDriver::~FFmpegDriver() {
	logger->debug("FFmpegDriver object destructing...");
	avcodec_free_context(&videoDecoderContext);
	avformat_close_input(&formatContext);
	av_free(videoDestData[0]);
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

}; //namespace YerFace
