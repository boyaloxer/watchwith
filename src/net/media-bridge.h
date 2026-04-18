#pragma once

#include <obs.h>
#include <obs.hpp>

#include <QObject>

#include <atomic>
#include <memory>
#include <mutex>

class PeerConnection;
class VideoEncoder;
class VideoDecoder;
class AudioEncoder;
class AudioDecoder;

void register_remote_source();

class MediaBridge : public QObject {
	Q_OBJECT

public:
	explicit MediaBridge(QObject *parent = nullptr);
	~MediaBridge();

	void start(PeerConnection *peer);
	void stop();

	bool isRunning() const { return running; }

	obs_source_t *getRemoteVideoSource() const { return remoteVideoSource; }

private:
	static void rawVideoCallback(void *param, struct video_data *frame);
	static void rawAudioCallback(void *param, size_t mix_idx, struct audio_data *data);

	void onRemoteVideoFrame(const QByteArray &data);
	void onRemoteAudioFrame(const QByteArray &data);

	void addRemoteSourceToScene();

	PeerConnection *peerConnection = nullptr;
	std::atomic<bool> running{false};

	OBSSourceAutoRelease remoteVideoSource;

	std::unique_ptr<VideoEncoder> videoEncoder;
	std::unique_ptr<VideoDecoder> videoDecoder;
	std::unique_ptr<AudioEncoder> audioEncoder;
	std::unique_ptr<AudioDecoder> audioDecoder;
};
