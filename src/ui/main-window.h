#pragma once

#include <QMainWindow>

class CanvasView;
class QAction;
class QToolBar;
class QMenu;

class MainWindow : public QMainWindow {
	Q_OBJECT

public:
	MainWindow(QWidget *parent = nullptr);
	~MainWindow();

	CanvasView *getCanvas() const { return canvas; }

protected:
	void keyPressEvent(QKeyEvent *event) override;
	void closeEvent(QCloseEvent *event) override;

private slots:
	void onAddWindowCapture();
	void onAddWebcam();
	void onAddAudioCapture();
	void onAddAppAudio();
	void onRemoveSelected();
	void onToggleFullscreen();
	void onSessionClicked();

private:
	void buildToolbar();
	void buildMenus();

	CanvasView *canvas = nullptr;
	QToolBar *toolbar = nullptr;
	QAction *sessionAction = nullptr;
	bool toolbarVisible = true;
};
