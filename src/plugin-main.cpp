#include "shorts-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>

#include <QMainWindow>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-shorts-vertical", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Vertical canvas for YouTube Shorts, TikTok, and Reels with move/resize camera controls";
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Vertical Shorts Plugin";
}

static ShortsDock *g_dock = nullptr;

static void SaveCallback(obs_data_t *save_data, bool saving, void *)
{
	if (!g_dock)
		return;

	if (saving) {
		OBSDataAutoRelease obj = obs_data_create();
		g_dock->SaveSettings(obj);
		obs_data_set_obj(save_data, "obs-shorts-vertical", obj);
	} else {
		obs_data_t *obj = obs_data_get_obj(save_data, "obs-shorts-vertical");
		if (obj) {
			g_dock->LoadSettings(obj);
			obs_data_release(obj);
		}
	}
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[obs-shorts-vertical] Loading Vertical Shorts Plugin");

	QMainWindow *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	g_dock = new ShortsDock(main);

	obs_frontend_add_dock_by_id("ShortsVerticalDock", obs_module_text("ShortsDock"), g_dock);
	obs_frontend_add_save_callback(SaveCallback, nullptr);

	blog(LOG_INFO, "[obs-shorts-vertical] Plugin loaded — open View → Docks → Shorts");
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_save_callback(SaveCallback, nullptr);
	/* Dock widget is owned/destroyed by OBS frontend */
	g_dock = nullptr;
	blog(LOG_INFO, "[obs-shorts-vertical] Plugin unloaded");
}
