#pragma warning(disable:4996)
//#pragma warning(disable:400)


#include <iostream>
#include <string>
#include <time.h>
#include <assert.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/time.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>

#pragma comment(lib, "avutil.lib")
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"swresample.lib")
#pragma comment(lib,"swscale.lib")

#include <SDL.h>
#include <SDL_video.h>
#include <SDL_render.h>
#include <SDL_rect.h>

}


#define SDL_USEREVENT_REFRESH  (SDL_USEREVENT + 1)

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

typedef struct packet_queue_t
{
	AVPacketList* first_pkt;
	AVPacketList* last_pkt;
	int nb_packets;
	int size;         //byte size
	SDL_mutex* mutex;
	SDL_cond* cond;
} packet_queue_t;

typedef struct AudioParams {
	int freq;
	int channels;
	int64_t channel_layout;
	enum AVSampleFormat fmt;
	int frame_size;
	int bytes_per_sec;
} FF_AudioParams;


static FF_AudioParams s_audio_param_src;
static FF_AudioParams s_audio_param_tgt;
static struct SwrContext* s_audio_swr_ctx;
static uint8_t* s_resample_buf = NULL;  // resample output buffer
static int s_resample_buf_len = 0;      // len of resample output buffer

static bool s_adecode_finished = false;
static bool s_vdecode_finished = false;

static packet_queue_t s_audio_pkt_queue;
static packet_queue_t s_video_pkt_queue;

std::string averr2str(int errnum) {
	char errStr[256] = { 0 };
	av_strerror(errnum, errStr, sizeof(errStr));
	return std::string(errStr);
}

void packet_queue_init(packet_queue_t* q)
{
	memset(q, 0, sizeof(packet_queue_t));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}

int packet_queue_num(packet_queue_t* q)
{
	return q->nb_packets;
}

int packet_queue_push(packet_queue_t* q, AVPacket* pkt)
{
	AVPacketList* pkt_list;

	if ((pkt != NULL) && (pkt->data != NULL) && (av_packet_make_refcounted(pkt) < 0))
	{
		printf("[pkt] is not refrence counted\n");
		return -1;
	}

	pkt_list = (AVPacketList*)av_malloc(sizeof(AVPacketList));
	if (!pkt_list)
	{
		return -1;
	}

	pkt_list->pkt = *pkt;
	pkt_list->next = NULL;

	SDL_LockMutex(q->mutex);

	if (!q->last_pkt)   // queue is null
	{
		q->first_pkt = pkt_list;
	}
	else
	{
		q->last_pkt->next = pkt_list;
	}
	q->last_pkt = pkt_list;
	q->nb_packets++;
	q->size += pkt_list->pkt.size;

	// Send a signal of the condition variable: restart a thread waiting for the q->cond condition variable
	SDL_CondSignal(q->cond);

	SDL_UnlockMutex(q->mutex);
	return 0;
}

int packet_queue_pop(packet_queue_t* q, AVPacket* pkt, int block)
{
	AVPacketList* p_pkt_node;
	int ret;

	SDL_LockMutex(q->mutex);

	while (1)
	{
		p_pkt_node = q->first_pkt;
		if (p_pkt_node)
		{
			q->first_pkt = p_pkt_node->next;
			if (!q->first_pkt)
			{
				q->last_pkt = NULL;
			}
			q->nb_packets--;
			q->size -= p_pkt_node->pkt.size;
			*pkt = p_pkt_node->pkt;
			av_free(p_pkt_node);
			ret = 1;
			break;
		}
		else if (!block)            // If the queue is empty and the blocking flag is invalid, exit immediately
		{
			ret = 0;
			break;
		}
		else                        // If the queue is empty and the blocking flag is valid, wait
		{
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
}

int audio_decode_frame(AVCodecContext* p_codec_ctx, AVPacket* p_packet, uint8_t* audio_buf, int buf_size)
{
	AVFrame* p_frame = av_frame_alloc();

	int frm_size = 0;
	int res = 0;
	int ret = 0;
	int nb_samples = 0;
	uint8_t* p_cp_buf = NULL;
	int cp_len = 0;
	bool need_new = false;

	res = 0;
	while (1)
	{
		need_new = false;

		ret = avcodec_receive_frame(p_codec_ctx, p_frame);
		if (ret != 0)
		{
			if (ret == AVERROR_EOF)
			{
				printf("audio avcodec_receive_frame(): the decoder has been fully flushed\n");
				res = 0;
				goto exit;
			}
			else if (ret == AVERROR(EAGAIN))
			{
				//printf("audio avcodec_receive_frame(): output is not available in this state - "
				//       "user must try to send new input\n");
				need_new = true;
			}
			else if (ret == AVERROR(EINVAL))
			{
				printf("audio avcodec_receive_frame(): codec not opened, or it is an encoder\n");
				res = -1;
				goto exit;
			}
			else
			{
				printf("audio avcodec_receive_frame(): legitimate decoding errors\n");
				res = -1;
				goto exit;
			}
		}
		else
		{
			if (p_frame->format != s_audio_param_src.fmt ||
				p_frame->channel_layout != s_audio_param_src.channel_layout ||
				p_frame->sample_rate != s_audio_param_src.freq)
			{
				swr_free(&s_audio_swr_ctx);
				s_audio_swr_ctx = swr_alloc_set_opts(NULL,
					s_audio_param_tgt.channel_layout,
					s_audio_param_tgt.fmt,
					s_audio_param_tgt.freq,
					p_frame->channel_layout,
					(AVSampleFormat)p_frame->format,
					p_frame->sample_rate,
					0,
					NULL);
				if (s_audio_swr_ctx == NULL || swr_init(s_audio_swr_ctx) < 0)
				{
					printf("Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
						p_frame->sample_rate, av_get_sample_fmt_name((AVSampleFormat)p_frame->format), p_frame->channels,
						s_audio_param_tgt.freq, av_get_sample_fmt_name(s_audio_param_tgt.fmt), s_audio_param_tgt.channels);
					swr_free(&s_audio_swr_ctx);
					return -1;
				}

				s_audio_param_src.channel_layout = p_frame->channel_layout;
				s_audio_param_src.channels = p_frame->channels;
				s_audio_param_src.freq = p_frame->sample_rate;
				s_audio_param_src.fmt = (AVSampleFormat)p_frame->format;
			}

			if (s_audio_swr_ctx != NULL)
			{
				const uint8_t** in = (const uint8_t**)p_frame->extended_data;
				uint8_t** out = &s_resample_buf;
				int out_count = (int64_t)p_frame->nb_samples * s_audio_param_tgt.freq / p_frame->sample_rate + 256;
				int out_size = av_samples_get_buffer_size(NULL, s_audio_param_tgt.channels, out_count, s_audio_param_tgt.fmt, 0);
				if (out_size < 0)
				{
					printf("av_samples_get_buffer_size() failed\n");
					return -1;
				}

				if (s_resample_buf == NULL)
				{
					av_fast_malloc(&s_resample_buf, (unsigned int*)&s_resample_buf_len, out_size);
				}
				if (s_resample_buf == NULL)
				{
					return AVERROR(ENOMEM);
				}
				nb_samples = swr_convert(s_audio_swr_ctx, out, out_count, in, p_frame->nb_samples);
				if (nb_samples < 0) {
					printf("swr_convert() failed\n");
					return -1;
				}
				if (nb_samples == out_count)
				{
					printf("audio buffer is probably too small\n");
					if (swr_init(s_audio_swr_ctx) < 0)
						swr_free(&s_audio_swr_ctx);
				}

				p_cp_buf = s_resample_buf;
				cp_len = nb_samples * s_audio_param_tgt.channels * av_get_bytes_per_sample(s_audio_param_tgt.fmt);
			}
			else
			{
				frm_size = av_samples_get_buffer_size(
					NULL,
					p_codec_ctx->channels,
					p_frame->nb_samples,
					p_codec_ctx->sample_fmt,
					1);

				//printf("frame size %d, buffer size %d\n", frm_size, buf_size);
				//assert(frm_size <= buf_size);

				p_cp_buf = p_frame->data[0];
				cp_len = frm_size;
			}

			memcpy(audio_buf, p_cp_buf, cp_len);

			res = cp_len;
			goto exit;
		}

		if (need_new)
		{
			ret = avcodec_send_packet(p_codec_ctx, p_packet);
			if (ret != 0)
			{
				printf("avcodec_send_packet() failed %d\n", ret);
				res = -1;
				goto exit;
			}
		}
	}

exit:
	av_frame_unref(p_frame);
	return res;
}

void sdl_audio_callback(void* userdata, uint8_t* stream, int len)
{
	AVCodecContext* p_codec_ctx = (AVCodecContext*)userdata;
	int copy_len;
	int get_size;

	static uint8_t s_audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	static uint32_t s_audio_len = 0;
	static uint32_t s_tx_idx = 0;


	AVPacket* p_packet;

	int frm_size = 0;
	int ret_size = 0;
	int ret;

	while (len > 0)
	{
		if (s_adecode_finished)
		{
			SDL_PauseAudio(1);
			printf("pause audio callback\n");
			return;
		}

		if (s_tx_idx >= s_audio_len)
		{

			p_packet = (AVPacket*)av_malloc(sizeof(AVPacket));

			if (packet_queue_pop(&s_audio_pkt_queue, p_packet, 1) == 0)
			{
				printf("audio packet buffer empty...\n");
				continue;
			}

			get_size = audio_decode_frame(p_codec_ctx, p_packet, s_audio_buf, sizeof(s_audio_buf));
			if (get_size < 0)
			{
				s_audio_len = 1024; // arbitrary?
				memset(s_audio_buf, 0, s_audio_len);
				av_packet_unref(p_packet);
			}
			else if (get_size == 0)
			{
				s_adecode_finished = true;
			}
			else
			{
				s_audio_len = get_size;
				av_packet_unref(p_packet);
			}
			s_tx_idx = 0;

			//if (p_packet->data != NULL)
			//{
			//	//av_packet_unref(p_packet);
			//}
		}

		copy_len = s_audio_len - s_tx_idx;
		if (copy_len > len)
		{
			copy_len = len;
		}

		memcpy(stream, (uint8_t*)s_audio_buf + s_tx_idx, copy_len);
		len -= copy_len;
		stream += copy_len;
		s_tx_idx += copy_len;
	}
}

// Pass in the current timer interval through the interval parameter, return the next timer interval, return 0 to cancel the timer
//Call this callback function when the timer timeout expires, generate the FF_REFRESH_EVENT event, and add it to the event queue
static uint32_t sdl_time_cb_refresh(uint32_t interval, void* opaque)
{
	SDL_Event sdl_event;
	sdl_event.type = SDL_USEREVENT_REFRESH;
	SDL_PushEvent(&sdl_event);
	return interval;
}

int video_thread(void* arg)
{
	AVCodecContext* p_codec_ctx = (AVCodecContext*)arg;

	AVFrame* p_frm_raw = NULL;
	AVFrame* p_frm_yuv = NULL;
	AVPacket* p_packet = NULL;
	struct SwsContext* sws_ctx = NULL;
	int buf_size;
	uint8_t* buffer = NULL;
	SDL_Window* screen;
	SDL_Renderer* sdl_renderer;
	SDL_Texture* sdl_texture;
	SDL_Rect sdl_rect;
	SDL_Thread* sdl_thread;
	SDL_Event sdl_event;

	int ret = 0;
	int res = -1;

	bool flush = false;

	p_packet = (AVPacket*)av_malloc(sizeof(AVPacket));

	p_frm_raw = av_frame_alloc();
	if (p_frm_raw == NULL)
	{
		printf("av_frame_alloc() for p_frm_raw failed\n");
		res = -1;
		goto exit0;
	}
	p_frm_yuv = av_frame_alloc();
	if (p_frm_yuv == NULL)
	{
		printf("av_frame_alloc() for p_frm_yuv failed\n");
		res = -1;
		goto exit1;
	}

	buf_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
		p_codec_ctx->width,
		p_codec_ctx->height,
		1
	);

	buffer = (uint8_t*)av_malloc(buf_size);
	if (buffer == NULL)
	{
		printf("av_malloc() for buffer failed\n");
		res = -1;
		goto exit2;
	}

	ret = av_image_fill_arrays(p_frm_yuv->data,     // dst data[]
		p_frm_yuv->linesize, // dst linesize[]
		buffer,              // src buffer
		AV_PIX_FMT_YUV420P,  // pixel format
		p_codec_ctx->width,  // width
		p_codec_ctx->height, // height
		1                    // align
	);
	if (ret < 0)
	{
		printf("av_image_fill_arrays() failed %d\n", ret);
		res = -1;
		goto exit3;
	}

	sws_ctx = sws_getContext(p_codec_ctx->width,    // src width
		p_codec_ctx->height,   // src height
		p_codec_ctx->pix_fmt,  // src format
		p_codec_ctx->width,    // dst width
		p_codec_ctx->height,   // dst height
		AV_PIX_FMT_YUV420P,    // dst format
		SWS_BICUBIC,           // flags
		NULL,                  // src filter
		NULL,                  // dst filter
		NULL                   // param
	);
	if (sws_ctx == NULL)
	{
		printf("sws_getContext() failed\n");
		res = -1;
		goto exit4;
	}

	screen = SDL_CreateWindow("simple ffplayer",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		p_codec_ctx->width,
		p_codec_ctx->height,
		SDL_WINDOW_OPENGL
	);
	if (screen == NULL)
	{
		printf("SDL_CreateWindow() failed: %s\n", SDL_GetError());
		res = -1;
		goto exit5;
	}

	sdl_renderer = SDL_CreateRenderer(screen, -1, 0);
	if (sdl_renderer == NULL)
	{
		printf("SDL_CreateRenderer() failed: %s\n", SDL_GetError());
		res = -1;
		goto exit5;
	}

	sdl_texture = SDL_CreateTexture(sdl_renderer,
		SDL_PIXELFORMAT_IYUV,
		SDL_TEXTUREACCESS_STREAMING,
		p_codec_ctx->width,
		p_codec_ctx->height
	);
	if (sdl_texture == NULL)
	{
		printf("SDL_CreateTexture() failed: %s\n", SDL_GetError());
		res = -1;
		goto exit5;
	}

	sdl_rect.x = 0;
	sdl_rect.y = 0;
	sdl_rect.w = p_codec_ctx->width;
	sdl_rect.h = p_codec_ctx->height;

	while (1)
	{
		if (s_vdecode_finished)
		{
			break;
		}

		if (!flush)
		{
			if (packet_queue_pop(&s_video_pkt_queue, p_packet, 0) == 0)
			{
				printf("video packet queue empty...\n");
				av_usleep(10000);
				continue;
			}

			ret = avcodec_send_packet(p_codec_ctx, p_packet);
			if (ret != 0)
			{
				if (ret == AVERROR_EOF)
				{
					printf("video avcodec_send_packet(): the decoder has been flushed\n");
					printf("test unref null packet\n");
					av_packet_unref(NULL);
				}
				else if (ret == AVERROR(EAGAIN))
				{
					printf("video avcodec_send_packet(): input is not accepted in the current state\n");
				}
				else if (ret == AVERROR(EINVAL))
				{
					printf("video avcodec_send_packet(): codec not opened, it is an encoder, or requires flush\n");
				}
				else if (ret == AVERROR(ENOMEM))
				{
					printf("video avcodec_send_packet(): failed to add packet to internal queue, or similar\n");
				}
				else
				{
					printf("video avcodec_send_packet(): legitimate decoding errors\n");
				}

				res = -1;
				goto exit6;
			}

			//flush
			if (p_packet->data == NULL)
			{
				printf("flush video decoder\n");
				flush = true;
			}

			av_packet_unref(p_packet);
		}

		ret = avcodec_receive_frame(p_codec_ctx, p_frm_raw);
		if (ret != 0)
		{
			if (ret == AVERROR_EOF)
			{
				printf("video avcodec_receive_frame(): the decoder has been fully flushed\n");
				s_vdecode_finished = true;
			}
			else if (ret == AVERROR(EAGAIN))
			{
				printf("video avcodec_receive_frame(): output is not available in this state - "
					"user must try to send new input\n");
				continue;
			}
			else if (ret == AVERROR(EINVAL))
			{
				printf("video avcodec_receive_frame(): codec not opened, or it is an encoder\n");
			}
			else
			{
				printf("video avcodec_receive_frame(): legitimate decoding errors\n");
			}

			res = -1;
			goto exit6;
		}

		sws_scale(sws_ctx,                                  // sws context
			(const uint8_t* const*)p_frm_raw->data,  // src slice
			p_frm_raw->linesize,                      // src stride
			0,                                        // src slice y
			p_codec_ctx->height,                      // src slice height
			p_frm_yuv->data,                          // dst planes
			p_frm_yuv->linesize                       // dst strides
		);


		SDL_UpdateYUVTexture(sdl_texture,                   // sdl texture
			&sdl_rect,                     // sdl rect
			p_frm_yuv->data[0],            // y plane
			p_frm_yuv->linesize[0],        // y pitch
			p_frm_yuv->data[1],            // u plane
			p_frm_yuv->linesize[1],        // u pitch
			p_frm_yuv->data[2],            // v plane
			p_frm_yuv->linesize[2]         // v pitch
		);


		SDL_RenderClear(sdl_renderer);

		SDL_RenderCopy(sdl_renderer,                        // sdl renderer
			sdl_texture,                         // sdl texture
			NULL,                                // src rect, if NULL copy texture
			&sdl_rect                            // dst rect
		);

		SDL_RenderPresent(sdl_renderer);

		while (true)
		{
			SDL_WaitEvent(&sdl_event);
			if (sdl_event.type == SDL_USEREVENT_REFRESH)
				break;
		}
	}

exit6:
	if (p_packet != NULL)
	{
		av_packet_unref(p_packet);
	}
exit5:
	sws_freeContext(sws_ctx);
exit4:
	av_free(buffer);
exit3:
	av_frame_free(&p_frm_yuv);
exit2:
	av_frame_free(&p_frm_raw);
exit1:
	avcodec_close(p_codec_ctx);
exit0:
	return res;
}

int open_audio_stream(AVFormatContext* p_fmt_ctx, AVCodecContext* p_codec_ctx, int steam_idx)
{
	AVCodecParameters* p_codec_par = NULL;
	SDL_AudioSpec wanted_spec;
	SDL_AudioSpec actual_spec;
	int ret;

	packet_queue_init(&s_audio_pkt_queue);

	p_codec_par = p_fmt_ctx->streams[steam_idx]->codecpar;

	const AVCodec* p_codec = avcodec_find_decoder(p_codec_par->codec_id);
	if (p_codec == NULL)
	{
		printf("Cann't find codec!\n");
		return -1;
	}

	p_codec_ctx = avcodec_alloc_context3(p_codec);
	if (p_codec_ctx == NULL)
	{
		printf("avcodec_alloc_context3() failed\n");
		return -1;
	}

	ret = avcodec_parameters_to_context(p_codec_ctx, p_codec_par);
	if (ret < 0)
	{
		printf("avcodec_parameters_to_context() failed %d\n", ret);
		return -1;
	}

	ret = avcodec_open2(p_codec_ctx, p_codec, NULL);
	if (ret < 0)
	{
		printf("avcodec_open2() failed %d\n", ret);
		return -1;
	}

	wanted_spec.freq = p_codec_ctx->sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = p_codec_ctx->channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
	wanted_spec.callback = sdl_audio_callback;
	wanted_spec.userdata = p_codec_ctx;
	if (SDL_OpenAudio(&wanted_spec, &actual_spec) < 0)
	{
		printf("SDL_OpenAudio() failed: %s\n", SDL_GetError());
		return -1;
	}

	s_audio_param_tgt.fmt = AV_SAMPLE_FMT_S16;
	s_audio_param_tgt.freq = actual_spec.freq;
	s_audio_param_tgt.channel_layout = av_get_default_channel_layout(actual_spec.channels);;
	s_audio_param_tgt.channels = actual_spec.channels;
	s_audio_param_tgt.frame_size = av_samples_get_buffer_size(NULL, actual_spec.channels, 1, s_audio_param_tgt.fmt, 1);
	s_audio_param_tgt.bytes_per_sec = av_samples_get_buffer_size(NULL, actual_spec.channels, actual_spec.freq, s_audio_param_tgt.fmt, 1);
	if (s_audio_param_tgt.bytes_per_sec <= 0 || s_audio_param_tgt.frame_size <= 0)
	{
		printf("av_samples_get_buffer_size failed\n");
		return -1;
	}
	s_audio_param_src = s_audio_param_tgt;

	SDL_PauseAudio(0);

	return 0;
}


int open_video_stream(AVFormatContext* p_fmt_ctx, AVCodecContext* p_codec_ctx, int steam_idx)
{
	AVCodecParameters* p_codec_par = NULL;

	int ret;

	packet_queue_init(&s_video_pkt_queue);

	p_codec_par = p_fmt_ctx->streams[steam_idx]->codecpar;

	const AVCodec* p_codec = avcodec_find_decoder(p_codec_par->codec_id);
	if (p_codec == NULL)
	{
		printf("Cann't find codec!\n");
		return -1;
	}

	p_codec_ctx = avcodec_alloc_context3(p_codec);
	if (p_codec_ctx == NULL)
	{
		printf("avcodec_alloc_context3() failed\n");
		return -1;
	}

	ret = avcodec_parameters_to_context(p_codec_ctx, p_codec_par);
	if (ret < 0)
	{
		printf("avcodec_parameters_to_context() failed %d\n", ret);
		return -1;
	}

	ret = avcodec_open2(p_codec_ctx, p_codec, NULL);
	if (ret < 0)
	{
		printf("avcodec_open2() failed %d\n", ret);
		return -1;
	}

	int temp_num = p_fmt_ctx->streams[steam_idx]->avg_frame_rate.num;
	int temp_den = p_fmt_ctx->streams[steam_idx]->avg_frame_rate.den;
	int frame_rate = (temp_den > 0) ? temp_num / temp_den : 25;
	int interval = (temp_num > 0) ? (temp_den * 1000) / temp_num : 40;

	printf("frame rate %d FPS, refresh interval %d ms\n", frame_rate, interval);

	SDL_AddTimer(interval, sdl_time_cb_refresh, NULL);

	SDL_CreateThread(video_thread, "video thread", p_codec_ctx);

	return 0;
}

int main(int argc, char** argv) {
	//01 Mitsukiyo 01 Constant Moderato.flac
	//V201 MP4 - AVC 1080p 24fps 8bit - AAC2.0.mp4
	std::string filePath = ".\\TestData\\V201 MP4 - AVC 1080p 24fps 8bit - AAC2.0.mp4";

	AVFormatContext* p_fmt_ctx = NULL;
	AVCodecContext* p_acodec_ctx = NULL;
	AVCodecContext* p_vcodec_ctx = NULL;
	AVPacket* p_packet = NULL;
	int i = 0;
	int a_idx = -1;
	int v_idx = -1;
	int ret = 0;
	int res = 0;

	//if (argc < 2)
	//{
	//	printf("Please provide a movie file\n");
	//	return -1;
	//}

	//set windows environment variable
	SDL_setenv("SDL_AUDIODRIVER", "directsound", 1);

	if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER))
	{
		printf("SDL_Init() failed: %s\n", SDL_GetError());
		res = -1;
		goto exit2;
	}

	ret = avformat_open_input(&p_fmt_ctx, filePath.c_str(), NULL, NULL);
	if (ret != 0)
	{
		printf("avformat_open_input() failed %d\n", ret);
		res = -1;
		goto exit0;
	}

	ret = avformat_find_stream_info(p_fmt_ctx, NULL);
	if (ret < 0)
	{
		printf("avformat_find_stream_info() failed %d\n", ret);
		res = -1;
		goto exit1;
	}

	av_dump_format(p_fmt_ctx, 0, argv[1], 0);

	a_idx = -1;
	v_idx = -1;
	for (i = 0; i < p_fmt_ctx->nb_streams; i++)
	{
		if ((p_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) &&
			(a_idx == -1))
		{
			a_idx = i;
			printf("Find a audio stream, index %d\n", a_idx);
		}
		if ((p_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) &&
			(v_idx == -1))
		{
			v_idx = i;
			printf("Find a video stream, index %d\n", v_idx);
		}
		if (a_idx != -1 && v_idx != -1)
		{
			break;
		}
	}
	if (a_idx == -1 && v_idx == -1)
	{
		printf("Cann't find any audio/video stream\n");
		res = -1;
		goto exit1;
	}

	if (a_idx != -1) {
		open_audio_stream(p_fmt_ctx, p_acodec_ctx, a_idx);
	}
	if (v_idx != -1) {
		open_video_stream(p_fmt_ctx, p_vcodec_ctx, v_idx);
	}

	p_packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	if (p_packet == NULL)
	{
		printf("av_malloc() failed\n");
		res = -1;
		goto exit2;
	}

	while (1)
	{
		if (packet_queue_num(&s_video_pkt_queue) > 100 ||
			packet_queue_num(&s_audio_pkt_queue) > 500)
		{
			av_usleep(10000);
			continue;
		}

		ret = av_read_frame(p_fmt_ctx, p_packet);

		if (ret < 0)
		{
			if ((ret == AVERROR_EOF) || avio_feof(p_fmt_ctx->pb))
			{
				printf("read end of file\n");
				p_packet->data = NULL;
				p_packet->size = 0;

				// The input file has been read, send a NULL packet to flush the decoder, otherwise the buffered frames in the decoder cannot be retrieved.
				if (v_idx != -1)
				{
					printf("push a flush packet into video queue\n");
					packet_queue_push(&s_video_pkt_queue, p_packet);
				}

				if (a_idx != -1)
				{
					printf("push a flush packet into audio queue\n");
					packet_queue_push(&s_audio_pkt_queue, p_packet);
				}

				break;
			}
			else
			{
				printf("read unexpected error\n");
				goto exit3;
			}
		}
		else
		{
			if (p_packet->stream_index == a_idx)
			{
				packet_queue_push(&s_audio_pkt_queue, p_packet);
			}
			else if (p_packet->stream_index == v_idx)
			{
				packet_queue_push(&s_video_pkt_queue, p_packet);
			}
			else
			{
				av_packet_unref(p_packet);
			}
		}
	}

	while (((a_idx >= 0) && (!s_adecode_finished)) ||
		((v_idx >= 0) && (!s_vdecode_finished)))
	{
		SDL_Delay(100);
	}

	printf("play finishied. exit now...");

	SDL_Delay(200);

exit3:
	SDL_Quit();
exit2:
	av_packet_unref(p_packet);
exit1:
	avformat_close_input(&p_fmt_ctx);
exit0:
	return res;
}