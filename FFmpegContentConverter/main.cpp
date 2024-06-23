#include <iostream>
#include <string>

extern "C" {

#include <libavcodec/avcodec.h>
#pragma comment(lib, "avcodec.lib")

#include <libavformat/avformat.h>
#pragma comment(lib, "avformat.lib")

#include <libavutil/avutil.h>
#pragma comment(lib, "avutil.lib")

}

using namespace std;

#define USE_H264BSF 0
#define USE_AACBSF 0
#define OUTPUTFMT "mkv"

int main() {
	string inFilePath = ".\\TestData\\input\\V201 MP4 - AVC 1080p 24fps 8bit - AAC2.0.mp4";
	string outFilePath = ".\\TestData\\output\\out." + string(OUTPUTFMT);

	//AVOutputFormat* outFmt = NULL;
	AVFormatContext* inFmtCtx = NULL, * outFmtCtx = NULL;
	AVPacket* pkt = av_packet_alloc();
	int ret;
	int inVideoIndex = -1, outVideoIndex = -1;
	int inAudioIndex = -1, outAudioIndex = -1;
	int frameIndex = 0;
	int64_t cur_pts_v = 0, cur_pts_a = 0;

	ret = avformat_open_input(&inFmtCtx, inFilePath.c_str(), NULL, NULL);
	if (ret < 0) {
		cerr << "Unable to open input file" << endl;
		goto end;
	}

	ret = avformat_find_stream_info(inFmtCtx, NULL);
	if (ret < 0) {
		cerr << "No stream info found" << endl;
		goto end;
	}

	std::cout << "===========Input Information==========" << endl;
	av_dump_format(inFmtCtx, 0, inFilePath.c_str(), 0);
	std::cout << "======================================" << endl;

	avformat_alloc_output_context2(&outFmtCtx, NULL, NULL, outFilePath.c_str());
	if (!outFmtCtx) {
		cerr << "Couldn't create output context" << endl;
		ret = AVERROR_UNKNOWN;
		goto end;
	}

	for (int i = 0; i < inFmtCtx->nb_streams; i++) {
		AVStream* inStream = inFmtCtx->streams[i];
		AVStream* outStream = NULL;
		switch (inStream->codecpar->codec_type)
		{
		case AVMEDIA_TYPE_VIDEO:
			outStream = avformat_new_stream(outFmtCtx, NULL);
			inVideoIndex = i;
			if (!outStream) {
				cerr << "Failed allocating output stream" << endl;
				ret = AVERROR_UNKNOWN;
				goto end;
			}
			outVideoIndex = outStream->index;
			ret = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
			if (ret < 0) {
				cerr << "Failed to copy codec parameters" << endl;
				goto end;
			}
			outStream->codecpar->codec_tag = 0;
			break;

		case AVMEDIA_TYPE_AUDIO:
			outStream = avformat_new_stream(outFmtCtx, NULL);
			inAudioIndex = i;
			if (!outStream) {
				cerr << "Failed allocating output stream" << endl;
				ret = AVERROR_UNKNOWN;
				goto end;
			}
			outAudioIndex = outStream->index;
			ret = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
			if (ret < 0) {
				cerr << "Failed to copy codec parameters" << endl;
				goto end;
			}
			outStream->codecpar->codec_tag = 0;
			break;
		}
	}

	std::cout << "===========Output Information==========" << endl;
	av_dump_format(outFmtCtx, 0, outFilePath.c_str(), 1);
	std::cout << "======================================" << endl;

	if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
		if (avio_open(&outFmtCtx->pb, outFilePath.c_str(), AVIO_FLAG_WRITE) < 0) {
			printf("Could not open output file '%s'", outFilePath.c_str());
			goto end;
		}
	}
	ret = avformat_write_header(outFmtCtx, NULL);
	if (ret < 0) {
		cerr << "Error occurred when opening output file" << endl;
		goto end;
	}
	while (1) {
		ret = av_read_frame(inFmtCtx, pkt);
		if (ret < 0) {
			break;
		}
		if (pkt->stream_index == inVideoIndex) {
			pkt->stream_index = outVideoIndex;
			ret = av_interleaved_write_frame(outFmtCtx, pkt);
			if (ret < 0) {
				av_packet_unref(pkt);
				cerr << "Error muxing packet" << endl;
				break;
			}
		}
		else if (pkt->stream_index == inAudioIndex) {
			pkt->stream_index = outAudioIndex;
			ret = av_interleaved_write_frame(outFmtCtx, pkt);
			if (ret < 0) {
				av_packet_unref(pkt);
				cerr << "Error muxing packet" << endl;
				break;
			}
		}
		av_packet_unref(pkt);
	}


#if USE_H264BSF
	AVBitStreamFilterContext* h264bsfc = av_bitstream_filter_init("h264_mp4toannexb");
#endif
#if USE_AACBSF
	AVBitStreamFilterContext* aacbsfc = av_bitstream_filter_init("aac_adtstoasc");
#endif



#if USE_H264BSF
	av_bitstream_filter_close(h264bsfc);
#endif
#if USE_AACBSF
	av_bitstream_filter_close(aacbsfc);
#endif

end:
	avformat_close_input(&inFmtCtx);
	/* close output */
	if (outFmtCtx && !(outFmtCtx->oformat->flags & AVFMT_NOFILE))
		avio_close(outFmtCtx->pb);
	avformat_free_context(outFmtCtx);
	if (ret < 0 && ret != AVERROR_EOF) {
		printf("Error occurred.\n");
		return -1;
	}
	return 0;
}