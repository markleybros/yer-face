
#include "SphinxDriver.hpp"

using namespace std;
using namespace PocketSphinx;

namespace YerFace {

SphinxDriver::SphinxDriver(string myHiddenMarkovModel, string myAllPhoneLM, FrameDerivatives *myFrameDerivatives, FFmpegDriver *myFFmpegDriver) {
	hiddenMarkovModel = myHiddenMarkovModel;
	allPhoneLM = myAllPhoneLM;
	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	ffmpegDriver = myFFmpegDriver;
	if(ffmpegDriver == NULL) {
		throw invalid_argument("ffmpegDriver cannot be NULL");
	}
	logger = new Logger("SphinxDriver");
	
	pocketSphinx = NULL;
	pocketSphinxConfig = NULL;
	timestampOffsetSet = false;
	utteranceRestarted = false;
	inSpeech = false;
	
	logger->info("Initializing PocketSphinx with Models... <HMM: %s, AllPhone: %s>", hiddenMarkovModel.c_str(), allPhoneLM.c_str());
	if((pocketSphinxConfig = cmd_ln_init(NULL, ps_args(), TRUE, "-hmm", hiddenMarkovModel.c_str(), "-allphone", allPhoneLM.c_str(), "-beam", "1e-20", "-pbeam", "1e-20", "-lw", "2.0", NULL)) == NULL) {
		throw runtime_error("Failed to create PocketSphinx configuration object!");
	}
	
	if((pocketSphinx = ps_init(pocketSphinxConfig)) == NULL) {
		throw runtime_error("Failed to create PocketSphinx speech recognizer!");
	}
	
	if(ps_start_utt(pocketSphinx) != 0) {
		throw runtime_error("Failed to start PocketSphinx utterance");
	}
	
	//This audio format is the only audio format that the Pocket Sphinx phoneme recognizer is trained to work on.
	AudioFrameCallback audioFrameCallback;
	audioFrameCallback.userdata = (void *)this;
	audioFrameCallback.channelLayout = AV_CH_LAYOUT_MONO;
	audioFrameCallback.sampleFormat = AV_SAMPLE_FMT_S16;
	audioFrameCallback.sampleRate = 16000;
	audioFrameCallback.callback = FFmpegDriverAudioFrameCallback;
	ffmpegDriver->registerAudioFrameCallback(audioFrameCallback);
	
	logger->debug("SphinxDriver object constructed and ready to go!");
}

SphinxDriver::~SphinxDriver() {
	logger->debug("SphinxDriver object destructing...");
	ps_free(pocketSphinx);
	cmd_ln_free_r(pocketSphinxConfig);
	delete logger;
}

void SphinxDriver::FFmpegDriverAudioFrameCallback(void *userdata, uint8_t *buf, int audioSamples, int audioBytes, int bufferSize, double timestamp) {
	SphinxDriver *self = (SphinxDriver *)userdata;
	char const *hypothesis;
	if(!self->timestampOffsetSet) {
		self->timestampOffset = timestamp;
		self->timestampOffsetSet = true;
		self->logger->info("Received first audio frame. Set initial timestamp offset to %.04lf seconds.", self->timestampOffset);
	}
	//FIXME -- don't block here... buffer and let another thread do the actual processing
	if(ps_process_raw(self->pocketSphinx, (int16 const *)buf, audioSamples, 0, 0) < 0) {
		throw runtime_error("Failed processing audio samples in PocketSphinx");
	}
	self->inSpeech = ps_get_in_speech(self->pocketSphinx);
	if(self->inSpeech && self->utteranceRestarted) {
		self->utteranceRestarted = false;
	}
	if(!self->inSpeech && !self->utteranceRestarted) {
		if(ps_end_utt(self->pocketSphinx) < 0) {
			throw runtime_error("Failed to end PocketSphinx utterance");
		}
		hypothesis = ps_get_hyp(self->pocketSphinx, NULL);
		self->logger->verbose("Utterance: %s", hypothesis);
		if(ps_start_utt(self->pocketSphinx) < 0) {
			throw runtime_error("Failed to start PocketSphinx utterance");
		}
		self->utteranceRestarted = true;
	}
}

} //namespace YerFace
