#pragma once

// Pool of PyroWave output frames: N sets of 3 R16_UNORM plane textures
// (Y, Cb, Cr; chroma is full-res for 4:4:4, quarter-res for 4:2:0), each
// with an SRV for rendering and a TEXTURE2DARRAY UAV for the D3D11
// decoder. Created shared, the D3D12 decoder writes them instead.
//
// Frames travel through the existing pipeline as AVFrame: data[0..2] =
// ID3D11Texture2D* planes,
// data[3] = FrameSet*, format = AV_PIX_FMT_YUV444P16 / AV_PIX_FMT_YUV420P16
// (the PyroWave sentinels — nothing else in this app produces them; they
// also drive the renderer's 16-bit CSC scaling and chroma cositing). buf[0]
// carries a free callback that recycles the set, so every av_frame_free in
// Pacer/FrameQueue returns planes to the pool automatically.

#include <d3d11.h>
#include <mutex>
#include <vector>
#include <wrl/client.h>

extern "C" {
#include <libavutil/frame.h>
}

namespace moonlight_xbox_dx {
namespace PyroWaveD3D11 {

class FramePool;

struct FrameSet {
	Microsoft::WRL::ComPtr<ID3D11Texture2D> tex[3];
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv[3];
	// TEXTURE2DARRAY dimension, ArraySize 1 (D3D11Decoder plane contract)
	Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav[3];
	FramePool *pool = nullptr;
	int index = -1;
};

class FramePool {
  public:
	// count sets of 3 R16_UNORM planes at width x height (chroma planes
	// width/2 x height/2 when chroma444 is false). shared creates them with NT
	// handle sharing so the D3D12 decoder (PyroWaveD3D12::Decoder) can write them.
	bool Init(ID3D11Device *device, int width, int height, bool chroma444, int count, bool shared = false);

	// nullptr when all sets are in flight (caller should drop the frame).
	FrameSet *Acquire();

	// Wraps an acquired set into an AVFrame owning it: on av_frame_free the
	// set returns to the pool. Returns nullptr on alloc failure (the set is
	// recycled). Caller fills pts and color fields.
	AVFrame *WrapFrame(FrameSet *set);

	// Returns an acquired set that was never wrapped (e.g. its decode failed).
	void Recycle(int index);

  private:
	friend struct FrameSet;
	static void FreeCallback(void *opaque, uint8_t *data);

	std::vector<FrameSet> m_sets;
	std::vector<int> m_freeList;
	std::mutex m_mutex;
	int m_width = 0, m_height = 0;
	bool m_chroma444 = true;
};

} // namespace PyroWaveD3D11
} // namespace moonlight_xbox_dx
