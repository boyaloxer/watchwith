#include "media-bridge.h"
#include "peer-connection.h"
#include "../app.h"

#include <obs.h>
#include <util/platform.h>

#include <cstring>

/* ------------------------------------------------------------------ */
/* Custom OBS source: "remote_video"                                  */
/* Receives raw NV12 frames pushed from the network                   */
/* ------------------------------------------------------------------ */

struct remote_video_data {
	obs_source_t *source;
	uint32_t width;
	uint32_t height;
};

static const char *remote_video_get_name(void *)
{
	return "Remote Video";
}

static void *remote_video_create(obs_data_t *, obs_source_t *source)
{
	auto *d = new remote_video_data();
	d->source = source;
	d->width = 1280;
	d->height = 720;
	return d;
}

static void remote_video_destroy(void *data)
{
	delete static_cast<remote_video_data *>(data);
}

static uint32_t remote_video_get_width(void *data)
{
	return static_cast<remote_video_data *>(data)->width;
}

static uint32_t remote_video_get_height(void *data)
{
	return static_cast<remote_video_data *>(data)->height;
}

static obs_source_info remote_video_info = {};

void register_remote_source()
{
	remote_video_info.id = "remote_video";
	remote_video_info.type = OBS_SOURCE_TYPE_INPUT;
	remote_video_info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO;
	remote_video_info.get_name = remote_video_get_name;
	remote_video_info.create = remote_video_create;
	remote_video_info.destroy = remote_video_destroy;
	remote_video_info.get_width = remote_video_get_width;
	remote_video_info.get_height = remote_video_get_height;
	obs_register_source(&remote_video_info);
}

/* ------------------------------------------------------------------ */
/* MediaBridge implementation                                         */
/* ------------------------------------------------------------------ */

MediaBridge::MediaBridge(QObject *parent) : QObject(parent) {}

MediaBridge::~MediaBridge()
{
	stop();
}

void MediaBridge::start(PeerConnection *peer)
{
	if (running)
		stop();

	peerConnection = peer;
	running = true;

	video_t *video = obs_get_video();
	audio_t *audio = obs_get_audio();

	if (video)
		video_output_connect(video, nullptr, rawVideoCallback, this);
	if (audio)
		audio_output_connect(audio, 0, nullptr, rawAudioCallback, this);

	connect(peer, &PeerConnection::videoFrameReceived, this, &MediaBridge::onRemoteVideoFrame);
	connect(peer, &PeerConnection::audioFrameReceived, this, &MediaBridge::onRemoteAudioFrame);

	addRemoteSourceToScene();

	blog(LOG_INFO, "MediaBridge started");
}

void MediaBridge::stop()
{
	if (!running)
		return;

	running = false;

	video_t *video = obs_get_video();
	audio_t *audio = obs_get_audio();

	if (video)
		video_output_disconnect(video, rawVideoCallback, this);
	if (audio)
		audio_output_disconnect(audio, 0, rawAudioCallback, this);

	if (peerConnection) {
		disconnect(peerConnection, &PeerConnection::videoFrameReceived, this,
			   &MediaBridge::onRemoteVideoFrame);
		disconnect(peerConnection, &PeerConnection::audioFrameReceived, this,
			   &MediaBridge::onRemoteAudioFrame);
		peerConnection = nullptr;
	}

	remoteVideoSource = nullptr;

	blog(LOG_INFO, "MediaBridge stopped");
}

void MediaBridge::addRemoteSourceToScene()
{
	obs_scene_t *scene = App()->getScene();
	if (!scene)
		return;

	OBSDataAutoRelease settings = obs_data_create();
	remoteVideoSource = obs_source_create("remote_video", "Partner Video", settings, nullptr);
	if (!remoteVideoSource)
		return;

	obs_sceneitem_t *item = obs_scene_add(scene, remoteVideoSource);
	if (item) {
		struct vec2 pos = {20.0f, 20.0f};
		obs_sceneitem_set_pos(item, &pos);
	}
}

void MediaBridge::rawVideoCallback(void *param, struct video_data *frame)
{
	auto *bridge = static_cast<MediaBridge *>(param);
	if (!bridge->running || !bridge->peerConnection)
		return;

	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;

	uint32_t w = ovi.output_width;
	uint32_t h = ovi.output_height;

	size_t ySize = (size_t)frame->linesize[0] * h;
	size_t uvSize = (size_t)frame->linesize[1] * (h / 2);
	size_t headerSize = 16;
	size_t totalSize = headerSize + ySize + uvSize;

	/* Limit bandwidth: skip if frame is too large (> 2MB) */
	if (totalSize > 2 * 1024 * 1024)
		return;

	std::vector<uint8_t> buf(totalSize);
	uint32_t ts = (uint32_t)(frame->timestamp / 1000000);
	uint32_t linesize0 = frame->linesize[0];

	memcpy(buf.data() + 0, &w, 4);
	memcpy(buf.data() + 4, &h, 4);
	memcpy(buf.data() + 8, &ts, 4);
	memcpy(buf.data() + 12, &linesize0, 4);
	memcpy(buf.data() + headerSize, frame->data[0], ySize);
	memcpy(buf.data() + headerSize + ySize, frame->data[1], uvSize);

	bridge->peerConnection->sendVideoFrame(buf.data(), totalSize, ts);
}

void MediaBridge::rawAudioCallback(void *param, size_t, struct audio_data *data)
{
	auto *bridge = static_cast<MediaBridge *>(param);
	if (!bridge->running || !bridge->peerConnection)
		return;

	struct obs_audio_info ai;
	if (!obs_get_audio_info(&ai))
		return;

	uint32_t channels = get_audio_channels(ai.speakers);
	uint32_t frames = data->frames;
	uint32_t sampleRate = ai.samples_per_sec;

	size_t headerSize = 12;
	size_t sampleDataSize = (size_t)frames * channels * sizeof(float);
	size_t totalSize = headerSize + sampleDataSize;

	std::vector<uint8_t> buf(totalSize);
	memcpy(buf.data() + 0, &frames, 4);
	memcpy(buf.data() + 4, &channels, 4);
	memcpy(buf.data() + 8, &sampleRate, 4);

	float *dst = reinterpret_cast<float *>(buf.data() + headerSize);
	for (uint32_t f = 0; f < frames; f++) {
		for (uint32_t c = 0; c < channels; c++) {
			float *src = reinterpret_cast<float *>(data->data[c]);
			dst[f * channels + c] = src[f];
		}
	}

	bridge->peerConnection->sendAudioFrame(buf.data(), totalSize, 0);
}

void MediaBridge::onRemoteVideoFrame(const QByteArray &data)
{
	if (!remoteVideoSource || data.size() < 16)
		return;

	const uint8_t *raw = reinterpret_cast<const uint8_t *>(data.constData());
	uint32_t w, h, ts, linesize;
	memcpy(&w, raw + 0, 4);
	memcpy(&h, raw + 4, 4);
	memcpy(&ts, raw + 8, 4);
	memcpy(&linesize, raw + 12, 4);

	if (w == 0 || h == 0 || w > 3840 || h > 2160)
		return;

	size_t headerSize = 16;
	size_t ySize = (size_t)linesize * h;
	size_t uvSize = (size_t)linesize * (h / 2);

	if ((size_t)data.size() < headerSize + ySize + uvSize)
		return;

	auto *srcData = static_cast<remote_video_data *>(obs_obj_get_data(remoteVideoSource));
	if (srcData) {
		srcData->width = w;
		srcData->height = h;
	}

	struct obs_source_frame frame = {};
	frame.width = w;
	frame.height = h;
	frame.format = VIDEO_FORMAT_NV12;
	frame.timestamp = (uint64_t)ts * 1000000ULL;
	frame.data[0] = const_cast<uint8_t *>(raw + headerSize);
	frame.data[1] = const_cast<uint8_t *>(raw + headerSize + ySize);
	frame.linesize[0] = linesize;
	frame.linesize[1] = linesize;

	obs_source_output_video(remoteVideoSource, &frame);
}

void MediaBridge::onRemoteAudioFrame(const QByteArray &data)
{
	if (!remoteVideoSource || data.size() < 12)
		return;

	const uint8_t *raw = reinterpret_cast<const uint8_t *>(data.constData());
	uint32_t frames, channels, sampleRate;
	memcpy(&frames, raw + 0, 4);
	memcpy(&channels, raw + 4, 4);
	memcpy(&sampleRate, raw + 8, 4);

	if (frames == 0 || channels == 0 || channels > 8)
		return;

	size_t headerSize = 12;
	size_t expectedSize = headerSize + (size_t)frames * channels * sizeof(float);
	if ((size_t)data.size() < expectedSize)
		return;

	const float *src = reinterpret_cast<const float *>(raw + headerSize);

	struct obs_source_audio audio = {};
	audio.samples_per_sec = sampleRate;
	audio.frames = frames;
	audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
	audio.timestamp = os_gettime_ns();

	switch (channels) {
	case 1:
		audio.speakers = SPEAKERS_MONO;
		break;
	case 2:
		audio.speakers = SPEAKERS_STEREO;
		break;
	default:
		audio.speakers = SPEAKERS_STEREO;
		channels = 2;
		break;
	}

	/* De-interleave into planar buffers */
	std::vector<std::vector<float>> planar(channels, std::vector<float>(frames));
	for (uint32_t f = 0; f < frames; f++) {
		for (uint32_t c = 0; c < channels; c++) {
			planar[c][f] = src[f * channels + c];
		}
	}

	for (uint32_t c = 0; c < channels; c++)
		audio.data[c] = reinterpret_cast<const uint8_t *>(planar[c].data());

	obs_source_output_audio(remoteVideoSource, &audio);
}
