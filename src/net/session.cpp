#include "session.h"
#include "peer-connection.h"

Session::Session(QObject *parent) : QObject(parent) {}

Session::~Session()
{
	disconnect();
}

void Session::ensurePeerConnection()
{
	if (peerConnection)
		peerConnection->close();

	peerConnection = std::make_unique<PeerConnection>(this);

	connect(peerConnection.get(), &PeerConnection::connected, this, [this]() {
		setState(SessionState::Connected);
		emit connected();
	});

	connect(peerConnection.get(), &PeerConnection::disconnected, this, [this]() {
		setState(SessionState::Disconnected);
		emit disconnected();
	});

	connect(peerConnection.get(), &PeerConnection::error, this, &Session::error);
}

QString Session::createOffer()
{
	currentRole = SessionRole::Host;
	ensurePeerConnection();

	QString offer = peerConnection->createOffer();
	if (offer.isEmpty()) {
		emit error("Failed to create connection offer");
		return QString();
	}

	setState(SessionState::WaitingForAnswer);
	return offer;
}

QString Session::acceptOffer(const QString &offerBlob)
{
	currentRole = SessionRole::Guest;
	ensurePeerConnection();

	QString answer = peerConnection->acceptOffer(offerBlob);
	if (answer.isEmpty())
		return QString();

	return answer;
}

bool Session::acceptAnswer(const QString &answerBlob)
{
	if (!peerConnection) {
		emit error("No active session");
		return false;
	}

	return peerConnection->acceptAnswer(answerBlob);
}

void Session::disconnect()
{
	if (peerConnection) {
		peerConnection->close();
		peerConnection.reset();
	}
	currentRole = SessionRole::None;
	setState(SessionState::Disconnected);
}

void Session::setState(SessionState newState)
{
	if (currentState != newState) {
		currentState = newState;
		emit stateChanged(newState);
	}
}
