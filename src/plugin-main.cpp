#include "shorts-dock.hpp"
#include "plugin-support.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-shorts-vertical", "en-US")
OBS_MODULE_AUTHOR("Vertical Shorts Plugin Contributors")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Professional Vertical Streaming Plugin for OBS Studio";
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Vertical Shorts Plugin";
}

static ShortsDock *g_dock = nullptr;
static bool g_save_callback_registered = false;
static bool g_tools_menu_registered = false;
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

static void OnToolsShowDock(void *)
{
	if (!g_dock) {
		blog(LOG_WARNING, "[obs-shorts-vertical] Tools menu: dock not registered yet");
		return;
	}
	ShowDock();
}

static void RegisterToolsMenu()
{
	if (g_tools_menu_registered)
		return;

	const char *title = obs_module_text("ShortsDockMenu");
	if (!title || !*title)
		title = "Vertical Shorts";

	obs_frontend_add_tools_menu_item(title, OnToolsShowDock, nullptr);
	g_tools_menu_registered = true;
	blog(LOG_INFO, "[obs-shorts-vertical] Tools menu item registered (%s)", title);
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

	ShortsDock *dock = new ShortsDock(main);

	const char *title = obs_module_text("ShortsDock");
	if (!title || !*title)
		title = "Vertical Shorts";

	const bool ok = obs_frontend_add_dock_by_id(DOCK_ID, title, dock);
	if (!ok) {
		blog(LOG_ERROR, "[obs-shorts-vertical] Failed to add dock id '%s' (already used?)", DOCK_ID);
		/* Do not leave a sticky pointer — allow a later retry. */
		delete dock;
		return;
	}

	g_dock = dock;

	if (!g_save_callback_registered) {
		obs_frontend_add_save_callback(SaveCallback, nullptr);
		g_save_callback_registered = true;
	}

	RegisterToolsMenu();

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
	blog(LOG_INFO, "[obs-shorts-vertical] Loading Vertical Shorts Plugin %s (libobs %s)",
	     PLUGIN_VERSION, obs_get_version_string());

	const char *bin = obs_get_module_binary_path(obs_current_module());
	const char *data = obs_get_module_data_path(obs_current_module());
	blog(LOG_INFO, "[obs-shorts-vertical] Module binary: %s", bin ? bin : "(null)");
	blog(LOG_INFO, "[obs-shorts-vertical] Module data: %s", data ? data : "(null)");

	/* Defer dock creation until the UI/video pipeline is ready. */
	obs_frontend_add_event_callback(FrontendEvent, nullptr);

	/* If the frontend already finished loading (hot reload / late load), register now. */
	if (obs_frontend_get_main_window()) {
		obs_source_t *scene = obs_frontend_get_current_scene();
		if (scene) {
			obs_source_release(scene);
			RegisterDock();
		}
	}

	return true;
}

void obs_module_post_load(void)
{
	blog(LOG_INFO, "[obs-shorts-vertical] obs_module_post_load");
	/* Retry dock registration if FINISHED_LOADING already fired before load. */
	if (!g_dock && obs_frontend_get_main_window())
		RegisterDock();
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(FrontendEvent, nullptr);
	if (g_save_callback_registered) {
		obs_frontend_remove_save_callback(SaveCallback, nullptr);
		g_save_callback_registered = false;
	}

	if (g_dock) {
		obs_frontend_remove_dock(DOCK_ID);
		g_dock = nullptr;
	}

	blog(LOG_INFO, "[obs-shorts-vertical] Plugin unloaded");
}
