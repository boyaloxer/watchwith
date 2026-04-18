#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

class VideoEncoder {
public:
	~VideoEncoder();

	bool init(uint32_t outW, uint32_t outH, uint32_t fps, uint32_t bitrateBps);
	void encode(const uint8_t *const srcData[], const uint32_t srcLinesize[],
		    uint32_t srcW, uint32_t srcH, int64_t pts,
		    std::function<void(const uint8_t *, size_t, bool)> onPacket);
	void shutdown();

private:
	void cleanup();

	AVCodecContext *ctx = nullptr;
	AVFrame *frame = nullptr;
	AVPacket *pkt = nullptr;
	SwsContext *sws = nullptr;
	uint32_t lastSrcW = 0, lastSrcH = 0;
	uint32_t encW = 0, encH = 0;
	int64_t frameCount = 0;
	std::mutex mtx;
};

class VideoDecoder {
public:
	~VideoDecoder();

	bool init();
	void decode(const uint8_t *data, size_t size,
		    std::function<void(uint32_t w, uint32_t h, const uint8_t *const data[],
				       const uint32_t linesize[], int64_t pts)>
			    onFrame);
	void shutdown();

private:
	void cleanup();

	AVCodecContext *ctx = nullptr;
	AVFrame *frame = nullptr;
	AVFrame *nv12Frame = nullptr;
	AVPacket *pkt = nullptr;
	SwsContext *sws = nullptr;
	uint32_t lastDecW = 0, lastDecH = 0;
	std::mutex mtx;
};

class AudioEncoder {
public:
	~AudioEncoder();

	bool init(uint32_t sampleRate, uint32_t channels, uint32_t bitrateBps);
	void encode(const uint8_t *const planarData[], uint32_t frames, uint32_t channels,
		    std::function<void(const uint8_t *, size_t)> onPacket);
	void shutdown();

private:
	void cleanup();
	void encodeBuffered(std::function<void(const uint8_t *, size_t)> &onPacket);

	AVCodecContext *ctx = nullptr;
	AVFrame *frame = nullptr;
	AVPacket *pkt = nullptr;
	SwrContext *swr = nullptr;
	int frameSize = 0;
	uint32_t encChannels = 0;
	int64_t pts = 0;
	std::vector<std::vector<float>> buffer;
	int buffered = 0;
	std::mutex mtx;
};

class AudioDecoder {
public:
	~AudioDecoder();

	bool init(uint32_t sampleRate, uint32_t channels);
	void decode(const uint8_t *data, size_t size,
		    std::function<void(const float *const *, uint32_t frames, uint32_t channels,
				       uint32_t sampleRate)>
			    onFrame);
	void shutdown();

private:
	void cleanup();

	AVCodecContext *ctx = nullptr;
	AVFrame *frame = nullptr;
	AVPacket *pkt = nullptr;
	uint32_t decChannels = 0;
	uint32_t decSampleRate = 0;
	std::vector<std::vector<float>> planarBuf;
	std::mutex mtx;
};
