#pragma once

#include "plugin-settings.hpp"
#include "qt-display.hpp"
#include "recording-automation.hpp"
#include "vertical-outputs.hpp"

#include <graphics/matrix4.h>
#include <graphics/vec2.h>
#include <obs-frontend-api.h>
#include <obs-hotkey.h>
#include <obs.hpp>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>

#include <functional>
#include <memory>
#include <vector>

#define ITEM_LEFT (1 << 0)
#define ITEM_RIGHT (1 << 1)
#define ITEM_TOP (1 << 2)
#define ITEM_BOTTOM (1 << 3)

enum class ItemHandle : uint32_t {
	None = 0,
	TopLeft = ITEM_TOP | ITEM_LEFT,
	TopCenter = ITEM_TOP,
	TopRight = ITEM_TOP | ITEM_RIGHT,
	CenterLeft = ITEM_LEFT,
	CenterRight = ITEM_RIGHT,
	BottomLeft = ITEM_BOTTOM | ITEM_LEFT,
	BottomCenter = ITEM_BOTTOM,
	BottomRight = ITEM_BOTTOM | ITEM_RIGHT,
};

using EventFilterFunc = std::function<bool(QObject *, QEvent *)>;

class OBSEventFilter : public QObject {
public:
	explicit OBSEventFilter(EventFilterFunc filter_) : filter(std::move(filter_)) {}

protected:
	bool eventFilter(QObject *obj, QEvent *event) override { return filter(obj, event); }
	EventFilterFunc filter;
};

class ShortsDock : public QFrame {
	Q_OBJECT

public:
	explicit ShortsDock(QWidget *parent = nullptr);
	~ShortsDock() override;

	void SaveSettings(obs_data_t *data);
	void LoadSettings(obs_data_t *data);

public slots:
	void HotkeySaveShortClip();
	void HotkeySaveLongClip();
	void HotkeyStartRecording();
	void HotkeyStopRecording();
	void HotkeyToggleRecording();
	void HotkeyStartLive();
	void HotkeyStopLive();
	void HotkeyOpenSettings();

private slots:
	void OnSceneSelectionChanged();
	void OnAddScene();
	void OnRemoveScene();
	void OnDuplicateScene();
	void OnRenameScene();
	void OnSourceSelectionChanged();
	void OnAddSource();
	void OnRemoveSource();
	void OnToggleSourceVisible();
	void OnToggleSourceLock();
	void OnSourceProperties();
	void OnSourceFilters();
	void OnSourceMoveUp();
	void OnSourceMoveDown();
	void OnFitToScreen();
	void OnStretchToScreen();
	void OnCenterToScreen();
	void OnResetTransform();
	void OnTransformEdited();
	void OnTransitionChanged(int index);
	void OnTransitionDurationChanged(int value);
	void OnGoLive();
	void OnRecord();
	void OnShortClip();
	void OnLongClip();
	void OnSettings();
	void RefreshScenesList();
	void RefreshSourcesList();
	void RefreshMixer();
	void RefreshTransitions();
	void RefreshTransformControls();
	void OnStreamingChanged(bool active);
	void OnRecordingChanged(bool active);
	void OnClipSaved(const QString &path, ClipKind kind);
	void SyncActiveSceneFromFrontend();
	void OnAutomationStatus(vsp::AutomationStatus status, const QString &text);
	void OnAutomationNotify(const QString &title, const QString &message);

private:
	void BuildUI();
	void RefreshVerticalWorkspace(bool force = false);
	void ApplyCanvasFromSettings();
	void CreateView();
	void DestroyView();
	void SetCanvasSize(uint32_t width, uint32_t height);
	void SetActiveScene(obs_scene_t *newScene, bool isVerticalMirror);
	obs_scene_t *EnsureVerticalMirror(obs_source_t *mainSceneSource);
	void SyncMirrorFromMain(obs_scene_t *mirror, obs_scene_t *mainScene);
	obs_scene_t *ActiveEditScene() const { return scene; }
	uint32_t ActiveCanvasWidth() const { return verticalWidth; }
	uint32_t ActiveCanvasHeight() const { return verticalHeight; }
	void HandleClipSaveResult(const ClipSaveInfo &info, ClipKind kind);
	void CollectSceneLists(QStringList &names, QStringList &uuids) const;
	void RegisterHotkeys();
	void UnregisterHotkeys();
	void SaveHotkeys(obs_data_t *data) const;
	void LoadHotkeys(obs_data_t *data);

	std::unique_ptr<OBSEventFilter> BuildEventFilter();
	bool HandlePreviewEvent(QObject *obj, QEvent *event);
	vec2 GetMouseEventPos(QMouseEvent *event);
	OBSSceneItem GetItemAtPos(const vec2 &pos, bool selectBelow);
	bool SelectedAtPos(const vec2 &pos);
	void DoSelect(const vec2 &pos);
	void GetStretchHandleData(const vec2 &pos);
	void MoveItems(const vec2 &pos);
	void StretchItem(const vec2 &pos);
	void DrawPreview(uint32_t cx, uint32_t cy);
	void DrawSceneEditing();
	static bool DrawSelectedItem(obs_scene_t *scene, obs_sceneitem_t *item, void *param);
	void UpdateCursor(uint32_t flags);
	void UpdatePreviewScale(int cx, int cy);
	void ShowContextMenu(const QPoint &globalPos);

	static void DrawCallback(void *data, uint32_t cx, uint32_t cy);
	static void FrontendEvent(enum obs_frontend_event event, void *private_data);
	static void HotkeyThunk(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);

	/* UI */
	OBSQTDisplay *preview = nullptr;
	std::unique_ptr<OBSEventFilter> previewEventFilter;
	QWidget *controlsOverlay = nullptr;
	QPushButton *goLiveBtn = nullptr;
	QPushButton *recordBtn = nullptr;
	QPushButton *shortClipBtn = nullptr;
	QPushButton *longClipBtn = nullptr;
	QPushButton *settingsBtn = nullptr;
	QLabel *autoIndicator = nullptr;

	QListWidget *scenesList = nullptr;
	QListWidget *sourcesList = nullptr;
	QWidget *mixerHost = nullptr;
	QVBoxLayout *mixerLayout = nullptr;
	QComboBox *transitionCombo = nullptr;
	QSpinBox *transitionDuration = nullptr;

	QDoubleSpinBox *posXSpin = nullptr;
	QDoubleSpinBox *posYSpin = nullptr;
	QDoubleSpinBox *sizeWSpin = nullptr;
	QDoubleSpinBox *sizeHSpin = nullptr;
	QDoubleSpinBox *rotSpin = nullptr;

	/* State */
	vsp::PluginSettings settings;
	std::unique_ptr<VerticalOutputs> outputs;
	std::unique_ptr<RecordingAutomation> automation;
	bool recordingStartedManually = false;

	obs_view_t *view = nullptr;
	video_t *video = nullptr;
	obs_scene_t *scene = nullptr;
	bool sceneIsMirror = false;
	QMap<QString, OBSScene> verticalMirrors;

	uint32_t verticalWidth = 1080;
	uint32_t verticalHeight = 1920;
	float previewScale = 1.0f;
	int previewX = 0;
	int previewY = 0;

	bool locked = false;
	bool mouseDown = false;
	bool mouseMoved = false;
	bool mouseOverItems = false;
	bool updatingTransform = false;
	bool clearing = false;
	bool loadingSettings = false;
	bool shuttingDown = false;

	vec2 startPos{};
	vec2 mousePos{};
	vec2 lastMoveOffset{};
	vec2 startItemPos{};
	vec2 stretchItemSize{};
	obs_sceneitem_crop startCrop{};
	vec2 cropSize{};

	OBSSceneItem stretchItem;
	ItemHandle stretchHandle = ItemHandle::None;
	matrix4 screenToItem{};
	matrix4 itemToScreen{};

	gs_vertbuffer_t *rectFill = nullptr;

	obs_hotkey_id hkShortClip = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hkLongClip = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hkStartRec = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hkStopRec = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hkToggleRec = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hkStartLive = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hkStopLive = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hkSettings = OBS_INVALID_HOTKEY_ID;
};
