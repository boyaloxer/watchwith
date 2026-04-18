#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>

#include <rtc/rtc.hpp>

#include <functional>
#include <memory>
#include <mutex>

class PeerConnection : public QObject {
	Q_OBJECT

public:
	explicit PeerConnection(QObject *parent = nullptr);
	~PeerConnection();

	struct ConnectionBlob {
		std::string sdp;
		std::vector<std::string> candidates;
	};

	QString createOffer();
	QString acceptOffer(const QString &blob);
	bool acceptAnswer(const QString &blob);
	void close();

	void sendVideoFrame(const uint8_t *data, size_t size, uint32_t timestamp);
	void sendAudioFrame(const uint8_t *data, size_t size, uint32_t timestamp);

signals:
	void connected();
	void disconnected();
	void videoFrameReceived(const QByteArray &data);
	void audioFrameReceived(const QByteArray &data);
	void error(const QString &message);

private:
	void setupPeerConnection();
	void setupDataChannel(std::shared_ptr<rtc::DataChannel> dc);

	static QString encodeBlob(const ConnectionBlob &blob);
	static ConnectionBlob decodeBlob(const QString &encoded);

	void onGatheringComplete();

	std::shared_ptr<rtc::PeerConnection> pc;
	std::shared_ptr<rtc::DataChannel> dataChannel;

	std::mutex mutex;
	ConnectionBlob localBlob;
	bool gatheringDone = false;
	std::function<void()> onGatheringDoneCallback;
};
