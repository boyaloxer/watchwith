#include "app.h"

#include <obs.h>
#include <util/platform.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
	SetErrorMode(SEM_FAILCRITICALERRORS);
	SetProcessDPIAware();
#endif

	base_get_log_handler(nullptr, nullptr);
	obs_set_cmdline_args(argc, argv);

	WatchWithApp app(argc, argv);

	if (!app.init()) {
		blog(LOG_ERROR, "Failed to initialize WatchWith");
		return 1;
	}

	int ret = app.exec();
	app.shutdown();

	blog(LOG_INFO, "Memory leaks: %ld", bnum_allocs());

	return ret;
}
