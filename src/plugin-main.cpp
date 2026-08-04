#include "shorts-dock.hpp"
#include "vertical-scenes-dock.hpp"
#include "vertical-sources-dock.hpp"
#include "vertical-transitions-dock.hpp"
#include "plugin-support.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>

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

static ShortsDock *g_workspace = nullptr;
static bool g_save_callback_registered = false;
static bool g_tools_menu_registered = false;
static bool g_docks_registered = false;

static const char *DOCK_CANVAS = "vertical_shorts_plugin_dock";
static const char *DOCK_SCENES = "vertical_shorts_scenes_dock";
static const char *DOCK_SOURCES = "vertical_shorts_sources_dock";
static const char *DOCK_TRANSITIONS = "vertical_shorts_transitions_dock";

static void SaveCallback(obs_data_t *save_data, bool saving, void *)
{
	if (!g_workspace)
		return;

	if (saving) {
		OBSDataAutoRelease obj = obs_data_create();
		g_workspace->SaveSettings(obj);
		obs_data_set_obj(save_data, "obs-shorts-vertical", obj);
	} else {
		obs_data_t *obj = obs_data_get_obj(save_data, "obs-shorts-vertical");
		if (obj) {
			g_workspace->LoadSettings(obj);
			obs_data_release(obj);
		}
	}
}

static void ShowDockById(const char *id)
{
	QMainWindow *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main)
		return;

	QDockWidget *dock = main->findChild<QDockWidget *>(id);
	if (!dock)
		return;

	dock->setVisible(true);
	dock->raise();
	if (dock->toggleViewAction())
		dock->toggleViewAction()->setChecked(true);
}

static void OnToolsShowDock(void *)
{
	if (!g_workspace) {
		blog(LOG_WARNING, "[obs-shorts-vertical] Tools menu: docks not registered yet");
		return;
	}
	ShowDockById(DOCK_CANVAS);
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
}

static bool AddDock(const char *id, const char *titleKey, const char *fallback, QWidget *widget)
{
	const char *title = obs_module_text(titleKey);
	if (!title || !*title)
		title = fallback;
	if (!obs_frontend_add_dock_by_id(id, title, widget)) {
		blog(LOG_ERROR, "[obs-shorts-vertical] Failed to add dock '%s'", id);
		delete widget;
		return false;
	}
	return true;
}

static void RegisterDocks()
{
	if (g_docks_registered)
		return;

	QMainWindow *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main) {
		blog(LOG_ERROR, "[obs-shorts-vertical] Main window not available; docks not registered");
		return;
	}

	auto *workspace = new ShortsDock(main);
	if (!AddDock(DOCK_CANVAS, "ShortsDock", "Vertical Shorts", workspace))
		return;
	g_workspace = workspace;

	if (!AddDock(DOCK_SCENES, "VerticalScenesDock", "Vertical Scenes", new VerticalScenesDock(workspace, main))) {
		/* Canvas already registered — keep workspace pointer. */
	}
	if (!AddDock(DOCK_SOURCES, "VerticalSourcesDock", "Vertical Sources",
		     new VerticalSourcesDock(workspace, main))) {
	}
	if (!AddDock(DOCK_TRANSITIONS, "VerticalTransitionsDock", "Vertical Transitions",
		     new VerticalTransitionsDock(workspace, main))) {
	}

	g_docks_registered = true;

	if (!g_save_callback_registered) {
		obs_frontend_add_save_callback(SaveCallback, nullptr);
		g_save_callback_registered = true;
	}

	RegisterToolsMenu();

	ShowDockById(DOCK_CANVAS);
	ShowDockById(DOCK_SCENES);
	ShowDockById(DOCK_SOURCES);
	ShowDockById(DOCK_TRANSITIONS);

	blog(LOG_INFO, "[obs-shorts-vertical] Native docks registered (canvas, scenes, sources, transitions)");
}

static void FrontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		RegisterDocks();
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[obs-shorts-vertical] Loading Vertical Shorts Plugin %s (libobs %s)", PLUGIN_VERSION,
	     obs_get_version_string());

	const char *bin = obs_get_module_binary_path(obs_current_module());
	const char *data = obs_get_module_data_path(obs_current_module());
	blog(LOG_INFO, "[obs-shorts-vertical] Module binary: %s", bin ? bin : "(null)");
	blog(LOG_INFO, "[obs-shorts-vertical] Module data: %s", data ? data : "(null)");

	obs_frontend_add_event_callback(FrontendEvent, nullptr);

	if (obs_frontend_get_main_window()) {
		obs_source_t *scene = obs_frontend_get_current_scene();
		if (scene) {
			obs_source_release(scene);
			RegisterDocks();
		}
	}

	return true;
}

void obs_module_post_load(void)
{
	blog(LOG_INFO, "[obs-shorts-vertical] obs_module_post_load");
	if (!g_docks_registered && obs_frontend_get_main_window())
		RegisterDocks();
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(FrontendEvent, nullptr);
	if (g_save_callback_registered) {
		obs_frontend_remove_save_callback(SaveCallback, nullptr);
		g_save_callback_registered = false;
	}

	if (g_docks_registered) {
		obs_frontend_remove_dock(DOCK_TRANSITIONS);
		obs_frontend_remove_dock(DOCK_SOURCES);
		obs_frontend_remove_dock(DOCK_SCENES);
		obs_frontend_remove_dock(DOCK_CANVAS);
		g_workspace = nullptr;
		g_docks_registered = false;
	}

	blog(LOG_INFO, "[obs-shorts-vertical] Plugin unloaded");
}
