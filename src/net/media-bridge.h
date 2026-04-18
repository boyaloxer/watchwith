#pragma once

#include <obs.h>
#include <obs.hpp>

#include <QObject>

#include <atomic>
#include <mutex>
#include <vector>

class PeerConnection;

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

	std::mutex encodeMutex;
};
