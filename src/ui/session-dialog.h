#pragma once

#include "net/session.h"

#include <QDialog>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

class SessionDialog : public QDialog {
	Q_OBJECT

public:
	explicit SessionDialog(Session *session, QWidget *parent = nullptr);

private slots:
	void onStartSession();
	void onJoinSession();
	void onPasteOffer();
	void onPasteAnswer();
	void onDisconnect();
	void onStateChanged(SessionState state);
	void onError(const QString &message);

private:
	void buildChoicePage();
	void buildHostOfferPage();
	void buildHostWaitPage();
	void buildGuestPastePage();
	void buildGuestAnswerPage();
	void buildConnectedPage();
	void showPage(int index);

	Session *session;
	QStackedWidget *stack = nullptr;

	QPlainTextEdit *hostOfferText = nullptr;
	QPlainTextEdit *hostAnswerPaste = nullptr;
	QPlainTextEdit *guestOfferPaste = nullptr;
	QPlainTextEdit *guestAnswerText = nullptr;
	QLabel *connectedLabel = nullptr;
};
