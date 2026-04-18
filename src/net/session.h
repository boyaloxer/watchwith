#pragma once

#include <QObject>
#include <QString>

#include <memory>

class PeerConnection;

enum class SessionState {
	Disconnected,
	WaitingForAnswer,
	WaitingForOffer,
	Connected,
};

enum class SessionRole {
	None,
	Host,
	Guest,
};

class Session : public QObject {
	Q_OBJECT

public:
	explicit Session(QObject *parent = nullptr);
	~Session();

	QString createOffer();
	QString acceptOffer(const QString &offerBlob);
	bool acceptAnswer(const QString &answerBlob);
	void disconnect();

	SessionState state() const { return currentState; }
	SessionRole role() const { return currentRole; }
	PeerConnection *peer() const { return peerConnection.get(); }

signals:
	void stateChanged(SessionState state);
	void connected();
	void disconnected();
	void error(const QString &message);

private:
	void setState(SessionState newState);
	void ensurePeerConnection();

	SessionState currentState = SessionState::Disconnected;
	SessionRole currentRole = SessionRole::None;
	std::unique_ptr<PeerConnection> peerConnection;
};
