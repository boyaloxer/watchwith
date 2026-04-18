#include "peer-connection.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <obs.h>

PeerConnection::PeerConnection(QObject *parent) : QObject(parent) {}

PeerConnection::~PeerConnection()
{
	close();
}

void PeerConnection::setupPeerConnection()
{
	rtc::Configuration config;
	config.iceServers.emplace_back("stun:stun.l.google.com:19302");
	config.iceServers.emplace_back("stun:stun1.l.google.com:19302");

	pc = std::make_shared<rtc::PeerConnection>(config);

	pc->onStateChange([this](rtc::PeerConnection::State state) {
		if (state == rtc::PeerConnection::State::Connected)
			QMetaObject::invokeMethod(this, [this]() { emit connected(); }, Qt::QueuedConnection);
		else if (state == rtc::PeerConnection::State::Disconnected ||
			 state == rtc::PeerConnection::State::Failed ||
			 state == rtc::PeerConnection::State::Closed)
			QMetaObject::invokeMethod(this, [this]() { emit disconnected(); }, Qt::QueuedConnection);
	});

	pc->onLocalCandidate([this](rtc::Candidate candidate) {
		std::lock_guard<std::mutex> lock(mutex);
		localBlob.candidates.push_back(std::string(candidate));
	});

	pc->onGatheringStateChange([this](rtc::PeerConnection::GatheringState state) {
		if (state == rtc::PeerConnection::GatheringState::Complete)
			onGatheringComplete();
	});

	pc->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
		setupDataChannel(dc);
	});
}

void PeerConnection::setupDataChannel(std::shared_ptr<rtc::DataChannel> dc)
{
	dataChannel = dc;

	dc->onOpen([this]() {
		blog(LOG_INFO, "WebRTC data channel opened");
	});

	dc->onClosed([this]() {
		blog(LOG_INFO, "WebRTC data channel closed");
	});

	dc->onMessage([this](rtc::binary bin) {
		if (bin.size() < 2)
			return;

		uint8_t type = static_cast<uint8_t>(bin[0]);
		const char *payload = reinterpret_cast<const char *>(bin.data()) + 1;
		size_t payloadSize = bin.size() - 1;

		QByteArray data(payload, (int)payloadSize);

		if (type == 0x01)
			QMetaObject::invokeMethod(this, [this, data]() { emit videoFrameReceived(data); },
						  Qt::QueuedConnection);
		else if (type == 0x02)
			QMetaObject::invokeMethod(this, [this, data]() { emit audioFrameReceived(data); },
						  Qt::QueuedConnection);
	}, nullptr);
}

void PeerConnection::onGatheringComplete()
{
	std::lock_guard<std::mutex> lock(mutex);
	gatheringDone = true;
	if (onGatheringDoneCallback)
		onGatheringDoneCallback();
}

QString PeerConnection::encodeBlob(const ConnectionBlob &blob)
{
	QJsonObject obj;
	obj["sdp"] = QString::fromStdString(blob.sdp);

	QJsonArray candidates;
	for (auto &c : blob.candidates)
		candidates.append(QString::fromStdString(c));
	obj["ice"] = candidates;

	QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact);
	QByteArray compressed = qCompress(json, 9);
	return QString::fromLatin1(compressed.toBase64());
}

PeerConnection::ConnectionBlob PeerConnection::decodeBlob(const QString &encoded)
{
	ConnectionBlob blob;
	QByteArray compressed = QByteArray::fromBase64(encoded.trimmed().toLatin1());
	QByteArray json = qUncompress(compressed);
	if (json.isEmpty())
		return blob;

	QJsonDocument doc = QJsonDocument::fromJson(json);
	if (!doc.isObject())
		return blob;

	QJsonObject obj = doc.object();
	blob.sdp = obj["sdp"].toString().toStdString();

	QJsonArray candidates = obj["ice"].toArray();
	for (auto c : candidates)
		blob.candidates.push_back(c.toString().toStdString());

	return blob;
}

QString PeerConnection::createOffer()
{
	setupPeerConnection();

	dataChannel = pc->createDataChannel("watchwith");
	setupDataChannel(dataChannel);

	pc->setLocalDescription(rtc::Description::Type::Offer);

	auto localDesc = pc->localDescription();
	if (!localDesc) {
		emit error("Failed to create local description");
		return QString();
	}

	{
		std::lock_guard<std::mutex> lock(mutex);
		localBlob.sdp = std::string(*localDesc);
	}

	/* Wait for ICE gathering to complete (with timeout) */
	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot(true);
	timeout.start(10000);

	connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

	{
		std::lock_guard<std::mutex> lock(mutex);
		if (gatheringDone) {
			return encodeBlob(localBlob);
		}
		onGatheringDoneCallback = [&loop]() {
			QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
		};
	}

	loop.exec();

	std::lock_guard<std::mutex> lock(mutex);
	onGatheringDoneCallback = nullptr;
	localBlob.sdp = std::string(*pc->localDescription());
	return encodeBlob(localBlob);
}

QString PeerConnection::acceptOffer(const QString &blob)
{
	ConnectionBlob remoteBlob = decodeBlob(blob);
	if (remoteBlob.sdp.empty()) {
		emit error("Invalid connection code");
		return QString();
	}

	setupPeerConnection();

	rtc::Description remoteDesc(remoteBlob.sdp, rtc::Description::Type::Offer);
	pc->setRemoteDescription(remoteDesc);

	for (auto &c : remoteBlob.candidates)
		pc->addRemoteCandidate(rtc::Candidate(c));

	pc->setLocalDescription(rtc::Description::Type::Answer);

	auto localDesc = pc->localDescription();
	if (!localDesc) {
		emit error("Failed to create answer");
		return QString();
	}

	{
		std::lock_guard<std::mutex> lock(mutex);
		localBlob.sdp = std::string(*localDesc);
	}

	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot(true);
	timeout.start(10000);

	connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

	{
		std::lock_guard<std::mutex> lock(mutex);
		if (gatheringDone) {
			localBlob.sdp = std::string(*pc->localDescription());
			return encodeBlob(localBlob);
		}
		onGatheringDoneCallback = [&loop]() {
			QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
		};
	}

	loop.exec();

	std::lock_guard<std::mutex> lock(mutex);
	onGatheringDoneCallback = nullptr;
	localBlob.sdp = std::string(*pc->localDescription());
	return encodeBlob(localBlob);
}

bool PeerConnection::acceptAnswer(const QString &blob)
{
	ConnectionBlob remoteBlob = decodeBlob(blob);
	if (remoteBlob.sdp.empty()) {
		emit error("Invalid response code");
		return false;
	}

	rtc::Description remoteDesc(remoteBlob.sdp, rtc::Description::Type::Answer);
	pc->setRemoteDescription(remoteDesc);

	for (auto &c : remoteBlob.candidates)
		pc->addRemoteCandidate(rtc::Candidate(c));

	return true;
}

void PeerConnection::sendVideoFrame(const uint8_t *data, size_t size, uint32_t)
{
	if (!dataChannel || !dataChannel->isOpen())
		return;

	rtc::binary packet(size + 1);
	packet[0] = std::byte{0x01};
	std::memcpy(&packet[1], data, size);

	try {
		dataChannel->send(packet);
	} catch (...) {
	}
}

void PeerConnection::sendAudioFrame(const uint8_t *data, size_t size, uint32_t)
{
	if (!dataChannel || !dataChannel->isOpen())
		return;

	rtc::binary packet(size + 1);
	packet[0] = std::byte{0x02};
	std::memcpy(&packet[1], data, size);

	try {
		dataChannel->send(packet);
	} catch (...) {
	}
}

void PeerConnection::close()
{
	if (dataChannel) {
		dataChannel->close();
		dataChannel.reset();
	}
	if (pc) {
		pc->close();
		pc.reset();
	}
	gatheringDone = false;
	localBlob = {};
}
