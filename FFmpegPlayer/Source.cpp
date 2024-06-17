#include <stdio.h>
#include <Windows.h>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <wrl.h>

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

using namespace std;
using Microsoft::WRL::ComPtr;

struct DecoderParam
{
	AVFormatContext* fmtCtx;
	AVCodecContext* vcodecCtx;
	int width;
	int height;
	int videoStreamIndex;
};

struct Color_RGB
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

void InitDecoder(const char* filePath, DecoderParam& param) {
	AVFormatContext* fmtCtx = nullptr;
	avformat_open_input(&fmtCtx, filePath, NULL, NULL);
	avformat_find_stream_info(fmtCtx, NULL);

	AVCodecContext* vcodecCtx = nullptr;
	for (int i = 0; i < fmtCtx->nb_streams; i++) {
		const AVCodec* codec = avcodec_find_decoder(fmtCtx->streams[i]->codecpar->codec_id);
		if (codec->type == AVMEDIA_TYPE_VIDEO) {
			param.videoStreamIndex = i;
			vcodecCtx = avcodec_alloc_context3(codec);
			avcodec_parameters_to_context(vcodecCtx, fmtCtx->streams[i]->codecpar);
			avcodec_open2(vcodecCtx, codec, NULL);
		}
	}

	param.fmtCtx = fmtCtx;
	param.vcodecCtx = vcodecCtx;
	param.width = vcodecCtx->width;
	param.height = vcodecCtx->height;
}

AVFrame* RequestFrame(DecoderParam& param) {
	auto& fmtCtx = param.fmtCtx;
	auto& vcodecCtx = param.vcodecCtx;
	auto& videoStreamIndex = param.videoStreamIndex;

	while (1) {
		AVPacket* packet = av_packet_alloc();
		int ret = av_read_frame(fmtCtx, packet);
		if (ret == 0 && packet->stream_index == videoStreamIndex) {
			ret = avcodec_send_packet(vcodecCtx, packet);
			if (ret == 0) {
				AVFrame* frame = av_frame_alloc();
				ret = avcodec_receive_frame(vcodecCtx, frame);
				if (ret == 0) {
					av_packet_unref(packet);
					return frame;
				}
				else if (ret == AVERROR(EAGAIN)) {
					av_frame_unref(frame);
				}
			}
		}

		av_packet_unref(packet);
	}

	return nullptr;
}

vector<Color_RGB> GetRGBPixels(AVFrame* frame, vector<Color_RGB>& buffer) {
	static SwsContext* swsctx = nullptr;
	swsctx = sws_getCachedContext(
		swsctx,
		frame->width, frame->height, (AVPixelFormat)frame->format,
		frame->width, frame->height, AVPixelFormat::AV_PIX_FMT_BGR24, NULL, NULL, NULL, NULL);

	uint8_t* data[] = { (uint8_t*)&buffer[0] };
	int linesize[] = { frame->width * 3 };
	sws_scale(swsctx, frame->data, frame->linesize, 0, frame->height, data, linesize);

	return buffer;
}

void StretchBits(HWND hwnd, const vector<Color_RGB>& bits, int width, int height) {
	auto hdc = GetDC(hwnd);
	BITMAPINFO bitinfo = {};
	auto& bmiHeader = bitinfo.bmiHeader;
	bmiHeader.biSize = sizeof(bitinfo.bmiHeader);
	bmiHeader.biWidth = width;
	bmiHeader.biHeight = -height;
	bmiHeader.biPlanes = 1;
	bmiHeader.biBitCount = 24;
	bmiHeader.biCompression = BI_RGB;

	StretchDIBits(hdc, 0, 0, width, height, 0, 0, width, height, &bits[0], &bitinfo, DIB_RGB_COLORS, SRCCOPY);
	ReleaseDC(hwnd, hdc);
}

int WINAPI WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine,
	_In_ int nShowCmd
) {
	SetProcessDPIAware();

	auto className = L"MyWindow";
	WNDCLASSW wndClass = {};
	wndClass.hInstance = NULL;
	wndClass.lpszClassName = className;
	wndClass.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
		switch (msg)
		{
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}
	};

	RegisterClass(&wndClass);

	//frame
	//test.264
	//Big_Buck_Bunny_1080_10s_1MB.mp4
	string filePath = ".\\TestData\\test.264";
	DecoderParam decoderParam;
	InitDecoder(filePath.c_str(), decoderParam);
	auto& width = decoderParam.width;
	auto& height = decoderParam.height;
	auto& fmtCtx = decoderParam.fmtCtx;
	auto& vcodecCtx = decoderParam.vcodecCtx;

	auto window = CreateWindow(className, L"Hello World Title", WS_OVERLAPPEDWINDOW, 0, 0, decoderParam.width, decoderParam.height, NULL, NULL, hInstance, NULL);

	ShowWindow(window, SW_SHOW);

	vector<Color_RGB> buffer(width * height);
	MSG msg;
	auto currentTime = chrono::system_clock::now();
	while (1) {
		BOOL hasMsg = PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
		if (hasMsg) {
			if (msg.message == WM_QUIT) {
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {
			AVFrame* frame = RequestFrame(decoderParam);

			vector<Color_RGB> pixels = GetRGBPixels(frame, buffer);

			av_frame_free(&frame);

			double framerate = (double)vcodecCtx->framerate.den / vcodecCtx->framerate.num;
			std::this_thread::sleep_until(currentTime + chrono::milliseconds((int)(framerate * 1000)));
			currentTime = chrono::system_clock::now();

			StretchBits(window, pixels, width, height);
		}
	}

	return 0;
}