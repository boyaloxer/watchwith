#include "main-window.h"
#include "canvas-view.h"
#include "session-dialog.h"
#include "app.h"

#include <QAction>
#include <QCloseEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QToolBar>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
	setWindowTitle("WatchWith");
	setMinimumSize(960, 540);
	resize(1600, 900);

	QPalette pal = palette();
	pal.setColor(QPalette::Window, Qt::black);
	setPalette(pal);
	setAutoFillBackground(true);

	canvas = new CanvasView(this);
	setCentralWidget(canvas);

	buildToolbar();
	buildMenus();

	setStyleSheet(
		"QMainWindow { background: black; }"
		"QToolBar { background: rgba(30, 30, 30, 220); border: none; spacing: 4px; padding: 2px; }"
		"QToolBar QToolButton { color: white; background: rgba(60, 60, 60, 200); border: 1px solid #555; "
		"border-radius: 4px; padding: 6px 12px; font-size: 13px; }"
		"QToolBar QToolButton:hover { background: rgba(80, 80, 80, 220); border-color: #0af; }"
		"QMenuBar { background: rgba(30, 30, 30, 220); color: white; border: none; }"
		"QMenuBar::item:selected { background: rgba(80, 80, 80, 220); }"
		"QMenu { background: rgb(40, 40, 40); color: white; border: 1px solid #555; }"
		"QMenu::item:selected { background: rgb(60, 80, 120); }");
}

MainWindow::~MainWindow() {}

void MainWindow::buildToolbar()
{
	toolbar = addToolBar("Sources");
	toolbar->setMovable(false);
	toolbar->setFloatable(false);
	toolbar->setIconSize(QSize(20, 20));

	sessionAction = toolbar->addAction("Start / Join");
	sessionAction->setToolTip("Start or join a watch session");
	connect(sessionAction, &QAction::triggered, this, &MainWindow::onSessionClicked);

	toolbar->addSeparator();

	QAction *addWindow = toolbar->addAction("+ Window");
	connect(addWindow, &QAction::triggered, this, &MainWindow::onAddWindowCapture);

	QAction *addWebcam = toolbar->addAction("+ Webcam");
	connect(addWebcam, &QAction::triggered, this, &MainWindow::onAddWebcam);

	QAction *addAudio = toolbar->addAction("+ Audio Device");
	connect(addAudio, &QAction::triggered, this, &MainWindow::onAddAudioCapture);

	QAction *addAppAudio = toolbar->addAction("+ App Audio");
	connect(addAppAudio, &QAction::triggered, this, &MainWindow::onAddAppAudio);

	toolbar->addSeparator();

	QAction *removeSelected = toolbar->addAction("Remove");
	connect(removeSelected, &QAction::triggered, this, &MainWindow::onRemoveSelected);

	toolbar->addSeparator();

	QAction *toggleFs = toolbar->addAction("Fullscreen");
	toggleFs->setShortcut(QKeySequence(Qt::Key_F11));
	connect(toggleFs, &QAction::triggered, this, &MainWindow::onToggleFullscreen);
}

void MainWindow::buildMenus()
{
	QMenu *fileMenu = menuBar()->addMenu("&File");

	QAction *sessionMenuItem = fileMenu->addAction("&Session...");
	connect(sessionMenuItem, &QAction::triggered, this, &MainWindow::onSessionClicked);

	fileMenu->addSeparator();

	QAction *quit = fileMenu->addAction("&Quit");
	quit->setShortcut(QKeySequence::Quit);
	connect(quit, &QAction::triggered, this, &QWidget::close);

	QMenu *sourceMenu = menuBar()->addMenu("&Sources");

	QAction *addWindow = sourceMenu->addAction("Add Window Capture...");
	connect(addWindow, &QAction::triggered, this, &MainWindow::onAddWindowCapture);

	QAction *addWebcam = sourceMenu->addAction("Add Webcam...");
	connect(addWebcam, &QAction::triggered, this, &MainWindow::onAddWebcam);

	QAction *addAudio = sourceMenu->addAction("Add Audio Device...");
	connect(addAudio, &QAction::triggered, this, &MainWindow::onAddAudioCapture);

	QAction *addAppAudio = sourceMenu->addAction("Add App Audio...");
	connect(addAppAudio, &QAction::triggered, this, &MainWindow::onAddAppAudio);

	sourceMenu->addSeparator();

	QAction *removeSource = sourceMenu->addAction("Remove Selected Source");
	removeSource->setShortcut(QKeySequence::Delete);
	connect(removeSource, &QAction::triggered, this, &MainWindow::onRemoveSelected);

	QMenu *viewMenu = menuBar()->addMenu("&View");

	QAction *fullscreen = viewMenu->addAction("Toggle Fullscreen");
	fullscreen->setShortcut(QKeySequence(Qt::Key_F11));
	connect(fullscreen, &QAction::triggered, this, &MainWindow::onToggleFullscreen);

	QAction *toggleToolbar = viewMenu->addAction("Toggle Toolbar");
	toggleToolbar->setShortcut(QKeySequence(Qt::Key_F10));
	connect(toggleToolbar, &QAction::triggered, this, [this]() {
		toolbarVisible = !toolbarVisible;
		toolbar->setVisible(toolbarVisible);
	});
}

void MainWindow::onAddWindowCapture()
{
	auto windows = App()->getAvailableWindows();
	if (windows.empty()) {
		QMessageBox::information(this, "No Windows", "No capturable windows found.");
		return;
	}

	QStringList items;
	for (auto &w : windows)
		items.append(QString::fromStdString(w.name));

	bool ok = false;
	QString selected = QInputDialog::getItem(this, "Select Window", "Window to capture:", items, 0, false, &ok);

	if (!ok || selected.isEmpty())
		return;

	for (auto &w : windows) {
		if (w.name == selected.toStdString()) {
			App()->addWindowCapture(w.id);
			break;
		}
	}
}

void MainWindow::onAddWebcam()
{
	auto devices = App()->getAvailableWebcams();
	if (devices.empty()) {
		App()->addWebcam("");
		return;
	}

	QStringList items;
	for (auto &d : devices)
		items.append(QString::fromStdString(d.name));

	bool ok = false;
	QString selected = QInputDialog::getItem(this, "Select Webcam", "Webcam device:", items, 0, false, &ok);

	if (!ok || selected.isEmpty())
		return;

	for (auto &d : devices) {
		if (d.name == selected.toStdString()) {
			App()->addWebcam(d.id);
			break;
		}
	}
}

void MainWindow::onAddAudioCapture()
{
	auto devices = App()->getAvailableAudioDevices();
	if (devices.empty()) {
		App()->addAudioCapture("");
		return;
	}

	QStringList items;
	for (auto &d : devices)
		items.append(QString::fromStdString(d.name));

	bool ok = false;
	QString selected =
		QInputDialog::getItem(this, "Select Audio Device", "Audio output to capture:", items, 0, false, &ok);

	if (!ok || selected.isEmpty())
		return;

	for (auto &d : devices) {
		if (d.name == selected.toStdString()) {
			App()->addAudioCapture(d.id);
			break;
		}
	}
}

void MainWindow::onAddAppAudio()
{
	auto windows = App()->getAvailableAppAudioWindows();
	if (windows.empty()) {
		QMessageBox::information(this, "No Applications", "No applications with audio found.");
		return;
	}

	QStringList items;
	for (auto &w : windows)
		items.append(QString::fromStdString(w.name));

	bool ok = false;
	QString selected =
		QInputDialog::getItem(this, "Select Application", "Capture audio from:", items, 0, false, &ok);

	if (!ok || selected.isEmpty())
		return;

	for (auto &w : windows) {
		if (w.name == selected.toStdString()) {
			App()->addAppAudioCapture(w.id);
			break;
		}
	}
}

void MainWindow::onRemoveSelected()
{
	obs_sceneitem_t *item = canvas->getSelectedItem();
	if (!item)
		return;

	canvas->clearSelection();
	App()->removeSource(item);
}

void MainWindow::onSessionClicked()
{
	SessionDialog dialog(App()->getSession(), this);
	dialog.exec();

	Session *s = App()->getSession();
	if (s->state() == SessionState::Connected)
		sessionAction->setText("Connected");
	else
		sessionAction->setText("Start / Join");
}

void MainWindow::onToggleFullscreen()
{
	if (isFullScreen()) {
		showNormal();
		menuBar()->show();
		toolbar->setVisible(toolbarVisible);
	} else {
		menuBar()->hide();
		toolbar->hide();
		showFullScreen();
	}
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Escape && isFullScreen()) {
		onToggleFullscreen();
		return;
	}

	QMainWindow::keyPressEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	event->accept();
	QApplication::quit();
}
