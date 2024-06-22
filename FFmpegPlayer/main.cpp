#include <stdio.h>
#include <Windows.h>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <wrl.h>
#include "Decoder.h"
#include "Render.h"
using namespace std;
using Microsoft::WRL::ComPtr;

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
		else if (!isend) {
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