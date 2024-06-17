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

#include <d3d9.h>
#pragma comment(lib, "d3d9.lib")
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

	//hardware
	AVBufferRef* hw_device_ctx = nullptr;
	av_hwdevice_ctx_create(&hw_device_ctx, AVHWDeviceType::AV_HWDEVICE_TYPE_DXVA2, NULL, NULL, NULL);
	vcodecCtx->hw_device_ctx = hw_device_ctx;

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
		else if (ret < 0) {
			return nullptr;
		}

		av_packet_unref(packet);
	}

	return nullptr;
}

void RenderHWFrame(HWND hwnd, AVFrame* frame) {
	IDirect3DSurface9* surface = (IDirect3DSurface9*)frame->data[3];
	IDirect3DDevice9* device;
	surface->GetDevice(&device);

	static ComPtr<IDirect3DSwapChain9> mySwap;
	if (mySwap == nullptr) {
		D3DPRESENT_PARAMETERS params = {};
		params.Windowed = TRUE;
		params.hDeviceWindow = hwnd;
		params.BackBufferFormat = D3DFORMAT::D3DFMT_X8R8G8B8;
		params.BackBufferWidth = frame->width;
		params.BackBufferHeight = frame->height;
		params.SwapEffect = D3DSWAPEFFECT_DISCARD;
		params.BackBufferCount = 1;
		params.Flags = 0;
		device->CreateAdditionalSwapChain(&params, mySwap.GetAddressOf());
	}

	ComPtr<IDirect3DSurface9> backSurface;
	mySwap->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, backSurface.GetAddressOf());

	device->StretchRect(surface, NULL, backSurface.Get(), NULL, D3DTEXF_LINEAR);

	mySwap->Present(NULL, NULL, NULL, NULL, NULL);
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
	string filePath = ".\\TestData\\Big_Buck_Bunny_1080_10s_1MB.mp4";
	DecoderParam decoderParam;
	InitDecoder(filePath.c_str(), decoderParam);
	auto& width = decoderParam.width;
	auto& height = decoderParam.height;
	auto& fmtCtx = decoderParam.fmtCtx;
	auto& vcodecCtx = decoderParam.vcodecCtx;

	auto window = CreateWindow(className, L"FFmpegPlayer", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, NULL, NULL, NULL, NULL);

	ShowWindow(window, SW_SHOW);

	vector<uint8_t> buffer(width * height * 4);
	MSG msg;
	bool isend = false;
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
		else if(!isend) {
			AVFrame* frame = RequestFrame(decoderParam);
			if (frame == nullptr) {
				isend = true;
				av_frame_free(&frame);
				avcodec_free_context(&vcodecCtx);
				avformat_close_input(&fmtCtx);
				continue;
			}
			double framerate = (double)vcodecCtx->framerate.den / vcodecCtx->framerate.num;
			std::this_thread::sleep_until(currentTime + chrono::milliseconds((int)(framerate * 1000)));
			currentTime = chrono::system_clock::now();

			RenderHWFrame(window, frame);

			av_frame_free(&frame);
		}
	}
	return 0;
}