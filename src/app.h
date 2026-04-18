#pragma once

#include <obs.hpp>

#include <QApplication>
#include <QPointer>
#include <functional>
#include <string>
#include <vector>

class MainWindow;
class MediaBridge;
class Session;

struct SourceInfo {
	std::string id;
	std::string name;
};

class WatchWithApp : public QApplication {
	Q_OBJECT

public:
	WatchWithApp(int &argc, char **argv);
	~WatchWithApp();

	bool init();
	void shutdown();

	obs_scene_t *getScene() const { return scene; }
	Session *getSession() const { return session; }
	MediaBridge *getMediaBridge() const { return mediaBridge; }

	void addWindowCapture(const std::string &windowName);
	void addWebcam(const std::string &deviceId = "");
	void addAudioCapture(const std::string &deviceId = "");
	void addAppAudioCapture(const std::string &window);
	void removeSource(obs_sceneitem_t *item);

	std::vector<SourceInfo> getAvailableWindows();
	std::vector<SourceInfo> getAvailableWebcams();
	std::vector<SourceInfo> getAvailableAudioDevices();
	std::vector<SourceInfo> getAvailableAppAudioWindows();

	const char *getRenderModule() const;

private:
	bool initVideo();
	bool initAudio();
	void loadModules();

	bool libobsInitialized = false;
	OBSScene scene;
	Session *session = nullptr;
	MediaBridge *mediaBridge = nullptr;
	QPointer<MainWindow> mainWindow;
};

inline WatchWithApp *App()
{
	return static_cast<WatchWithApp *>(qApp);
}
