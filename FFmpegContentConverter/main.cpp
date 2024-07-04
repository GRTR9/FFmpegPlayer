extern "C" {

#include <libavcodec/avcodec.h>
#pragma comment(lib, "avcodec.lib")

#include <libavformat/avformat.h>
#pragma comment(lib, "avformat.lib")

#include <libavutil/imgutils.h>
#pragma comment(lib, "avutil.lib")

#include <libswscale/swscale.h>
#pragma comment(lib, "swscale.lib")

}
#include <iostream>
#include <string>

std::string averr2string(int errnum) {
	char errStr[256] = { 0 };
	av_strerror(errnum, errStr, sizeof(errStr));
	return std::string(errStr);
}

bool Format_conver(const std::string& inputFile, const std::string& outputFileName) {
	AVFormatContext* inFmtCtx = nullptr;
	AVCodecContext* inVideoCodecCtx = nullptr;
	AVCodecContext* inAudioCodecCtx = nullptr;
	AVFormatContext* outFmtCtx = nullptr;
	AVStream* inVideoStream = nullptr;
	AVStream* inAudioStream = nullptr;
	SwsContext* swsContext = nullptr;
	/*AVCodecID outVideoCodecId;
	AVCodecID outAudioCodecId;*/
	int ret;

	//if (Format == "avi")
	//{
	//	outVideoCodecId = AV_CODEC_ID_MPEG2VIDEO;
	//	outAudioCodecId = AV_CODEC_ID_PCM_S16LE;
	//}
	//else if (Format == "mp4")
	//{
	//	outVideoCodecId = AV_CODEC_ID_H264;
	//	outAudioCodecId = AV_CODEC_ID_AAC;
	//}
	//else if (Format == "wmv")
	//{
	//	outVideoCodecId = AV_CODEC_ID_MSMPEG4V3;
	//	outAudioCodecId = AV_CODEC_ID_WMAV2;
	//}
	//else if (Format == "mkv")
	//{
	//	outVideoCodecId = AV_CODEC_ID_H264;
	//	outAudioCodecId = AV_CODEC_ID_MP3;
	//}
	//else if (Format == "flv")
	//{
	//	outVideoCodecId = AV_CODEC_ID_MPEG4;
	//	outAudioCodecId = AV_CODEC_ID_AAC;
	//}
	//else {
	//	std::cout << "Conversion to this format is not supported" << std::endl;
	//	return false;
	//}


	if (avformat_open_input(&inFmtCtx, inputFile.c_str(), nullptr, nullptr) != 0) {
		std::cout << "Unable to open input file" << std::endl;
		return false;
	}

	if (avformat_find_stream_info(inFmtCtx, nullptr) < 0) {
		std::cout << "Unable to obtain input file stream information" << std::endl;
		avformat_close_input(&inFmtCtx);
		return false;
	}

	int videoStreamIndex = -1;
	int audioStreamIndex = -1;
	for (int i = 0; i < inFmtCtx->nb_streams; i++) {
		if (inFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoStreamIndex = i;
		}
		else if (inFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioStreamIndex = i;
		}
	}

	if (videoStreamIndex == -1 || audioStreamIndex == -1) {
		std::cout << "Stream not found" << std::endl;
		avformat_close_input(&inFmtCtx);
		return false;
	}

	inVideoStream = inFmtCtx->streams[videoStreamIndex];
	inAudioStream = inFmtCtx->streams[audioStreamIndex];

	const AVCodec* inVideoCodec = avcodec_find_decoder(inVideoStream->codecpar->codec_id);
	if (!inVideoCodec) {
		std::cout << "Video decoder not found" << std::endl;
		avformat_close_input(&inFmtCtx);
		return false;
	}

	inVideoCodecCtx = avcodec_alloc_context3(inVideoCodec);
	if (!inVideoCodecCtx) {
		std::cout << "Failed to create video decoder context" << std::endl;
		avformat_close_input(&inFmtCtx);
		return false;
	}

	avcodec_parameters_to_context(inVideoCodecCtx, inVideoStream->codecpar);
	if (avcodec_open2(inVideoCodecCtx, inVideoCodec, nullptr) < 0) {
		std::cout << "Failed to open video decoder" << std::endl;
		avformat_close_input(&inFmtCtx);
		avcodec_free_context(&inVideoCodecCtx);
		return false;
	}

	const AVCodec* inAudioCodec = avcodec_find_decoder(inAudioStream->codecpar->codec_id);
	if (!inAudioCodec) {
		std::cout << "Failed to get audio encoder" << std::endl;
		avformat_close_input(&inFmtCtx);
		avcodec_free_context(&inVideoCodecCtx);
		return false;
	}

	inAudioCodecCtx = avcodec_alloc_context3(inAudioCodec);
	if (!inAudioCodecCtx) {
		std::cout << "Failed to create audio encoder context" << std::endl;
		avformat_close_input(&inFmtCtx);
		avcodec_free_context(&inVideoCodecCtx);
		return false;
	}

	avcodec_parameters_to_context(inAudioCodecCtx, inAudioStream->codecpar);
	if (avcodec_open2(inAudioCodecCtx, inAudioCodec, nullptr) < 0) {
		std::cout << "Failed to open audio encoder" << std::endl;
		avformat_close_input(&inFmtCtx);
		avcodec_free_context(&inVideoCodecCtx);
		avcodec_free_context(&inAudioCodecCtx);
		return false;
	}

	avformat_alloc_output_context2(&outFmtCtx, nullptr, nullptr, outputFileName.c_str());
	if (!outFmtCtx) {
		std::cout << "Failed to create context for output file" << std::endl;
		avformat_close_input(&inFmtCtx);
		avcodec_free_context(&inVideoCodecCtx);
		avcodec_free_context(&inAudioCodecCtx);
		return false;
	}

	AVStream* outVideoStream = avformat_new_stream(outFmtCtx, nullptr);
	if (!outVideoStream) {
		std::cout << "Adding video stream to output file failed" << std::endl;
		avformat_close_input(&inFmtCtx);
		avcodec_free_context(&inVideoCodecCtx);
		avcodec_free_context(&inAudioCodecCtx);
		avformat_free_context(outFmtCtx);
		return false;
	}
	outVideoStream->id = outFmtCtx->nb_streams - 1;
	avcodec_parameters_copy(outVideoStream->codecpar, inVideoStream->codecpar);
	outVideoStream->codecpar->codec_tag = 0;
	//outVideoStream->time_base = inVideoStream->time_base;

	const AVCodec* outVideoCodec = avcodec_find_encoder(outVideoStream->codecpar->codec_id);
	if (!outVideoCodec) {
		std::cout << "Failed to set video encoder" << std::endl;
		avformat_close_input(&inFmtCtx);
		avcodec_free_context(&inVideoCodecCtx);
		avcodec_free_context(&inAudioCodecCtx);
		avformat_free_context(outFmtCtx);
		return false;
	}
	AVCodecContext* outVideoCodecCtx = avcodec_alloc_context3(outVideoCodec);
	if (!outVideoCodecCtx) {
		std::cout << "Failed to set video encoder context" << std::endl;
		avformat_close_input(&inFmtCtx);
		avcodec_free_context(&inVideoCodecCtx);
		avcodec_free_context(&inAudioCodecCtx);
		avformat_free_context(outFmtCtx);
		return false;
	}

	////Not sure
	////avcodec_parameters_to_context(outVideoCodecCtx, outVideoStream->codecpar);
	//outVideoCodecCtx->codec_id = outVideoCodecId;
	////outVideoCodecCtx->time_base = videoStream->time_base;
	//outVideoCodecCtx->time_base.den = inVideoCodecCtx->time_base.den;
	//outVideoCodecCtx->time_base.num = inVideoCodecCtx->time_base.num;
	//outVideoCodecCtx->gop_size = inVideoCodecCtx->gop_size;
	//outVideoCodecCtx->bit_rate = inVideoCodecCtx->bit_rate;
	//outVideoCodecCtx->refs = inVideoCodecCtx->refs;
	//outVideoCodecCtx->max_b_frames = inVideoCodecCtx->max_b_frames;
	//outVideoCodecCtx->width = inVideoCodecCtx->width;
	//outVideoCodecCtx->height = inVideoCodecCtx->height;
	//outVideoCodecCtx->pix_fmt = inVideoCodecCtx->pix_fmt;

	avcodec_parameters_to_context(outVideoCodecCtx, outVideoStream->codecpar);
	//outVideoCodecCtx->codec_id = outVideoCodecId;
	outVideoCodecCtx->time_base = inVideoStream->time_base;
	/*outVideoCodecCtx->time_base.den = inVideoCodecCtx->time_base.den;
	outVideoCodecCtx->time_base.num = inVideoCodecCtx->time_base.num;*/
	outVideoCodecCtx->gop_size = inVideoCodecCtx->gop_size;
	//outVideoCodecCtx->bit_rate = 8000000;
	outVideoCodecCtx->refs = inVideoCodecCtx->refs;
	//outVideoCodecCtx->max_b_frames = 4;
	//outVideoCodecCtx->width = 1920;
	//outVideoCodecCtx->height = 1080;
	//outVideoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;


	avcodec_parameters_from_context(outVideoStream->codecpar, outVideoCodecCtx);

	if (avcodec_open2(outVideoCodecCtx, outVideoCodec, nullptr) < 0) {
		std::cout << "Unable to open video encoder" << std::endl;
		avformat_close_input(&inFmtCtx);
		avcodec_free_context(&inVideoCodecCtx);
		avcodec_free_context(&inAudioCodecCtx);
		avformat_free_context(outFmtCtx);
		avcodec_free_context(&outVideoCodecCtx);
		return false;
	}

	AVStream* outAudioStream = avformat_new_stream(outFmtCtx, nullptr);
	if (!outAudioStream) {
		std::cout << "Adding audio stream to output file failed" << std::endl;
		avformat_close_input(&inFmtCtx);
		avcodec_free_context(&inVideoCodecCtx);
		avcodec_free_context(&inAudioCodecCtx);
		avformat_free_context(outFmtCtx);
		avcodec_free_context(&outVideoCodecCtx);
		return false;
	}
	outAudioStream->id = outFmtCtx->nb_streams - 1;

	avcodec_parameters_copy(outAudioStream->codecpar, inAudioStream->codecpar);
	outAudioStream->codecpar->codec_tag = 0;

	const AVCodec* outAudioCodec = avcodec_find_encoder(outAudioStream->codecpar->codec_id);
	if (!outAudioCodec) {
		std::cout << "Failed to set audio encoder" << std::endl;
		avformat_close_input(&inFmtCtx);
		avcodec_free_context(&inVideoCodecCtx);
		avcodec_free_context(&inAudioCodecCtx);
		avformat_free_context(outFmtCtx);
		avcodec_free_context(&outVideoCodecCtx);
		return false;
	}
	AVCodecContext* outAudioCodecCtx = avcodec_alloc_context3(outAudioCodec);
	if (!outAudioCodecCtx) {
		std::cout << "Failed to set audio encoder context" << std::endl;
		avformat_close_input(&inFmtCtx);
		avcodec_free_context(&inVideoCodecCtx);
		avcodec_free_context(&inAudioCodecCtx);
		avformat_free_context(outFmtCtx);
		avcodec_free_context(&outVideoCodecCtx);
		return false;
	}

	avcodec_parameters_to_context(outAudioCodecCtx, outAudioStream->codecpar);
	outAudioCodecCtx->codec_id = outAudioStream->codecpar->codec_id;
	outAudioCodecCtx->time_base = inAudioStream->time_base;
	outAudioCodecCtx->sample_fmt = inAudioCodecCtx->sample_fmt;
	//outAudioCodecCtx->sample_fmt = AV_SAMPLE_FMT_S16;
	avcodec_parameters_from_context(outAudioStream->codecpar, outAudioCodecCtx);

	ret = avcodec_open2(outAudioCodecCtx, outAudioCodec, nullptr);
	if (ret < 0) {
		std::cout << "Unable to open audio encoder: " << averr2string(ret) << std::endl;
		avformat_close_input(&inFmtCtx);
		avcodec_free_context(&inVideoCodecCtx);
		avcodec_free_context(&inAudioCodecCtx);
		avformat_free_context(outFmtCtx);
		avcodec_free_context(&outVideoCodecCtx);
		avcodec_free_context(&outAudioCodecCtx);
		return false;
	}

	if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
		if (avio_open(&outFmtCtx->pb, outputFileName.c_str(), AVIO_FLAG_WRITE) < 0) {
			std::cout << "Unable to open output file" << std::endl;
			avformat_close_input(&inFmtCtx);
			avcodec_free_context(&inVideoCodecCtx);
			avcodec_free_context(&inAudioCodecCtx);
			avformat_free_context(outFmtCtx);
			avcodec_free_context(&outVideoCodecCtx);
			avcodec_free_context(&outAudioCodecCtx);
			return false;
		}
	}

	ret = avformat_write_header(outFmtCtx, nullptr);
	if (ret < 0) {
		std::cout << "Unable to write output file header: " << averr2string(ret) << std::endl;
		avformat_close_input(&inFmtCtx);
		avcodec_free_context(&inVideoCodecCtx);
		avcodec_free_context(&inAudioCodecCtx);
		avformat_free_context(outFmtCtx);
		avcodec_free_context(&outVideoCodecCtx);
		avcodec_free_context(&outAudioCodecCtx);
		return false;
	}

	av_dump_format(outFmtCtx, 0, outputFileName.c_str(), 1);

	AVFrame* videoFrame = av_frame_alloc();
	AVFrame* audioFrame = av_frame_alloc();
	AVPacket* inputPacket = av_packet_alloc();
	AVPacket* videoOutputPacket = av_packet_alloc();
	AVPacket* audioOutputPacket = av_packet_alloc();
	if (!videoFrame || !audioFrame || !inputPacket || !videoOutputPacket || !audioOutputPacket) {
		std::cout << "Failed to allocate frame object" << std::endl;
		avformat_close_input(&inFmtCtx);
		avcodec_free_context(&inVideoCodecCtx);
		avcodec_free_context(&inAudioCodecCtx);
		avformat_free_context(outFmtCtx);
		avcodec_free_context(&outVideoCodecCtx);
		avcodec_free_context(&outAudioCodecCtx);
		return false;
	}

	// initswsContext (pixel format converter)
	/*swsContext = sws_getContext(inVideoCodecCtx->width, inVideoCodecCtx->height, inVideoCodecCtx->pix_fmt,
		outVideoCodecCtx->width, outVideoCodecCtx->height, outVideoCodecCtx->pix_fmt,
		SWS_BILINEAR, nullptr, nullptr, nullptr);
	if (!swsContext) {
		std::cout << "Failed to initialize pixel format converter" << std::endl;
		avformat_close_input(&inFmtCtx);
		avcodec_free_context(&inVideoCodecCtx);
		avcodec_free_context(&inAudioCodecCtx);
		avformat_free_context(outFmtCtx);
		avcodec_free_context(&outVideoCodecCtx);
		avcodec_free_context(&outAudioCodecCtx);
		av_frame_free(&videoFrame);
		av_frame_free(&audioFrame);
		av_packet_free(&inputPacket);
		av_packet_free(&videoOutputPacket);
		av_packet_free(&audioOutputPacket);
		return false;
	}*/

	ret = 0;
	int nVideoCount = 0;
	while (av_read_frame(inFmtCtx, inputPacket) >= 0) {
		if (inputPacket->stream_index == videoStreamIndex) {
			ret = avcodec_send_packet(inVideoCodecCtx, inputPacket);
			if (ret < 0) {
				break;
			}
			while (ret >= 0) {
				ret = avcodec_receive_frame(inVideoCodecCtx, videoFrame);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
					break;
				}
				else if (ret < 0) {
					std::cout << "Video decoding ret abnormality: " << averr2string(ret) << std::endl;
					avformat_close_input(&inFmtCtx);
					avcodec_free_context(&inVideoCodecCtx);
					avcodec_free_context(&inAudioCodecCtx);
					avformat_free_context(outFmtCtx);
					avcodec_free_context(&outVideoCodecCtx);
					avcodec_free_context(&outAudioCodecCtx);
					av_frame_free(&videoFrame);
					av_frame_free(&audioFrame);
					av_packet_free(&inputPacket);
					av_packet_free(&videoOutputPacket);
					av_packet_free(&audioOutputPacket);
					return false;
				}

				// covert pixel format
				/*sws_scale(swsContext, videoFrame->data, videoFrame->linesize, 0, inVideoCodecCtx->height,
					videoFrame->data, videoFrame->linesize);*/

				//videoFrame->pts = (int64_t)(40 * (nVideoCount) / av_q2d(outVideoCodecCtx->time_base) / 1000.0);
				AVRational time_base1 = inVideoStream->time_base;
				//Duration between 2 frames (us)
				int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(inVideoStream->r_frame_rate);
				//Parameters
				videoFrame->pts = (double)(nVideoCount * calc_duration) / (double)(av_q2d(time_base1) * AV_TIME_BASE);
				videoFrame->pkt_dts = videoFrame->pts;
				videoFrame->duration = (double)calc_duration / (double)(av_q2d(time_base1) * AV_TIME_BASE);

				nVideoCount++;

				ret = avcodec_send_frame(outVideoCodecCtx, videoFrame);
				if (ret < 0) {
					break;
				}

				while (ret >= 0) {
					ret = avcodec_receive_packet(outVideoCodecCtx, videoOutputPacket);
					if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
						break;
					}
					else if (ret < 0) {
						std::cout << "Video encoding ret abnormal" << std::endl;
						avformat_close_input(&inFmtCtx);
						avcodec_free_context(&inVideoCodecCtx);
						avcodec_free_context(&inAudioCodecCtx);
						avformat_free_context(outFmtCtx);
						avcodec_free_context(&outVideoCodecCtx);
						avcodec_free_context(&outAudioCodecCtx);
						av_frame_free(&videoFrame);
						av_frame_free(&audioFrame);
						av_packet_free(&inputPacket);
						av_packet_free(&videoOutputPacket);
						av_packet_free(&audioOutputPacket);
						return false;
					}
										//0/1					//1/24000					1/24000	
					av_packet_rescale_ts(videoOutputPacket, outVideoCodecCtx->time_base, outVideoStream->time_base);
					videoOutputPacket->stream_index = outVideoStream->index;

					ret = av_interleaved_write_frame(outFmtCtx, videoOutputPacket);
					if (ret < 0) {
						break;
					}
				}
			}
		}
		else if (inputPacket->stream_index == audioStreamIndex) {

			ret = avcodec_send_packet(inAudioCodecCtx, inputPacket);
			if (ret < 0) {
				break;
			}

			while (ret >= 0) {
				ret = avcodec_receive_frame(inAudioCodecCtx, audioFrame);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
					break;
				}
				else if (ret < 0) {
					std::cout << "Audio decoding ret exception" << std::endl;
					avformat_close_input(&inFmtCtx);
					avcodec_free_context(&inVideoCodecCtx);
					avcodec_free_context(&inAudioCodecCtx);
					avformat_free_context(outFmtCtx);
					avcodec_free_context(&outVideoCodecCtx);
					avcodec_free_context(&outAudioCodecCtx);
					av_frame_free(&videoFrame);
					av_frame_free(&audioFrame);
					av_packet_free(&inputPacket);
					av_packet_free(&videoOutputPacket);
					av_packet_free(&audioOutputPacket);
					return false;
				}

				ret = avcodec_send_frame(outAudioCodecCtx, audioFrame);
				if (ret < 0) {
					break;
				}

				while (ret >= 0) {
					ret = avcodec_receive_packet(outAudioCodecCtx, audioOutputPacket);
					if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
						break;
					}
					else if (ret < 0) {
						std::cout << "Audio encoding ret abnormal" << std::endl;
						avformat_close_input(&inFmtCtx);
						avcodec_free_context(&inVideoCodecCtx);
						avcodec_free_context(&inAudioCodecCtx);
						avformat_free_context(outFmtCtx);
						avcodec_free_context(&outVideoCodecCtx);
						avcodec_free_context(&outAudioCodecCtx);
						av_frame_free(&videoFrame);
						av_frame_free(&audioFrame);
						av_packet_free(&inputPacket);
						av_packet_free(&videoOutputPacket);
						av_packet_free(&audioOutputPacket);
						return false;
					}

					//av_packet_rescale_ts(audioOutputPacket, outAudioCodecCtx->time_base, outAudioStream->time_base);
					audioOutputPacket->stream_index = outAudioStream->index;

					ret = av_interleaved_write_frame(outFmtCtx, audioOutputPacket);
					if (ret < 0) {
						break;
					}
				}
			}
		}

		av_packet_unref(inputPacket);
	}

	av_write_trailer(outFmtCtx);

	av_frame_free(&videoFrame);
	av_frame_free(&audioFrame);
	av_packet_free(&inputPacket);
	av_packet_free(&videoOutputPacket);
	av_packet_free(&audioOutputPacket);

	avcodec_free_context(&inVideoCodecCtx);
	avcodec_free_context(&inAudioCodecCtx);
	avcodec_free_context(&outVideoCodecCtx);
	avcodec_free_context(&outAudioCodecCtx);

	avformat_close_input(&inFmtCtx);
	avformat_free_context(outFmtCtx);

	sws_freeContext(swsContext);

	return true;
}


int main() {
	std::string inputFilename, outputFilename;
	inputFilename = ".\\TestData\\input\\V201MP4-AVC1080p24fps8bit-AAC2.0.mp4";
	outputFilename = ".\\TestData\\output\\out.mov";
	if (!Format_conver(inputFilename, outputFilename)) {
		std::cout << "Failed to convert!" << std::endl;
		return -1;
	}
	std::cout << "Conversion complete!" << std::endl;
	return 0;
}