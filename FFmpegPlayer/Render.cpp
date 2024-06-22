#include "Render.h"

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
