#include "session-dialog.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

static const QString dialogStyle = QStringLiteral(
	"QDialog { background: rgb(30, 30, 30); }"
	"QLabel { color: white; font-size: 14px; }"
	"QPlainTextEdit { background: rgb(45, 45, 45); color: #0af; border: 1px solid #555; "
	"border-radius: 6px; padding: 8px; font-size: 11px; font-family: monospace; }"
	"QPushButton { background: rgb(60, 60, 60); color: white; border: 1px solid #555; "
	"border-radius: 6px; padding: 10px 24px; font-size: 14px; min-width: 100px; }"
	"QPushButton:hover { background: rgb(80, 80, 80); border-color: #0af; }"
	"QPushButton#primary { background: rgb(0, 120, 215); border-color: rgb(0, 120, 215); }"
	"QPushButton#primary:hover { background: rgb(30, 145, 235); }");

SessionDialog::SessionDialog(Session *session, QWidget *parent) : QDialog(parent), session(session)
{
	setWindowTitle("Session");
	setMinimumSize(480, 360);
	setStyleSheet(dialogStyle);

	stack = new QStackedWidget(this);

	buildChoicePage();
	buildHostOfferPage();
	buildHostWaitPage();
	buildGuestPastePage();
	buildGuestAnswerPage();
	buildConnectedPage();

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(stack);

	connect(session, &Session::stateChanged, this, &SessionDialog::onStateChanged);
	connect(session, &Session::error, this, &SessionDialog::onError);

	if (session->state() == SessionState::Connected)
		showPage(5);
	else
		showPage(0);
}

/* Page 0: Choice */
void SessionDialog::buildChoicePage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setAlignment(Qt::AlignCenter);
	layout->setSpacing(20);

	auto *title = new QLabel("Watch Together");
	title->setStyleSheet("font-size: 22px; font-weight: bold;");
	title->setAlignment(Qt::AlignCenter);
	layout->addWidget(title);

	auto *subtitle = new QLabel("Start a session and share the code with your partner,\nor join by pasting a code they sent you.");
	subtitle->setAlignment(Qt::AlignCenter);
	subtitle->setWordWrap(true);
	subtitle->setStyleSheet("color: #aaa;");
	layout->addWidget(subtitle);

	layout->addSpacing(10);

	auto *startBtn = new QPushButton("Start Session");
	startBtn->setObjectName("primary");
	startBtn->setMinimumHeight(44);
	connect(startBtn, &QPushButton::clicked, this, &SessionDialog::onStartSession);
	layout->addWidget(startBtn);

	auto *joinBtn = new QPushButton("Join Session");
	joinBtn->setMinimumHeight(44);
	connect(joinBtn, &QPushButton::clicked, this, [this]() { showPage(3); });
	layout->addWidget(joinBtn);

	stack->addWidget(page);
}

/* Page 1: Host — shows offer code to copy */
void SessionDialog::buildHostOfferPage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setSpacing(12);

	auto *step = new QLabel("Step 1 of 2 — Send this code to your partner");
	step->setStyleSheet("font-size: 16px; font-weight: bold;");
	step->setAlignment(Qt::AlignCenter);
	layout->addWidget(step);

	auto *hint = new QLabel("Copy the code below and send it to them (text, email, etc.):");
	hint->setWordWrap(true);
	hint->setAlignment(Qt::AlignCenter);
	hint->setStyleSheet("color: #aaa;");
	layout->addWidget(hint);

	hostOfferText = new QPlainTextEdit;
	hostOfferText->setReadOnly(true);
	hostOfferText->setMaximumHeight(100);
	layout->addWidget(hostOfferText);

	auto *btnRow = new QHBoxLayout;
	auto *copyBtn = new QPushButton("Copy Code");
	copyBtn->setObjectName("primary");
	connect(copyBtn, &QPushButton::clicked, this, [this, copyBtn]() {
		QApplication::clipboard()->setText(hostOfferText->toPlainText());
		copyBtn->setText("Copied!");
	});
	btnRow->addWidget(copyBtn);

	auto *nextBtn = new QPushButton("Next");
	connect(nextBtn, &QPushButton::clicked, this, [this]() { showPage(2); });
	btnRow->addWidget(nextBtn);

	auto *cancelBtn = new QPushButton("Cancel");
	connect(cancelBtn, &QPushButton::clicked, this, [this]() {
		session->disconnect();
		showPage(0);
	});
	btnRow->addWidget(cancelBtn);

	layout->addLayout(btnRow);
	stack->addWidget(page);
}

/* Page 2: Host — paste the answer from guest */
void SessionDialog::buildHostWaitPage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setSpacing(12);

	auto *step = new QLabel("Step 2 of 2 — Paste your partner's response");
	step->setStyleSheet("font-size: 16px; font-weight: bold;");
	step->setAlignment(Qt::AlignCenter);
	layout->addWidget(step);

	auto *hint = new QLabel("Your partner will send back a response code. Paste it below:");
	hint->setWordWrap(true);
	hint->setAlignment(Qt::AlignCenter);
	hint->setStyleSheet("color: #aaa;");
	layout->addWidget(hint);

	hostAnswerPaste = new QPlainTextEdit;
	hostAnswerPaste->setPlaceholderText("Paste response code here...");
	hostAnswerPaste->setMaximumHeight(100);
	layout->addWidget(hostAnswerPaste);

	auto *btnRow = new QHBoxLayout;

	auto *pasteBtn = new QPushButton("Paste from Clipboard");
	connect(pasteBtn, &QPushButton::clicked, this, [this]() {
		hostAnswerPaste->setPlainText(QApplication::clipboard()->text());
	});
	btnRow->addWidget(pasteBtn);

	auto *connectBtn = new QPushButton("Connect");
	connectBtn->setObjectName("primary");
	connect(connectBtn, &QPushButton::clicked, this, &SessionDialog::onPasteAnswer);
	btnRow->addWidget(connectBtn);

	layout->addLayout(btnRow);
	stack->addWidget(page);
}

/* Page 3: Guest — paste the offer from host */
void SessionDialog::buildGuestPastePage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setSpacing(12);

	auto *step = new QLabel("Step 1 of 2 — Paste the code from your partner");
	step->setStyleSheet("font-size: 16px; font-weight: bold;");
	step->setAlignment(Qt::AlignCenter);
	layout->addWidget(step);

	auto *hint = new QLabel("They should have sent you a connection code. Paste it below:");
	hint->setWordWrap(true);
	hint->setAlignment(Qt::AlignCenter);
	hint->setStyleSheet("color: #aaa;");
	layout->addWidget(hint);

	guestOfferPaste = new QPlainTextEdit;
	guestOfferPaste->setPlaceholderText("Paste connection code here...");
	guestOfferPaste->setMaximumHeight(100);
	layout->addWidget(guestOfferPaste);

	auto *btnRow = new QHBoxLayout;

	auto *pasteBtn = new QPushButton("Paste from Clipboard");
	connect(pasteBtn, &QPushButton::clicked, this, [this]() {
		guestOfferPaste->setPlainText(QApplication::clipboard()->text());
	});
	btnRow->addWidget(pasteBtn);

	auto *acceptBtn = new QPushButton("Accept");
	acceptBtn->setObjectName("primary");
	connect(acceptBtn, &QPushButton::clicked, this, &SessionDialog::onPasteOffer);
	btnRow->addWidget(acceptBtn);

	auto *backBtn = new QPushButton("Back");
	connect(backBtn, &QPushButton::clicked, this, [this]() { showPage(0); });
	btnRow->addWidget(backBtn);

	layout->addLayout(btnRow);
	stack->addWidget(page);
}

/* Page 4: Guest — shows answer code to copy back */
void SessionDialog::buildGuestAnswerPage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setSpacing(12);

	auto *step = new QLabel("Step 2 of 2 — Send this response back");
	step->setStyleSheet("font-size: 16px; font-weight: bold;");
	step->setAlignment(Qt::AlignCenter);
	layout->addWidget(step);

	auto *hint = new QLabel("Copy the response below and send it back to your partner:");
	hint->setWordWrap(true);
	hint->setAlignment(Qt::AlignCenter);
	hint->setStyleSheet("color: #aaa;");
	layout->addWidget(hint);

	guestAnswerText = new QPlainTextEdit;
	guestAnswerText->setReadOnly(true);
	guestAnswerText->setMaximumHeight(100);
	layout->addWidget(guestAnswerText);

	auto *btnRow = new QHBoxLayout;
	auto *copyBtn = new QPushButton("Copy Response");
	copyBtn->setObjectName("primary");
	connect(copyBtn, &QPushButton::clicked, this, [this, copyBtn]() {
		QApplication::clipboard()->setText(guestAnswerText->toPlainText());
		copyBtn->setText("Copied!");
	});
	btnRow->addWidget(copyBtn);

	auto *doneBtn = new QPushButton("Done");
	connect(doneBtn, &QPushButton::clicked, this, &QDialog::accept);
	btnRow->addWidget(doneBtn);

	layout->addLayout(btnRow);
	stack->addWidget(page);
}

/* Page 5: Connected */
void SessionDialog::buildConnectedPage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setAlignment(Qt::AlignCenter);
	layout->setSpacing(16);

	auto *icon = new QLabel("Connected!");
	icon->setStyleSheet("font-size: 24px; font-weight: bold; color: #4f4;");
	icon->setAlignment(Qt::AlignCenter);
	layout->addWidget(icon);

	connectedLabel = new QLabel("You are connected to your partner.");
	connectedLabel->setAlignment(Qt::AlignCenter);
	connectedLabel->setStyleSheet("color: #aaa;");
	layout->addWidget(connectedLabel);

	auto *disconnectBtn = new QPushButton("Disconnect");
	connect(disconnectBtn, &QPushButton::clicked, this, &SessionDialog::onDisconnect);
	layout->addWidget(disconnectBtn);

	auto *closeBtn = new QPushButton("Close");
	closeBtn->setObjectName("primary");
	connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
	layout->addWidget(closeBtn);

	stack->addWidget(page);
}

void SessionDialog::showPage(int index)
{
	stack->setCurrentIndex(index);
}

void SessionDialog::onStartSession()
{
	QString offer = session->createOffer();
	hostOfferText->setPlainText(offer);
	showPage(1);
}

void SessionDialog::onJoinSession()
{
	showPage(3);
}

void SessionDialog::onPasteOffer()
{
	QString offerBlob = guestOfferPaste->toPlainText().trimmed();
	if (offerBlob.isEmpty()) {
		QMessageBox::warning(this, "Empty", "Please paste a connection code first.");
		return;
	}

	QString answer = session->acceptOffer(offerBlob);
	if (answer.isEmpty())
		return;

	guestAnswerText->setPlainText(answer);
	showPage(4);
}

void SessionDialog::onPasteAnswer()
{
	QString answerBlob = hostAnswerPaste->toPlainText().trimmed();
	if (answerBlob.isEmpty()) {
		QMessageBox::warning(this, "Empty", "Please paste the response code first.");
		return;
	}

	if (session->acceptAnswer(answerBlob))
		showPage(5);
}

void SessionDialog::onDisconnect()
{
	session->disconnect();
	showPage(0);
}

void SessionDialog::onStateChanged(SessionState state)
{
	if (state == SessionState::Connected)
		showPage(5);
	else if (state == SessionState::Disconnected && stack->currentIndex() == 5)
		showPage(0);
}

void SessionDialog::onError(const QString &message)
{
	QMessageBox::warning(this, "Error", message);
}
