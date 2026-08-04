#pragma once

#include "qt-display.hpp"

#include <graphics/matrix4.h>
#include <graphics/vec2.h>
#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <functional>
#include <memory>
#include <mutex>
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

	uint32_t CanvasWidth() const { return canvasWidth; }
	uint32_t CanvasHeight() const { return canvasHeight; }
	obs_scene_t *CurrentScene() const { return scene; }
	video_t *GetVideo() const { return video; }

private slots:
	void OnAddScene();
	void OnRemoveScene();
	void OnSceneChanged(int index);
	void OnAddCamera();
	void OnAddExistingSource();
	void OnRemoveSource();
	void OnSourceSelectionChanged();
	void OnCanvasPresetChanged(int index);
	void OnApplyCustomSize();
	void OnToggleRecord();
	void OnTransformEdited();
	void OnFitToScreen();
	void OnStretchToScreen();
	void OnCenterToScreen();
	void OnResetTransform();
	void OnLockToggled(bool locked);
	void RefreshSourcesList();
	void RefreshTransformControls();

private:
	void BuildUI();
	void CreateView();
	void DestroyView();
	void SetCanvasSize(uint32_t width, uint32_t height);
	void SetCurrentScene(obs_scene_t *newScene);
	obs_scene_t *CreateShortScene(const char *name);
	void UpdateScenesCombo();
	void UpdatePreviewScale(int cx, int cy);

	/* Preview interaction (move / resize like main OBS) */
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

	static void DrawCallback(void *data, uint32_t cx, uint32_t cy);
	static void FrontendEvent(enum obs_frontend_event event, void *private_data);

	OBSQTDisplay *preview = nullptr;
	std::unique_ptr<OBSEventFilter> previewEventFilter;
	QComboBox *scenesCombo = nullptr;
	QListWidget *sourcesList = nullptr;
	QComboBox *canvasPreset = nullptr;
	QSpinBox *widthSpin = nullptr;
	QSpinBox *heightSpin = nullptr;
	QPushButton *recordButton = nullptr;
	QCheckBox *lockCheck = nullptr;
	QLabel *helpLabel = nullptr;

	QDoubleSpinBox *posXSpin = nullptr;
	QDoubleSpinBox *posYSpin = nullptr;
	QDoubleSpinBox *sizeWSpin = nullptr;
	QDoubleSpinBox *sizeHSpin = nullptr;
	QDoubleSpinBox *rotSpin = nullptr;

	obs_view_t *view = nullptr;
	video_t *video = nullptr;
	obs_scene_t *scene = nullptr;
	obs_output_t *recordOutput = nullptr;

	uint32_t canvasWidth = 1080;
	uint32_t canvasHeight = 1920;
	float previewScale = 1.0f;
	int previewX = 0;
	int previewY = 0;

	bool locked = false;
	bool mouseDown = false;
	bool mouseMoved = false;
	bool mouseOverItems = false;
	bool updatingTransform = false;
	bool clearing = false;

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

	std::vector<OBSScene> scenes;
	std::mutex selectMutex;
	std::vector<obs_sceneitem_t *> selectedItems;

	gs_vertbuffer_t *box = nullptr;
	gs_vertbuffer_t *rectFill = nullptr;

	QPointer<QAction> toggleAction;
};
