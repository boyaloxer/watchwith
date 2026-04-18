#include "media-bridge.h"
#include "media-codec.h"
#include "peer-connection.h"
#include "../app.h"

#include <obs.h>
#include <util/platform.h>

#include <cstring>

/* ------------------------------------------------------------------ */
/* Custom OBS source: "remote_video"                                  */
/* Receives decoded NV12 frames pushed from the network               */
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

	struct obs_video_info ovi;
	if (obs_get_video_info(&ovi)) {
		uint32_t fps = ovi.fps_num / ovi.fps_den;
		if (fps == 0)
			fps = 30;
		videoEncoder = std::make_unique<VideoEncoder>();
		if (!videoEncoder->init(1280, 720, fps, 2500000))
			videoEncoder.reset();
	}

	struct obs_audio_info ai;
	if (obs_get_audio_info(&ai)) {
		uint32_t channels = get_audio_channels(ai.speakers);
		audioEncoder = std::make_unique<AudioEncoder>();
		if (!audioEncoder->init(ai.samples_per_sec, channels, 128000))
			audioEncoder.reset();
	}

	videoDecoder = std::make_unique<VideoDecoder>();
	if (!videoDecoder->init())
		videoDecoder.reset();

	if (obs_get_audio_info(&ai)) {
		uint32_t channels = get_audio_channels(ai.speakers);
		audioDecoder = std::make_unique<AudioDecoder>();
		if (!audioDecoder->init(ai.samples_per_sec, channels))
			audioDecoder.reset();
	}

	video_t *video = obs_get_video();
	audio_t *audio = obs_get_audio();

	if (video)
		video_output_connect(video, nullptr, rawVideoCallback, this);
	if (audio)
		audio_output_connect(audio, 0, nullptr, rawAudioCallback, this);

	connect(peer, &PeerConnection::videoFrameReceived, this, &MediaBridge::onRemoteVideoFrame);
	connect(peer, &PeerConnection::audioFrameReceived, this, &MediaBridge::onRemoteAudioFrame);

	addRemoteSourceToScene();

	blog(LOG_INFO, "MediaBridge started (H264 + Opus encoding)");
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

	videoEncoder.reset();
	videoDecoder.reset();
	audioEncoder.reset();
	audioDecoder.reset();

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

/* ------------------------------------------------------------------ */
/* Encoding callbacks (OBS threads -> encode -> send)                 */
/* ------------------------------------------------------------------ */

void MediaBridge::rawVideoCallback(void *param, struct video_data *frame)
{
	auto *bridge = static_cast<MediaBridge *>(param);
	if (!bridge->running || !bridge->peerConnection || !bridge->videoEncoder)
		return;

	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;

	uint32_t w = ovi.output_width;
	uint32_t h = ovi.output_height;

	bridge->videoEncoder->encode(
		(const uint8_t *const *)frame->data, frame->linesize, w, h, frame->timestamp,
		[bridge](const uint8_t *data, size_t size, bool) {
			bridge->peerConnection->sendVideoFrame(data, size, 0);
		});
}

void MediaBridge::rawAudioCallback(void *param, size_t, struct audio_data *data)
{
	auto *bridge = static_cast<MediaBridge *>(param);
	if (!bridge->running || !bridge->peerConnection || !bridge->audioEncoder)
		return;

	struct obs_audio_info ai;
	if (!obs_get_audio_info(&ai))
		return;

	uint32_t channels = get_audio_channels(ai.speakers);

	bridge->audioEncoder->encode(
		(const uint8_t *const *)data->data, data->frames, channels,
		[bridge](const uint8_t *encData, size_t encSize) {
			bridge->peerConnection->sendAudioFrame(encData, encSize, 0);
		});
}

/* ------------------------------------------------------------------ */
/* Decoding callbacks (receive -> decode -> OBS source)               */
/* ------------------------------------------------------------------ */

void MediaBridge::onRemoteVideoFrame(const QByteArray &data)
{
	if (!remoteVideoSource || !videoDecoder || data.isEmpty())
		return;

	const uint8_t *raw = reinterpret_cast<const uint8_t *>(data.constData());

	videoDecoder->decode(raw, (size_t)data.size(),
			     [this](uint32_t w, uint32_t h, const uint8_t *const frameData[],
				    const uint32_t linesize[], int64_t) {
				     auto *srcData = static_cast<remote_video_data *>(
					     obs_obj_get_data(remoteVideoSource));
				     if (srcData) {
					     srcData->width = w;
					     srcData->height = h;
				     }

				     struct obs_source_frame obsFrame = {};
				     obsFrame.width = w;
				     obsFrame.height = h;
				     obsFrame.format = VIDEO_FORMAT_NV12;
				     obsFrame.timestamp = os_gettime_ns();
				     obsFrame.data[0] = const_cast<uint8_t *>(frameData[0]);
				     obsFrame.data[1] = const_cast<uint8_t *>(frameData[1]);
				     obsFrame.linesize[0] = linesize[0];
				     obsFrame.linesize[1] = linesize[1];

				     obs_source_output_video(remoteVideoSource, &obsFrame);
			     });
}

void MediaBridge::onRemoteAudioFrame(const QByteArray &data)
{
	if (!remoteVideoSource || !audioDecoder || data.isEmpty())
		return;

	const uint8_t *raw = reinterpret_cast<const uint8_t *>(data.constData());

	audioDecoder->decode(
		raw, (size_t)data.size(),
		[this](const float *const *planar, uint32_t frames, uint32_t channels,
		       uint32_t sampleRate) {
			struct obs_source_audio audio = {};
			audio.samples_per_sec = sampleRate;
			audio.frames = frames;
			audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
			audio.timestamp = os_gettime_ns();

			switch (channels) {
			case 1:
				audio.speakers = SPEAKERS_MONO;
				break;
			default:
				audio.speakers = SPEAKERS_STEREO;
				break;
			}

			for (uint32_t c = 0; c < channels && c < MAX_AV_PLANES; c++)
				audio.data[c] = reinterpret_cast<const uint8_t *>(planar[c]);

			obs_source_output_audio(remoteVideoSource, &audio);
		});
}
