#include "shorts-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-shorts-vertical", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Vertical Shorts production dock with vertical canvas, streaming, recording, short/long clips, and automation";
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Vertical Shorts Plugin";
}

static ShortsDock *g_dock = nullptr;
static const char *DOCK_ID = "vertical_shorts_plugin_dock";

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

static void ShowDock()
{
	QMainWindow *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main)
		return;

	QDockWidget *dock = main->findChild<QDockWidget *>(DOCK_ID);
	if (!dock)
		return;

	dock->setFloating(false);
	main->addDockWidget(Qt::RightDockWidgetArea, dock);
	dock->setVisible(true);
	dock->raise();
	dock->toggleViewAction()->setChecked(true);
}

static void RegisterDock()
{
	if (g_dock)
		return;

	QMainWindow *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main) {
		blog(LOG_ERROR, "[obs-shorts-vertical] Main window not available; dock not registered");
		return;
	}

	g_dock = new ShortsDock(main);

	const char *title = obs_module_text("ShortsDock");
	if (!title || !*title)
		title = "Vertical Shorts";

	const bool ok = obs_frontend_add_dock_by_id(DOCK_ID, title, g_dock);
	if (!ok) {
		blog(LOG_ERROR, "[obs-shorts-vertical] Failed to add dock id '%s' (already used?)", DOCK_ID);
		return;
	}

	obs_frontend_add_save_callback(SaveCallback, nullptr);

	/* OBS adds docks hidden by default — show it so users can find it. */
	ShowDock();

	blog(LOG_INFO, "[obs-shorts-vertical] Dock registered and shown (%s)", title);
}

static void FrontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		RegisterDock();
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP ||
		   event == OBS_FRONTEND_EVENT_EXIT) {
		/* Keep dock alive until unload; save callback handles persistence. */
	}
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[obs-shorts-vertical] Loading Vertical Shorts Plugin");

	/* Defer dock creation until the UI/video pipeline is ready. */
	obs_frontend_add_event_callback(FrontendEvent, nullptr);

	/* If the frontend already finished loading (hot reload / late load), register now. */
	if (obs_frontend_get_main_window()) {
		/* Still wait for FINISHED_LOADING when starting with OBS; only register
		 * immediately if scenes are already available (indicates UI is ready). */
		obs_source_t *scene = obs_frontend_get_current_scene();
		if (scene) {
			obs_source_release(scene);
			RegisterDock();
		}
	}

	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(FrontendEvent, nullptr);
	obs_frontend_remove_save_callback(SaveCallback, nullptr);

	if (g_dock) {
		obs_frontend_remove_dock(DOCK_ID);
		g_dock = nullptr;
	}

	blog(LOG_INFO, "[obs-shorts-vertical] Plugin unloaded");
}
