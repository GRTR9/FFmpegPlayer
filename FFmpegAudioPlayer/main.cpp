#pragma warning(disable:4996)
#include <iostream>
#include <string>

extern "C" {

#include <libavcodec/avcodec.h>
#pragma comment(lib, "avcodec.lib")

#include <libavformat/avformat.h>
#pragma comment(lib, "avformat.lib")

#include <libavutil/avutil.h>
#pragma comment(lib, "avutil.lib")

#include <libswresample/swresample.h>
#pragma comment(lib, "swresample.lib")

#include <libavutil/audio_fifo.h>
}

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

using namespace std;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
	AVAudioFifo* fifo = reinterpret_cast<AVAudioFifo*>(pDevice->pUserData);
	av_audio_fifo_read(fifo, &pOutput, frameCount);

	(void)pInput;
}

int main() {
	string fileName = "01 Mitsukiyo 01 Constant Moderato.mp3";
	string filePath = ".\\TestData\\" + fileName;
	//1. Init decode parameter
	AVFormatContext* audioFmtCtx = nullptr;
	int ret = avformat_open_input(&audioFmtCtx, filePath.c_str(), NULL, NULL);
	if (ret < 0) {
		cerr << "Unable to open audio" << endl;
		return -1;
	}

	ret = avformat_find_stream_info(audioFmtCtx, NULL);
	if (ret < 0) {
		cerr << "Unable to find info" << endl;
		return -1;
	}

	int audioStreamIndex = av_find_best_stream(audioFmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (audioFmtCtx < 0) {
		cerr << "No index of audio stream found" << endl;
		return -1;
	}

	AVStream* audioStream = audioFmtCtx->streams[audioStreamIndex];

	const AVCodec* audioDecoder = avcodec_find_decoder(audioStream->codecpar->codec_id);
	if (!audioDecoder) {
		cerr << "No decoder found" << endl;
		return -1;
	}

	AVCodecContext* audioCodecCtx = avcodec_alloc_context3(audioDecoder);
	avcodec_parameters_to_context(audioCodecCtx, audioStream->codecpar);

	ret = avcodec_open2(audioCodecCtx, audioDecoder, NULL);
	if (ret < 0) {
		cerr << "Cound't open decoder" << endl;
		return -1;
	}

	//2. Decode audio
	AVPacket* audiopkt = av_packet_alloc();
	AVFrame* audioFrame = av_frame_alloc();

	SwrContext* resampler = swr_alloc_set_opts(NULL, audioStream->codecpar->channel_layout, AV_SAMPLE_FMT_FLT, audioStream->codecpar->sample_rate, audioStream->codecpar->channel_layout, (AVSampleFormat)audioStream->codecpar->format, audioStream->codecpar->sample_rate, 0, NULL);

	AVAudioFifo* fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLT, audioStream->codecpar->channels, 1);

	while (av_read_frame(audioFmtCtx, audiopkt) == 0) {
		if (audiopkt->stream_index != audioStream->index) continue;
		ret = avcodec_send_packet(audioCodecCtx, audiopkt);
		if (ret < 0) {
			if (ret != AVERROR(EAGAIN)) {
				cerr << "Some decoding error occurred" << endl;
			}
		}

		while ((ret = avcodec_receive_frame(audioCodecCtx, audioFrame)) == 0) {
			//resample the frame
			AVFrame* resampledFrame = av_frame_alloc();
			resampledFrame->sample_rate = audioFrame->sample_rate;
			resampledFrame->channel_layout = audioFrame->channel_layout;
			resampledFrame->channels = audioFrame->channels;
			resampledFrame->format = AV_SAMPLE_FMT_FLT;

			ret = swr_convert_frame(resampler, resampledFrame, audioFrame);
			av_frame_unref(audioFrame);
			av_audio_fifo_write(fifo, (void**)resampledFrame->data, resampledFrame->nb_samples);
			av_frame_free(&resampledFrame);
		}
	}

	//3. Play back the audio
	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format = ma_format_f32;
	config.playback.channels = audioStream->codecpar->channels;
	config.sampleRate = audioStream->codecpar->sample_rate;
	config.dataCallback = data_callback;
	config.pUserData = fifo;

	ma_device device;

	avformat_close_input(&audioFmtCtx);
	av_frame_free(&audioFrame);
	av_packet_free(&audiopkt);
	avcodec_free_context(&audioCodecCtx);
	swr_free(&resampler);

	if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
		printf("Failed to open playback device.\n");
		return -3;
	}

	if (ma_device_start(&device) != MA_SUCCESS) {
		printf("Failed to start playback device.\n");
		ma_device_uninit(&device);
		return -4;
	}

	cout << "Now Playing: " << fileName << endl;
	printf("Press Enter to quit...");
	getchar();

	ma_device_uninit(&device);

	av_audio_fifo_free(fifo);

	return 0;
}