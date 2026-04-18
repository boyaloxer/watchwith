#include "media-codec.h"

#include <obs.h>

#include <algorithm>
#include <cstring>

/* ================================================================== */
/* VideoEncoder                                                       */
/* ================================================================== */

VideoEncoder::~VideoEncoder()
{
	shutdown();
}

void VideoEncoder::cleanup()
{
	if (sws) {
		sws_freeContext(sws);
		sws = nullptr;
	}
	if (pkt) {
		av_packet_free(&pkt);
		pkt = nullptr;
	}
	if (frame) {
		av_frame_free(&frame);
		frame = nullptr;
	}
	if (ctx) {
		avcodec_free_context(&ctx);
		ctx = nullptr;
	}
	lastSrcW = lastSrcH = 0;
	frameCount = 0;
}

bool VideoEncoder::init(uint32_t outW, uint32_t outH, uint32_t fps, uint32_t bitrateBps)
{
	std::lock_guard<std::mutex> lock(mtx);
	cleanup();

	encW = outW;
	encH = outH;

	const AVCodec *codec = avcodec_find_encoder_by_name("libx264");
	if (!codec)
		codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!codec) {
		blog(LOG_ERROR, "VideoEncoder: H264 encoder not found");
		return false;
	}

	ctx = avcodec_alloc_context3(codec);
	if (!ctx)
		return false;

	ctx->width = (int)outW;
	ctx->height = (int)outH;
	ctx->pix_fmt = AV_PIX_FMT_YUV420P;
	ctx->time_base = AVRational{1, (int)fps};
	ctx->framerate = AVRational{(int)fps, 1};
	ctx->bit_rate = bitrateBps;
	ctx->rc_max_rate = bitrateBps;
	ctx->rc_buffer_size = (int)(bitrateBps / 2);
	ctx->gop_size = 60;
	ctx->max_b_frames = 0;
	ctx->thread_count = 2;
	ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;

	av_opt_set(ctx->priv_data, "preset", "ultrafast", 0);
	av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);

	if (avcodec_open2(ctx, codec, nullptr) < 0) {
		blog(LOG_ERROR, "VideoEncoder: failed to open codec");
		avcodec_free_context(&ctx);
		ctx = nullptr;
		return false;
	}

	frame = av_frame_alloc();
	frame->format = ctx->pix_fmt;
	frame->width = (int)outW;
	frame->height = (int)outH;
	if (av_frame_get_buffer(frame, 0) < 0) {
		blog(LOG_ERROR, "VideoEncoder: failed to alloc frame buffer");
		cleanup();
		return false;
	}

	pkt = av_packet_alloc();

	blog(LOG_INFO, "VideoEncoder: %s %dx%d @ %dfps %dkbps", codec->name, outW, outH, fps,
	     bitrateBps / 1000);
	return true;
}

void VideoEncoder::encode(const uint8_t *const srcData[], const uint32_t srcLinesize[],
			   uint32_t srcW, uint32_t srcH, int64_t,
			   std::function<void(const uint8_t *, size_t, bool)> onPacket)
{
	std::lock_guard<std::mutex> lock(mtx);
	if (!ctx || !frame || !pkt)
		return;

	if (srcW != lastSrcW || srcH != lastSrcH) {
		if (sws)
			sws_freeContext(sws);
		sws = sws_getContext((int)srcW, (int)srcH, AV_PIX_FMT_NV12, (int)encW, (int)encH,
				     AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
		lastSrcW = srcW;
		lastSrcH = srcH;
		if (!sws) {
			blog(LOG_ERROR, "VideoEncoder: failed to create scaler %dx%d -> %dx%d", srcW,
			     srcH, encW, encH);
			return;
		}
	}

	av_frame_make_writable(frame);

	int srcStride[] = {(int)srcLinesize[0], (int)srcLinesize[1]};
	sws_scale(sws, srcData, srcStride, 0, (int)srcH, frame->data, frame->linesize);

	frame->pts = frameCount++;

	if (avcodec_send_frame(ctx, frame) < 0)
		return;

	while (avcodec_receive_packet(ctx, pkt) == 0) {
		bool keyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
		onPacket(pkt->data, pkt->size, keyframe);
		av_packet_unref(pkt);
	}
}

void VideoEncoder::shutdown()
{
	std::lock_guard<std::mutex> lock(mtx);
	cleanup();
}

/* ================================================================== */
/* VideoDecoder                                                       */
/* ================================================================== */

VideoDecoder::~VideoDecoder()
{
	shutdown();
}

void VideoDecoder::cleanup()
{
	if (sws) {
		sws_freeContext(sws);
		sws = nullptr;
	}
	if (pkt) {
		av_packet_free(&pkt);
		pkt = nullptr;
	}
	if (nv12Frame) {
		av_frame_free(&nv12Frame);
		nv12Frame = nullptr;
	}
	if (frame) {
		av_frame_free(&frame);
		frame = nullptr;
	}
	if (ctx) {
		avcodec_free_context(&ctx);
		ctx = nullptr;
	}
	lastDecW = lastDecH = 0;
}

bool VideoDecoder::init()
{
	std::lock_guard<std::mutex> lock(mtx);
	cleanup();

	const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	if (!codec) {
		blog(LOG_ERROR, "VideoDecoder: H264 decoder not found");
		return false;
	}

	ctx = avcodec_alloc_context3(codec);
	if (!ctx)
		return false;

	ctx->thread_count = 2;
	ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;

	if (avcodec_open2(ctx, codec, nullptr) < 0) {
		blog(LOG_ERROR, "VideoDecoder: failed to open codec");
		avcodec_free_context(&ctx);
		ctx = nullptr;
		return false;
	}

	frame = av_frame_alloc();
	nv12Frame = av_frame_alloc();
	pkt = av_packet_alloc();

	blog(LOG_INFO, "VideoDecoder: %s initialized", codec->name);
	return true;
}

void VideoDecoder::decode(
	const uint8_t *data, size_t size,
	std::function<void(uint32_t, uint32_t, const uint8_t *const[], const uint32_t[], int64_t)>
		onFrame)
{
	std::lock_guard<std::mutex> lock(mtx);
	if (!ctx || !frame || !pkt)
		return;

	pkt->data = const_cast<uint8_t *>(data);
	pkt->size = (int)size;

	if (avcodec_send_packet(ctx, pkt) < 0)
		return;

	while (avcodec_receive_frame(ctx, frame) == 0) {
		uint32_t w = (uint32_t)frame->width;
		uint32_t h = (uint32_t)frame->height;

		if (w == 0 || h == 0 || w > 3840 || h > 2160)
			continue;

		if (w != lastDecW || h != lastDecH) {
			if (sws)
				sws_freeContext(sws);
			sws = sws_getContext((int)w, (int)h, (AVPixelFormat)frame->format, (int)w,
					     (int)h, AV_PIX_FMT_NV12, SWS_BILINEAR, nullptr, nullptr,
					     nullptr);
			lastDecW = w;
			lastDecH = h;

			av_frame_unref(nv12Frame);
			nv12Frame->format = AV_PIX_FMT_NV12;
			nv12Frame->width = (int)w;
			nv12Frame->height = (int)h;
			if (av_frame_get_buffer(nv12Frame, 0) < 0)
				continue;
		}

		if (!sws)
			continue;

		av_frame_make_writable(nv12Frame);
		sws_scale(sws, frame->data, frame->linesize, 0, (int)h, nv12Frame->data,
			  nv12Frame->linesize);

		uint32_t linesize[2] = {(uint32_t)nv12Frame->linesize[0],
					(uint32_t)nv12Frame->linesize[1]};
		onFrame(w, h, (const uint8_t *const *)nv12Frame->data, linesize, frame->pts);
	}
}

void VideoDecoder::shutdown()
{
	std::lock_guard<std::mutex> lock(mtx);
	cleanup();
}

/* ================================================================== */
/* AudioEncoder                                                       */
/* ================================================================== */

AudioEncoder::~AudioEncoder()
{
	shutdown();
}

void AudioEncoder::cleanup()
{
	if (pkt) {
		av_packet_free(&pkt);
		pkt = nullptr;
	}
	if (frame) {
		av_frame_free(&frame);
		frame = nullptr;
	}
	if (swr) {
		swr_free(&swr);
		swr = nullptr;
	}
	if (ctx) {
		avcodec_free_context(&ctx);
		ctx = nullptr;
	}
	buffer.clear();
	buffered = 0;
	pts = 0;
	frameSize = 0;
	encChannels = 0;
}

bool AudioEncoder::init(uint32_t sampleRate, uint32_t channels, uint32_t bitrateBps)
{
	std::lock_guard<std::mutex> lock(mtx);
	cleanup();

	encChannels = channels;

	const AVCodec *codec = avcodec_find_encoder_by_name("libopus");
	if (!codec)
		codec = avcodec_find_encoder(AV_CODEC_ID_OPUS);
	if (!codec) {
		codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
		if (!codec) {
			blog(LOG_ERROR, "AudioEncoder: no suitable encoder found");
			return false;
		}
	}

	ctx = avcodec_alloc_context3(codec);
	if (!ctx)
		return false;

	ctx->sample_rate = (int)sampleRate;
	ctx->bit_rate = bitrateBps;
	av_channel_layout_default(&ctx->ch_layout, (int)channels);

	if (codec->sample_fmts)
		ctx->sample_fmt = codec->sample_fmts[0];
	else
		ctx->sample_fmt = AV_SAMPLE_FMT_FLT;

	if (avcodec_open2(ctx, codec, nullptr) < 0) {
		blog(LOG_ERROR, "AudioEncoder: failed to open codec");
		avcodec_free_context(&ctx);
		ctx = nullptr;
		return false;
	}

	frameSize = ctx->frame_size;
	if (frameSize <= 0)
		frameSize = 960;

	frame = av_frame_alloc();
	frame->format = ctx->sample_fmt;
	av_channel_layout_copy(&frame->ch_layout, &ctx->ch_layout);
	frame->sample_rate = ctx->sample_rate;
	frame->nb_samples = frameSize;
	if (av_frame_get_buffer(frame, 0) < 0) {
		blog(LOG_ERROR, "AudioEncoder: failed to alloc frame buffer");
		cleanup();
		return false;
	}

	pkt = av_packet_alloc();

	AVChannelLayout inLayout;
	av_channel_layout_default(&inLayout, (int)channels);

	int ret = swr_alloc_set_opts2(&swr, &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate,
				      &inLayout, AV_SAMPLE_FMT_FLTP, (int)sampleRate, 0, nullptr);
	av_channel_layout_uninit(&inLayout);

	if (ret < 0 || swr_init(swr) < 0) {
		blog(LOG_ERROR, "AudioEncoder: failed to init resampler");
		cleanup();
		return false;
	}

	buffer.resize(channels);
	buffered = 0;
	pts = 0;

	blog(LOG_INFO, "AudioEncoder: %s %dHz %dch %dkbps frame_size=%d fmt=%d", codec->name,
	     sampleRate, channels, bitrateBps / 1000, frameSize, ctx->sample_fmt);
	return true;
}

void AudioEncoder::encode(const uint8_t *const planarData[], uint32_t frames, uint32_t channels,
			   std::function<void(const uint8_t *, size_t)> onPacket)
{
	std::lock_guard<std::mutex> lock(mtx);
	if (!ctx || !frame || !pkt || !swr)
		return;

	uint32_t ch = std::min(channels, encChannels);
	for (uint32_t c = 0; c < ch; c++) {
		const float *src = reinterpret_cast<const float *>(planarData[c]);
		buffer[c].insert(buffer[c].end(), src, src + frames);
	}
	for (uint32_t c = ch; c < encChannels; c++)
		buffer[c].insert(buffer[c].end(), (size_t)frames, 0.0f);
	buffered += (int)frames;

	encodeBuffered(onPacket);
}

void AudioEncoder::encodeBuffered(std::function<void(const uint8_t *, size_t)> &onPacket)
{
	while (buffered >= frameSize) {
		std::vector<const uint8_t *> srcSlices(encChannels);
		for (uint32_t c = 0; c < encChannels; c++)
			srcSlices[c] = reinterpret_cast<const uint8_t *>(buffer[c].data());

		av_frame_make_writable(frame);
		frame->nb_samples = frameSize;

		int converted =
			swr_convert(swr, frame->data, frameSize, srcSlices.data(), frameSize);
		if (converted <= 0)
			break;

		frame->nb_samples = converted;
		frame->pts = pts;
		pts += converted;

		if (avcodec_send_frame(ctx, frame) < 0)
			break;

		while (avcodec_receive_packet(ctx, pkt) == 0) {
			onPacket(pkt->data, pkt->size);
			av_packet_unref(pkt);
		}

		for (uint32_t c = 0; c < encChannels; c++)
			buffer[c].erase(buffer[c].begin(), buffer[c].begin() + frameSize);
		buffered -= frameSize;
	}
}

void AudioEncoder::shutdown()
{
	std::lock_guard<std::mutex> lock(mtx);
	cleanup();
}

/* ================================================================== */
/* AudioDecoder                                                       */
/* ================================================================== */

AudioDecoder::~AudioDecoder()
{
	shutdown();
}

void AudioDecoder::cleanup()
{
	if (pkt) {
		av_packet_free(&pkt);
		pkt = nullptr;
	}
	if (frame) {
		av_frame_free(&frame);
		frame = nullptr;
	}
	if (ctx) {
		avcodec_free_context(&ctx);
		ctx = nullptr;
	}
	planarBuf.clear();
	decChannels = 0;
	decSampleRate = 0;
}

bool AudioDecoder::init(uint32_t sampleRate, uint32_t channels)
{
	std::lock_guard<std::mutex> lock(mtx);
	cleanup();

	decChannels = channels;
	decSampleRate = sampleRate;

	const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
	if (!codec)
		codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
	if (!codec) {
		blog(LOG_ERROR, "AudioDecoder: no suitable decoder found");
		return false;
	}

	ctx = avcodec_alloc_context3(codec);
	if (!ctx)
		return false;

	ctx->sample_rate = (int)sampleRate;
	av_channel_layout_default(&ctx->ch_layout, (int)channels);

	if (avcodec_open2(ctx, codec, nullptr) < 0) {
		blog(LOG_ERROR, "AudioDecoder: failed to open codec");
		avcodec_free_context(&ctx);
		ctx = nullptr;
		return false;
	}

	frame = av_frame_alloc();
	pkt = av_packet_alloc();
	planarBuf.resize(channels);

	blog(LOG_INFO, "AudioDecoder: %s %dHz %dch", codec->name, sampleRate, channels);
	return true;
}

void AudioDecoder::decode(
	const uint8_t *data, size_t size,
	std::function<void(const float *const *, uint32_t, uint32_t, uint32_t)> onFrame)
{
	std::lock_guard<std::mutex> lock(mtx);
	if (!ctx || !frame || !pkt)
		return;

	pkt->data = const_cast<uint8_t *>(data);
	pkt->size = (int)size;

	if (avcodec_send_packet(ctx, pkt) < 0)
		return;

	while (avcodec_receive_frame(ctx, frame) == 0) {
		uint32_t frames = (uint32_t)frame->nb_samples;
		uint32_t ch = std::min((uint32_t)frame->ch_layout.nb_channels, decChannels);

		if (frames == 0 || ch == 0)
			continue;

		for (uint32_t c = 0; c < ch; c++)
			planarBuf[c].resize(frames);

		if (frame->format == AV_SAMPLE_FMT_FLTP) {
			for (uint32_t c = 0; c < ch; c++)
				memcpy(planarBuf[c].data(), frame->data[c], frames * sizeof(float));
		} else if (frame->format == AV_SAMPLE_FMT_FLT) {
			const float *src = reinterpret_cast<const float *>(frame->data[0]);
			for (uint32_t f = 0; f < frames; f++)
				for (uint32_t c = 0; c < ch; c++)
					planarBuf[c][f] = src[f * ch + c];
		} else if (frame->format == AV_SAMPLE_FMT_S16) {
			const int16_t *src = reinterpret_cast<const int16_t *>(frame->data[0]);
			for (uint32_t f = 0; f < frames; f++)
				for (uint32_t c = 0; c < ch; c++)
					planarBuf[c][f] = src[f * ch + c] / 32768.0f;
		} else if (frame->format == AV_SAMPLE_FMT_S16P) {
			for (uint32_t c = 0; c < ch; c++) {
				const int16_t *src =
					reinterpret_cast<const int16_t *>(frame->data[c]);
				for (uint32_t f = 0; f < frames; f++)
					planarBuf[c][f] = src[f] / 32768.0f;
			}
		} else {
			blog(LOG_WARNING, "AudioDecoder: unsupported sample format %d",
			     frame->format);
			continue;
		}

		std::vector<const float *> ptrs(ch);
		for (uint32_t c = 0; c < ch; c++)
			ptrs[c] = planarBuf[c].data();

		onFrame(ptrs.data(), frames, ch, decSampleRate);
	}
}

void AudioDecoder::shutdown()
{
	std::lock_guard<std::mutex> lock(mtx);
	cleanup();
}
