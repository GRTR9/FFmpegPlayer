#pragma once
#include <Windows.h>
#include "Decoder.h"
#include <wrl.h>

using Microsoft::WRL::ComPtr;

void RenderHWFrame(HWND hwnd, AVFrame* frame);