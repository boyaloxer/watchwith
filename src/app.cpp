#include "app.h"
#include "net/media-bridge.h"
#include "net/session.h"
#include "ui/main-window.h"

#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef _MSC_VER
extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 1;
extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

static void uiTaskHandler(obs_task_t task, void *param, bool wait)
{
	auto doTask = [=]() { task(param); };

	if (wait) {
		QMetaObject::invokeMethod(App(), [doTask]() { doTask(); }, Qt::BlockingQueuedConnection);
	} else {
		QMetaObject::invokeMethod(App(), [doTask]() { doTask(); }, Qt::QueuedConnection);
	}
}

WatchWithApp::WatchWithApp(int &argc, char **argv) : QApplication(argc, argv)
{
	setApplicationName("WatchWith");
	setApplicationVersion("0.1.0");

#ifdef _WIN32
	QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
		Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
}

WatchWithApp::~WatchWithApp()
{
	shutdown();
}

const char *WatchWithApp::getRenderModule() const
{
#ifdef _WIN32
	return DL_D3D11;
#elif defined(__APPLE__) && defined(__aarch64__)
	return DL_OPENGL;
#else
	return DL_OPENGL;
#endif
}

bool WatchWithApp::initVideo()
{
	struct obs_video_info ovi = {};
	ovi.graphics_module = getRenderModule();
	ovi.fps_num = 30;
	ovi.fps_den = 1;
	ovi.base_width = 1920;
	ovi.base_height = 1080;
	ovi.output_width = 1920;
	ovi.output_height = 1080;
	ovi.output_format = VIDEO_FORMAT_NV12;
	ovi.colorspace = VIDEO_CS_709;
	ovi.range = VIDEO_RANGE_PARTIAL;
	ovi.adapter = 0;
	ovi.gpu_conversion = true;
	ovi.scale_type = OBS_SCALE_BICUBIC;

	int ret = obs_reset_video(&ovi);
	if (ret != OBS_VIDEO_SUCCESS) {
		blog(LOG_ERROR, "Failed to initialize video: %d", ret);
		return false;
	}

	return true;
}

bool WatchWithApp::initAudio()
{
	struct obs_audio_info2 ai = {};
	ai.samples_per_sec = 48000;
	ai.speakers = SPEAKERS_STEREO;
	ai.max_buffering_ms = 1000;
	ai.fixed_buffering = false;

	if (!obs_reset_audio2(&ai)) {
		blog(LOG_ERROR, "Failed to initialize audio");
		return false;
	}

	return true;
}

void WatchWithApp::loadModules()
{
	obs_load_all_modules();
	obs_log_loaded_modules();
	obs_post_load_modules();
}

bool WatchWithApp::init()
{
	char *exePathPtr = os_get_executable_path_ptr(nullptr);
	std::string basePath;
	if (exePathPtr) {
		basePath = exePathPtr;
		bfree(exePathPtr);

		for (auto &c : basePath)
			if (c == '\\')
				c = '/';

		size_t pos = basePath.rfind("/bin/");
		if (pos != std::string::npos)
			basePath = basePath.substr(0, pos + 1);
		else if (!basePath.empty() && basePath.back() != '/')
			basePath += '/';

		std::string dataPath = basePath + "data/libobs/";
		blog(LOG_INFO, "WatchWith base path: %s", basePath.c_str());
		blog(LOG_INFO, "WatchWith data path: %s", dataPath.c_str());

#pragma warning(push)
#pragma warning(disable : 4996)
		obs_add_data_path(dataPath.c_str());
#pragma warning(pop)
	}

	if (!obs_startup("en-US", nullptr, nullptr)) {
		blog(LOG_ERROR, "Failed to start libobs");
		return false;
	}

	libobsInitialized = true;
	obs_set_ui_task_handler(uiTaskHandler);

	if (!basePath.empty()) {
		std::string moduleBin = basePath + "obs-plugins/64bit/";
		std::string moduleData = basePath + "data/obs-plugins/%module%/";
		blog(LOG_INFO, "WatchWith module bin: %s", moduleBin.c_str());
		blog(LOG_INFO, "WatchWith module data: %s", moduleData.c_str());
		obs_add_module_path(moduleBin.c_str(), moduleData.c_str());
	}

	if (!initAudio())
		return false;

	if (!initVideo())
		return false;

	register_remote_source();
	loadModules();

	OBSSourceAutoRelease sceneSource = obs_source_create("scene", "WatchWith Scene", nullptr, nullptr);
	if (!sceneSource)
		return false;

	scene = obs_scene_from_source(sceneSource);
	obs_set_output_source(0, sceneSource);

	session = new Session(this);
	mediaBridge = new MediaBridge(this);

	connect(session, &Session::connected, this, [this]() {
		if (session->peer())
			mediaBridge->start(session->peer());
	});
	connect(session, &Session::disconnected, this, [this]() {
		mediaBridge->stop();
	});

	mainWindow = new MainWindow();
	mainWindow->show();

	return true;
}

void WatchWithApp::shutdown()
{
	if (mainWindow) {
		delete mainWindow;
		mainWindow = nullptr;
	}

	if (mediaBridge) {
		mediaBridge->stop();
		delete mediaBridge;
		mediaBridge = nullptr;
	}

	if (session) {
		session->disconnect();
		delete session;
		session = nullptr;
	}

	scene = nullptr;

	if (libobsInitialized) {
		obs_set_output_source(0, nullptr);
		QApplication::sendPostedEvents(nullptr);
		obs_shutdown();
		libobsInitialized = false;
	}
}

void WatchWithApp::addWindowCapture(const std::string &windowName)
{
	if (!scene)
		return;

#ifdef _WIN32
	const char *captureType = "window_capture";
#elif defined(__APPLE__)
	const char *captureType = "window_capture";
#else
	const char *captureType = "xcomposite_input";
#endif

	OBSDataAutoRelease settings = obs_data_create();
	if (!windowName.empty())
		obs_data_set_string(settings, "window", windowName.c_str());

	OBSSourceAutoRelease source = obs_source_create(captureType, "Shared Video", settings, nullptr);
	if (!source)
		return;

	obs_sceneitem_t *item = obs_scene_add(scene, source);
	if (item) {
		vec2 pos = {0, 0};
		obs_sceneitem_set_pos(item, &pos);
	}
}

void WatchWithApp::addWebcam(const std::string &deviceId)
{
	if (!scene)
		return;

#ifdef _WIN32
	const char *captureType = "dshow_input";
#elif defined(__APPLE__)
	const char *captureType = "av_capture_input";
#else
	const char *captureType = "v4l2_input";
#endif

	OBSDataAutoRelease settings = obs_data_create();
	if (!deviceId.empty())
		obs_data_set_string(settings, "video_device_id", deviceId.c_str());

	static int webcamCount = 0;
	std::string name = "Webcam " + std::to_string(++webcamCount);

	OBSSourceAutoRelease source = obs_source_create(captureType, name.c_str(), settings, nullptr);
	if (!source)
		return;

	obs_sceneitem_t *item = obs_scene_add(scene, source);
	if (item) {
		vec2 pos = {1520, 760};
		vec2 scale = {0.25f, 0.25f};
		obs_sceneitem_set_pos(item, &pos);
		obs_sceneitem_set_scale(item, &scale);
	}
}

void WatchWithApp::addAudioCapture(const std::string &deviceId)
{
	if (!scene)
		return;

#ifdef _WIN32
	const char *captureType = "wasapi_output_capture";
#elif defined(__APPLE__)
	const char *captureType = "coreaudio_output_capture";
#else
	const char *captureType = "pulse_output_capture";
#endif

	OBSDataAutoRelease settings = obs_data_create();
	if (!deviceId.empty())
		obs_data_set_string(settings, "device_id", deviceId.c_str());

	OBSSourceAutoRelease source = obs_source_create(captureType, "Desktop Audio", settings, nullptr);
	if (!source)
		return;

	obs_scene_add(scene, source);
}

void WatchWithApp::addAppAudioCapture(const std::string &window)
{
	if (!scene)
		return;

#ifdef _WIN32
	const char *captureType = "wasapi_process_output_capture";
#else
	return;
#endif

	OBSDataAutoRelease settings = obs_data_create();
	if (!window.empty())
		obs_data_set_string(settings, "window", window.c_str());

	static int appAudioCount = 0;
	std::string name = "App Audio " + std::to_string(++appAudioCount);

	OBSSourceAutoRelease source = obs_source_create(captureType, name.c_str(), settings, nullptr);
	if (!source)
		return;

	obs_scene_add(scene, source);
}

void WatchWithApp::removeSource(obs_sceneitem_t *item)
{
	if (item)
		obs_sceneitem_remove(item);
}

std::vector<SourceInfo> WatchWithApp::getAvailableWindows()
{
	std::vector<SourceInfo> result;

#ifdef _WIN32
	const char *captureType = "window_capture";
#elif defined(__APPLE__)
	const char *captureType = "window_capture";
#else
	const char *captureType = "xcomposite_input";
#endif

	obs_properties_t *props = obs_get_source_properties(captureType);
	if (!props)
		return result;

	obs_property_t *prop = obs_properties_get(props, "window");
	if (prop && obs_property_get_type(prop) == OBS_PROPERTY_LIST) {
		size_t count = obs_property_list_item_count(prop);
		for (size_t i = 0; i < count; i++) {
			const char *name = obs_property_list_item_name(prop, i);
			const char *val = obs_property_list_item_string(prop, i);
			if (name && val)
				result.push_back({val, name});
		}
	}

	obs_properties_destroy(props);
	return result;
}

std::vector<SourceInfo> WatchWithApp::getAvailableWebcams()
{
	std::vector<SourceInfo> result;

#ifdef _WIN32
	const char *captureType = "dshow_input";
	const char *propName = "video_device_id";
#elif defined(__APPLE__)
	const char *captureType = "av_capture_input";
	const char *propName = "device";
#else
	const char *captureType = "v4l2_input";
	const char *propName = "device_id";
#endif

	obs_properties_t *props = obs_get_source_properties(captureType);
	if (!props)
		return result;

	obs_property_t *prop = obs_properties_get(props, propName);
	if (prop && obs_property_get_type(prop) == OBS_PROPERTY_LIST) {
		size_t count = obs_property_list_item_count(prop);
		for (size_t i = 0; i < count; i++) {
			const char *name = obs_property_list_item_name(prop, i);
			const char *val = obs_property_list_item_string(prop, i);
			if (name && val)
				result.push_back({val, name});
		}
	}

	obs_properties_destroy(props);
	return result;
}

std::vector<SourceInfo> WatchWithApp::getAvailableAudioDevices()
{
	std::vector<SourceInfo> result;

#ifdef _WIN32
	const char *captureType = "wasapi_output_capture";
#elif defined(__APPLE__)
	const char *captureType = "coreaudio_output_capture";
#else
	const char *captureType = "pulse_output_capture";
#endif

	obs_properties_t *props = obs_get_source_properties(captureType);
	if (!props)
		return result;

	obs_property_t *prop = obs_properties_get(props, "device_id");
	if (prop && obs_property_get_type(prop) == OBS_PROPERTY_LIST) {
		size_t count = obs_property_list_item_count(prop);
		for (size_t i = 0; i < count; i++) {
			const char *name = obs_property_list_item_name(prop, i);
			const char *val = obs_property_list_item_string(prop, i);
			if (name && val)
				result.push_back({val, name});
		}
	}

	obs_properties_destroy(props);
	return result;
}

std::vector<SourceInfo> WatchWithApp::getAvailableAppAudioWindows()
{
	std::vector<SourceInfo> result;

#ifdef _WIN32
	const char *captureType = "wasapi_process_output_capture";
#else
	return result;
#endif

	obs_properties_t *props = obs_get_source_properties(captureType);
	if (!props)
		return result;

	obs_property_t *prop = obs_properties_get(props, "window");
	if (prop && obs_property_get_type(prop) == OBS_PROPERTY_LIST) {
		size_t count = obs_property_list_item_count(prop);
		for (size_t i = 0; i < count; i++) {
			const char *name = obs_property_list_item_name(prop, i);
			const char *val = obs_property_list_item_string(prop, i);
			if (name && val)
				result.push_back({val, name});
		}
	}

	obs_properties_destroy(props);
	return result;
}
