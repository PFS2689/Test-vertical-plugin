#include "shorts-dock.hpp"
#include "capture-source-share.hpp"
#include "credential-store.hpp"
#include "display-helpers.hpp"
#include "settings-dialog.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSharedPointer>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>
#include <QtMath>

#include <initializer_list>

#include <graphics/vec3.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(v) ((void)(v))
#endif

struct ItemTransform {
	vec2 pos;
	vec2 scale;
	float rot = 0.0f;
};

static ItemTransform ReadItemTransform(obs_sceneitem_t *item)
{
	ItemTransform t{};
	obs_sceneitem_get_pos(item, &t.pos);
	obs_sceneitem_get_scale(item, &t.scale);
	t.rot = obs_sceneitem_get_rot(item);
	return t;
}

/* Vertical-only clipboards (never touch main OBS scene-item transforms). */
struct SourceClipboard {
	std::string id;
	OBSData settings;
	OBSData hotkeys;
	bool valid = false;
};

struct TransformClipboard {
	obs_transform_info info{};
	obs_sceneitem_crop crop{};
	bool valid = false;
};

static SourceClipboard g_sourceClipboard;
static TransformClipboard g_transformClipboard;

static const uint32_t kAlignIndexTable[] = {
	OBS_ALIGN_TOP | OBS_ALIGN_LEFT,    OBS_ALIGN_TOP,    OBS_ALIGN_TOP | OBS_ALIGN_RIGHT,
	OBS_ALIGN_LEFT,                    OBS_ALIGN_CENTER, OBS_ALIGN_RIGHT,
	OBS_ALIGN_BOTTOM | OBS_ALIGN_LEFT, OBS_ALIGN_BOTTOM, OBS_ALIGN_BOTTOM | OBS_ALIGN_RIGHT,
};

static int AlignToIndex(uint32_t align)
{
	for (int i = 0; i < 9; i++) {
		if (kAlignIndexTable[i] == align)
			return i;
	}
	return 4;
}

static QString UniqueSourceName(const QString &base)
{
	QString text = base;
	int i = 2;
	OBSSourceAutoRelease existing = obs_get_source_by_name(text.toUtf8().constData());
	while (existing) {
		text = QStringLiteral("%1 %2").arg(base).arg(i++);
		existing = obs_get_source_by_name(text.toUtf8().constData());
	}
	return text;
}

#define HANDLE_RADIUS 5.0f

namespace {

const char *Translate(const char *key)
{
	return obs_module_text(key);
}

struct ItemAtPosData {
	vec2 pos;
	obs_sceneitem_t *item = nullptr;
	bool selectBelow;
	obs_sceneitem_t *selectedBelow = nullptr;
};

bool ItemAtPosFilter(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *data = static_cast<ItemAtPosData *>(param);
	if (!obs_sceneitem_visible(item))
		return true;

	matrix4 transform;
	matrix4 invTransform;
	vec3 transformedPos;
	vec3 pos3;
	vec3 pos3_;

	vec3_set(&pos3, data->pos.x, data->pos.y, 0.0f);
	obs_sceneitem_get_box_transform(item, &transform);
	matrix4_inv(&invTransform, &transform);
	vec3_transform(&transformedPos, &pos3, &invTransform);
	vec3_transform(&pos3_, &transformedPos, &transform);

	if (vec3_dist(&pos3, &pos3_) > 1.0f)
		return true;
	if (transformedPos.x < 0.0f || transformedPos.x > 1.0f || transformedPos.y < 0.0f ||
	    transformedPos.y > 1.0f)
		return true;

	if (data->selectBelow && obs_sceneitem_selected(item)) {
		if (!data->item)
			data->selectedBelow = item;
		return true;
	}

	data->item = item;
	return false;
}

struct SelectedAtPosData {
	vec2 pos;
	bool selected = false;
};

bool SelectedAtPosFilter(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *data = static_cast<SelectedAtPosData *>(param);
	if (!obs_sceneitem_selected(item) || !obs_sceneitem_visible(item))
		return true;

	matrix4 transform;
	matrix4 invTransform;
	vec3 transformedPos;
	vec3 pos3;
	vec3 pos3_;

	vec3_set(&pos3, data->pos.x, data->pos.y, 0.0f);
	obs_sceneitem_get_box_transform(item, &transform);
	matrix4_inv(&invTransform, &transform);
	vec3_transform(&transformedPos, &pos3, &invTransform);
	vec3_transform(&pos3_, &transformedPos, &transform);

	if (vec3_dist(&pos3, &pos3_) > 1.0f)
		return true;
	if (transformedPos.x < 0.0f || transformedPos.x > 1.0f || transformedPos.y < 0.0f ||
	    transformedPos.y > 1.0f)
		return true;

	data->selected = true;
	return false;
}

bool CollectSelected(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *items = static_cast<std::vector<obs_sceneitem_t *> *>(param);
	if (obs_sceneitem_selected(item))
		items->push_back(item);
	return true;
}

bool ClearSelection(obs_scene_t *, obs_sceneitem_t *item, void *)
{
	obs_sceneitem_select(item, false);
	return true;
}

void DrawRect(float thickness, vec2 scale)
{
	gs_render_start(true);

	gs_vertex2f(0.0f, 0.0f);
	gs_vertex2f(0.0f + (thickness / scale.x), 0.0f);
	gs_vertex2f(0.0f, 1.0f);
	gs_vertex2f(0.0f + (thickness / scale.x), 1.0f);
	gs_vertex2f(0.0f, 1.0f - (thickness / scale.y));
	gs_vertex2f(1.0f, 1.0f);
	gs_vertex2f(1.0f, 1.0f - (thickness / scale.y));
	gs_vertex2f(1.0f - (thickness / scale.x), 1.0f);
	gs_vertex2f(1.0f, 0.0f);
	gs_vertex2f(1.0f - (thickness / scale.x), 0.0f);
	gs_vertex2f(1.0f, 0.0f + (thickness / scale.y));
	gs_vertex2f(0.0f, 0.0f);
	gs_vertex2f(0.0f, 0.0f + (thickness / scale.y));

	gs_vertbuffer_t *rect = gs_render_save();
	gs_load_vertexbuffer(rect);
	gs_draw(GS_TRISTRIP, 0, 0);
	gs_vertexbuffer_destroy(rect);
}

void DrawSquareAtPos(float x, float y, float radius)
{
	struct vec3 pos;
	vec3_set(&pos, x, y, 0.0f);

	struct matrix4 matrix;
	gs_matrix_get(&matrix);
	vec3_transform(&pos, &pos, &matrix);

	gs_matrix_push();
	gs_matrix_identity();
	gs_matrix_translate(&pos);
	gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, RAD(-matrix.x.y));
	gs_matrix_translate3f(-radius, -radius, 0.0f);
	gs_matrix_scale3f(radius * 2.0f, radius * 2.0f, 1.0f);
	gs_draw(GS_TRISTRIP, 0, 0);
	gs_matrix_pop();
}

obs_source_t *FindFrontendSceneByUuid(const QString &uuid)
{
	if (uuid.isEmpty())
		return nullptr;

	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	obs_source_t *found = nullptr;
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *src = scenes.sources.array[i];
		const char *id = obs_source_get_uuid(src);
		if (id && uuid == QString::fromUtf8(id)) {
			found = obs_source_get_ref(src);
			break;
		}
	}
	obs_frontend_source_list_free(&scenes);
	return found;
}

bool MirrorHasSource(obs_scene_t *mirror, obs_source_t *source)
{
	struct Data {
		obs_source_t *source;
		bool found;
	} data{source, false};

	obs_scene_enum_items(
		mirror,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
			auto *d = static_cast<Data *>(param);
			if (obs_sceneitem_get_source(item) == d->source) {
				d->found = true;
				return false;
			}
			return true;
		},
		&data);
	return data.found;
}

obs_sceneitem_t *FindItemBySource(obs_scene_t *scene, obs_source_t *source)
{
	struct Data {
		obs_source_t *source;
		obs_sceneitem_t *item;
	} data{source, nullptr};

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
			auto *d = static_cast<Data *>(param);
			if (obs_sceneitem_get_source(item) == d->source) {
				d->item = item;
				return false;
			}
			return true;
		},
		&data);
	return data.item;
}

obs_sceneitem_t *FindItemById(obs_scene_t *scene, int64_t id)
{
	struct Data {
		int64_t id;
		obs_sceneitem_t *item;
	} data{id, nullptr};

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
			auto *d = static_cast<Data *>(param);
			if (obs_sceneitem_get_id(item) == d->id) {
				d->item = item;
				return false;
			}
			return true;
		},
		&data);
	return data.item;
}

QToolButton *MakeToolButton(QWidget *parent, const QString &text, const QString &tip)
{
	auto *btn = new QToolButton(parent);
	btn->setText(text);
	btn->setToolTip(tip);
	btn->setAutoRaise(true);
	return btn;
}

struct CollectSourcesCtx {
	std::vector<obs_source_t *> *sources;
};

bool CollectMainSources(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *ctx = static_cast<CollectSourcesCtx *>(param);
	ctx->sources->push_back(obs_sceneitem_get_source(item));
	return true;
}

struct RemoveMissingCtx {
	const std::vector<obs_source_t *> *mainSources;
	std::vector<obs_sceneitem_t *> *toRemove;
};

bool CollectMissingMirrorItems(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *ctx = static_cast<RemoveMissingCtx *>(param);
	obs_source_t *src = obs_sceneitem_get_source(item);
	bool present = false;
	for (obs_source_t *m : *ctx->mainSources) {
		if (m == src) {
			present = true;
			break;
		}
	}
	if (!present)
		ctx->toRemove->push_back(item);
	return true;
}

} // namespace

ShortsDock::ShortsDock(QWidget *parent) : QFrame(parent)
{
	setObjectName("ShortsDock");
	setMinimumWidth(200);
	setMinimumHeight(240);

	settings.recordingPath = vsp::DefaultRecordingPath();
	vsp::EnsureDefaultDestinations(settings);
	vsp::CanvasSizeForPreset(settings.canvasPreset, settings.customWidth, settings.customHeight, verticalWidth,
				 verticalHeight);
	verticalTransitionName = QStringLiteral("Fade");
	verticalTransitionDurationMs = 300;

	outputs = std::make_unique<VerticalOutputs>(this);
	automation = std::make_unique<RecordingAutomation>(outputs.get(), this);
	connect(outputs.get(), &VerticalOutputs::streamingChanged, this, &ShortsDock::OnStreamingChanged);
	connect(outputs.get(), &VerticalOutputs::recordingChanged, this, &ShortsDock::OnRecordingChanged);
	connect(outputs.get(), &VerticalOutputs::clipSaved, this, &ShortsDock::OnClipSaved);
	connect(outputs.get(), &VerticalOutputs::bufferStatusChanged, this, &ShortsDock::OnBufferStatus);
	connect(automation.get(), &RecordingAutomation::statusChanged, this, &ShortsDock::OnAutomationStatus);
	connect(automation.get(), &RecordingAutomation::notify, this, &ShortsDock::OnAutomationNotify);

	BuildUI();
	BootstrapVerticalCanvasPipeline();
	if (outputs) {
		outputs->SetVideo(video);
		outputs->ApplySettings(settings);
	}
	if (automation)
		automation->ApplySettings(settings);

	obs_frontend_add_event_callback(FrontendEvent, this);
	RegisterHotkeys();

	RefreshVerticalWorkspace(true);
	EmitSceneUiChanged();
	emit verticalTransitionsChanged();

	QTimer::singleShot(0, this, [this]() { EnsureBufferIfConfigured(); });
}

ShortsDock::~ShortsDock()
{
	obs_frontend_remove_event_callback(FrontendEvent, this);
	UnregisterHotkeys();
	clearing = true;
	shuttingDown = true;

	if (automation)
		automation->OnObsShutdown();

	if (outputs)
		outputs->StopAll();

	EnsurePreviewSceneShowing(false);

	if (scene) {
		obs_scene_release(scene);
		scene = nullptr;
	}

	DestroyView();

	verticalTransition = nullptr;
	transitionPreviewScene = nullptr;
	verticalScenes.clear();
	sceneOrder.clear();

	obs_enter_graphics();
	gs_vertexbuffer_destroy(rectFill);
	rectFill = nullptr;
	gs_vertexbuffer_destroy(box);
	box = nullptr;
	obs_leave_graphics();
}

void ShortsDock::BuildUI()
{
	/* Dark dock chrome — never inherit a white QFrame/QWidget fill behind the preview. */
	setObjectName(QStringLiteral("ShortsDock"));
	setStyleSheet(QStringLiteral(
		"#ShortsDock { background-color: #1f1f1f; }"
		"#VerticalShortsPreview { background-color: #282828; }"
		"#vsControlsBar, #vsPresetBar { background-color: #1f1f1f; }"
		"#vsControlsBar QPushButton {"
		"  font-size: 14px;"
		"  min-width: 28px;"
		"  max-width: 32px;"
		"  min-height: 24px;"
		"  max-height: 26px;"
		"  padding: 0px 1px;"
		"  border: 1px solid #3a3a3a;"
		"  border-radius: 3px;"
		"  background-color: #2a2a2a;"
		"  color: #f0f0f0;"
		"}"
		"#vsControlsBar QPushButton:checked {"
		"  background-color: #3d5a3d;"
		"  border-color: #6aae6a;"
		"}"
		"#vsControlsBar QPushButton:pressed { background-color: #333333; }"
		"#vsPresetBar QLabel {"
		"  color: #d0d0d0;"
		"  font-size: 11px;"
		"  padding: 0px;"
		"}"
		"#vsPresetBar QComboBox {"
		"  min-height: 22px;"
		"  max-height: 24px;"
		"  font-size: 11px;"
		"  padding: 0px 4px;"
		"}"));

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(2, 2, 2, 2);
	root->setSpacing(2);

	preview = new OBSQTDisplay(this);
	preview->setObjectName(QStringLiteral("VerticalShortsPreview"));
	preview->setMinimumSize(120, 120);
	preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	preview->setMouseTracking(true);
	/* OBS-style dark preview clear color (never white). */
	preview->SetDisplayBackgroundColor(QColor(0x28, 0x28, 0x28));
	previewEventFilter = BuildEventFilter();
	preview->installEventFilter(previewEventFilter.get());

	auto addDrawCallback = [this]() {
		obs_display_t *display = preview ? preview->GetDisplay() : nullptr;
		if (display) {
			obs_display_set_background_color(display, GREY_COLOR_BACKGROUND);
			obs_display_set_enabled(display, true);
			obs_display_add_draw_callback(display, DrawCallback, this);
			blog(LOG_INFO,
			     "[obs-shorts-vertical] Preview display ready: enabled=%d draw_callback=registered",
			     (int)obs_display_enabled(display));
		} else {
			blog(LOG_ERROR, "[obs-shorts-vertical] DisplayCreated but GetDisplay() is null — canvas would be blank");
		}
		/* Display is ready — bind PROGRAM channel 0 so ACTIVATE/MAIN_VIEW reaches VCDs. */
		EnsureCanvasProgramChannel(true);
		EnsurePreviewSceneShowing(true);
		LogRenderPipeline("DisplayCreated");
	};
	connect(preview, &OBSQTDisplay::DisplayCreated, addDrawCallback);
	root->addWidget(preview, 1); /* stretch: canvas takes all extra space */

	/* Retry display attach after layout — dock widgets are often not exposed at ctor time.
	 * Without obs_display, Windows shows a solid white HWND. */
	QTimer::singleShot(0, this, [this]() {
		if (preview)
			preview->CreateDisplay(true);
	});
	QTimer::singleShot(250, this, [this]() {
		if (preview && !preview->GetDisplay()) {
			blog(LOG_WARNING, "[obs-shorts-vertical] Preview display still missing after 250ms — forcing create");
			preview->CreateDisplay(true);
		}
		EnsureCanvasProgramChannel(true);
	});

	/* Compact OBS-style emoji toolbar — fixed height, centered. */
	controlsBar = new QWidget(this);
	controlsBar->setObjectName(QStringLiteral("vsControlsBar"));
	controlsBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	auto *bar = new QHBoxLayout(controlsBar);
	bar->setContentsMargins(2, 1, 2, 1);
	bar->setSpacing(3);

	/* Emoji-only controls — tooltips carry the accessible names. */
	goLiveBtn = new QPushButton(QString::fromUtf8("\U0001F7E2"), controlsBar);
	goLiveBtn->setCheckable(true);
	goLiveBtn->setToolTip(Translate("GoLive"));
	goLiveBtn->setAccessibleName(Translate("GoLive"));
	connect(goLiveBtn, &QPushButton::clicked, this, &ShortsDock::OnGoLive);

	recordBtn = new QPushButton(QString::fromUtf8("\u23FA\uFE0F"), controlsBar);
	recordBtn->setCheckable(true);
	recordBtn->setToolTip(Translate("Record"));
	recordBtn->setAccessibleName(Translate("Record"));
	connect(recordBtn, &QPushButton::clicked, this, &ShortsDock::OnRecord);

	shortClipBtn = new QPushButton(QString::fromUtf8("\U0001F4F8"), controlsBar);
	shortClipBtn->setToolTip(Translate("ShortClipTip"));
	shortClipBtn->setAccessibleName(Translate("ShortClip"));
	connect(shortClipBtn, &QPushButton::clicked, this, &ShortsDock::OnShortClip);

	longClipBtn = new QPushButton(QString::fromUtf8("\U0001F4F7"), controlsBar);
	longClipBtn->setToolTip(Translate("LongClipTip"));
	longClipBtn->setAccessibleName(Translate("LongClip"));
	connect(longClipBtn, &QPushButton::clicked, this, &ShortsDock::OnLongClip);

	settingsBtn = new QPushButton(QString::fromUtf8("\u2699\uFE0F"), controlsBar);
	settingsBtn->setToolTip(Translate("Settings"));
	settingsBtn->setAccessibleName(Translate("Settings"));
	connect(settingsBtn, &QPushButton::clicked, this, &ShortsDock::OnSettings);

	bar->addStretch(1);
	bar->addWidget(goLiveBtn);
	bar->addWidget(recordBtn);
	bar->addWidget(shortClipBtn);
	bar->addWidget(longClipBtn);
	bar->addWidget(settingsBtn);
	bar->addStretch(1);
	root->addWidget(controlsBar, 0);

	/* Preset row directly under emoji toolbar — fixed/minimum height. */
	presetBar = new QWidget(this);
	presetBar->setObjectName(QStringLiteral("vsPresetBar"));
	presetBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	auto *presetLayout = new QHBoxLayout(presetBar);
	presetLayout->setContentsMargins(4, 1, 4, 2);
	presetLayout->setSpacing(4);

	auto *presetLabel = new QLabel(Translate("DockPreset") + QStringLiteral(":"), presetBar);

	canvasPresetCombo = new QComboBox(presetBar);
	canvasPresetCombo->setObjectName(QStringLiteral("vsCanvasPreset"));
	canvasPresetCombo->setToolTip(Translate("CanvasPreset"));
	canvasPresetCombo->setAccessibleName(Translate("CanvasPreset"));
	canvasPresetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	canvasPresetCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	canvasPresetCombo->setMinimumContentsLength(12);
	connect(canvasPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&ShortsDock::OnCanvasPresetChanged);
	presetLabel->setBuddy(canvasPresetCombo);

	/* Clip length selectors stay available but off the emoji row. */
	shortClipPresetCombo = new QComboBox(presetBar);
	shortClipPresetCombo->setObjectName(QStringLiteral("vsShortClipPreset"));
	shortClipPresetCombo->setToolTip(Translate("ShortClipLength"));
	shortClipPresetCombo->setAccessibleName(Translate("ShortClipLength"));
	shortClipPresetCombo->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
	shortClipPresetCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	connect(shortClipPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&ShortsDock::OnShortClipPresetChanged);

	longClipPresetCombo = new QComboBox(presetBar);
	longClipPresetCombo->setObjectName(QStringLiteral("vsLongClipPreset"));
	longClipPresetCombo->setToolTip(Translate("LongClipLength"));
	longClipPresetCombo->setAccessibleName(Translate("LongClipLength"));
	longClipPresetCombo->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
	longClipPresetCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	connect(longClipPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&ShortsDock::OnLongClipPresetChanged);

	presetLayout->addWidget(presetLabel, 0);
	presetLayout->addWidget(canvasPresetCombo, 1);
	presetLayout->addWidget(shortClipPresetCombo, 0);
	presetLayout->addWidget(longClipPresetCombo, 0);
	root->addWidget(presetBar, 0);

	SyncCanvasPresetControl();
	SyncClipPresetControls();
}

void ShortsDock::ApplyCanvasFromSettings()
{
	vsp::CanvasSizeForPreset(settings.canvasPreset, settings.customWidth, settings.customHeight, verticalWidth,
				 verticalHeight);
}

void ShortsDock::RefreshVerticalWorkspace(bool force)
{
	BootstrapVerticalCanvasPipeline();
	if (outputs)
		outputs->SetVideo(video);
	/* Always rebind PROGRAM channel — SetActiveScene early-outs when the
	 * scene pointer is unchanged, which left channel 0 empty after canvas
	 * recreate/reset and produced a blank Vertical Shorts preview. */
	EnsureCanvasProgramChannel(force);
	EmitSourceUiChanged();
	if (force)
		LogRenderPipeline("RefreshVerticalWorkspace");
}

size_t ShortsDock::CountSceneItems(obs_scene_t *sc)
{
	if (!sc)
		return 0;
	size_t count = 0;
	obs_scene_enum_items(
		sc,
		[](obs_scene_t *, obs_sceneitem_t *, void *param) -> bool {
			(*static_cast<size_t *>(param))++;
			return true;
		},
		&count);
	return count;
}

void ShortsDock::BootstrapVerticalCanvasPipeline()
{
	/* 1) Vertical Shorts obs_canvas_t @ 1080x1920, FPS from global OBS render. */
	CreateView();

	/* 2) Vertical Scene 1 via obs_canvas_scene_create — never a Main OBS scene. */
	EnsureDefaultVerticalScene();

	/* 3) PROGRAM channel 0 = Vertical Scene 1 (MAIN_VIEW activation for capture). */
	if (canvas && scene) {
		obs_source_t *want = obs_scene_get_source(scene);
		obs_canvas_set_channel(canvas, 0, want);
		blog(LOG_INFO,
		     "[obs-shorts-vertical] Bootstrap: channel 0 → '%s' on Vertical Shorts canvas "
		     "(%ux%u, items=%zu)",
		     want ? obs_source_get_name(want) : "(null)", verticalWidth, verticalHeight,
		     CountSceneItems(scene));
	}

	EnsurePreviewSceneShowing(true);
}

void ShortsDock::MaybeBootstrapIndependentCamera()
{
	if (bootstrapCameraAttempted || clearing || shuttingDown || loadingSettings || !scene)
		return;
	bootstrapCameraAttempted = true;

	if (CountSceneItems(scene) > 0) {
		blog(LOG_INFO,
		     "[obs-shorts-vertical] Bootstrap camera skipped — Vertical Scene already has sources");
		return;
	}

	blog(LOG_INFO,
	     "[obs-shorts-vertical] Bootstrap: creating independent dshow_input Video Capture Device "
	     "on Vertical Scene 1 (not Main OBS)");
	CreateIndependentVideoCapture("dshow_input", QString::fromUtf8(obs_module_text("VideoCaptureDevice")));
}

void ShortsDock::EnsureDefaultVerticalScene()
{
	if (!verticalScenes.isEmpty()) {
		if (!scene && !sceneOrder.isEmpty() && !loadingSettings)
			RequestSelectScene(sceneOrder.first());
		return;
	}

	obs_scene_t *created = CreateVerticalScene("Vertical Scene 1");
	if (!created)
		return;
	obs_source_t *src = obs_scene_get_source(created);
	const char *uuid = src ? obs_source_get_uuid(src) : nullptr;
	if (uuid && *uuid) {
		const QString key = QString::fromUtf8(uuid);
		verticalScenes.insert(key, created);
		sceneOrder.append(key);
		SetActiveScene(created, false);
		blog(LOG_INFO,
		     "[obs-shorts-vertical] Created Vertical Scene 1 via obs_canvas_scene_create "
		     "(canvas=%p scene=%p)",
		     (void *)canvas, (void *)created);
	}
	obs_scene_release(created);
}

void ShortsDock::EmitSceneUiChanged()
{
	emit verticalScenesChanged();
	EmitSourceUiChanged();
}

void ShortsDock::EmitSourceUiChanged()
{
	emit verticalSourcesChanged();
	emit verticalTransformChanged();
}

obs_scene_t *ShortsDock::FindVerticalSceneByUuid(const QString &uuid) const
{
	auto it = verticalScenes.find(uuid);
	if (it == verticalScenes.end())
		return nullptr;
	return it.value();
}

QString ShortsDock::ActiveSceneUuid() const
{
	if (!scene)
		return {};
	obs_source_t *src = obs_scene_get_source(scene);
	const char *uuid = src ? obs_source_get_uuid(src) : nullptr;
	return uuid ? QString::fromUtf8(uuid) : QString();
}

void ShortsDock::CreateView()
{
	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi)) {
		blog(LOG_WARNING, "[obs-shorts-vertical] obs_get_video_info failed");
		return;
	}

	/* Vertical canvas geometry — independent of Main OBS base size.
	 * FPS / color format follow the global OBS render video_info (often 30 FPS). */
	ovi.base_width = verticalWidth;
	ovi.base_height = verticalHeight;
	ovi.output_width = verticalWidth;
	ovi.output_height = verticalHeight;

	/* ACTIVATE | SCENE_REF — MAIN_VIEW activation for capture devices.
	 * MIX_AUDIO intentionally omitted so vertical canvas audio is not mixed
	 * into the main OBS program audio output. */
	const uint32_t verticalCanvasFlags = ACTIVATE | SCENE_REF;
	if (!canvas) {
		canvas = obs_canvas_create_private("Vertical Shorts", &ovi, verticalCanvasFlags);
		if (!canvas) {
			blog(LOG_ERROR, "[obs-shorts-vertical] obs_canvas_create_private failed");
			video = nullptr;
			return;
		}
		blog(LOG_INFO,
		     "[obs-shorts-vertical] Vertical Shorts obs_canvas_t created: %ux%u @ %u/%u FPS "
		     "(ACTIVATE|SCENE_REF, private — not Main OBS)",
		     verticalWidth, verticalHeight, ovi.fps_num, ovi.fps_den);
	} else {
		struct obs_video_info cur = {};
		bool needReset = !obs_canvas_has_video(canvas);
		if (!needReset && obs_canvas_get_video_info(canvas, &cur)) {
			needReset = cur.base_width != verticalWidth || cur.base_height != verticalHeight ||
				    cur.output_width != verticalWidth || cur.output_height != verticalHeight ||
				    cur.fps_num != ovi.fps_num || cur.fps_den != ovi.fps_den;
		} else if (!needReset) {
			needReset = true;
		}
		if (needReset) {
			if (!obs_canvas_reset_video(canvas, &ovi))
				blog(LOG_WARNING, "[obs-shorts-vertical] obs_canvas_reset_video failed");
			else
				blog(LOG_INFO,
				     "[obs-shorts-vertical] Vertical canvas reset to %ux%u @ %u/%u FPS",
				     verticalWidth, verticalHeight, ovi.fps_num, ovi.fps_den);
		}
	}

	video = obs_canvas_get_video(canvas);
	if (!video)
		blog(LOG_WARNING, "[obs-shorts-vertical] Vertical canvas has no video output");

	AttachScenesToCanvas();
	EnsureCanvasProgramChannel(false);
}

void ShortsDock::DestroyView()
{
	if (canvas) {
		obs_canvas_set_channel(canvas, 0, nullptr);
		obs_canvas_remove(canvas);
		obs_canvas_release(canvas);
		canvas = nullptr;
	}
	video = nullptr;
}

void ShortsDock::AttachScenesToCanvas()
{
	if (!canvas)
		return;

	for (auto it = verticalScenes.begin(); it != verticalScenes.end(); ++it) {
		obs_scene_t *sc = it.value();
		if (!sc)
			continue;
		obs_source_t *src = obs_scene_get_source(sc);
		if (!src)
			continue;
		OBSCanvasAutoRelease owned = obs_source_get_canvas(src);
		if (owned == canvas)
			continue;
		obs_canvas_move_scene(sc, canvas);
		blog(LOG_INFO, "[obs-shorts-vertical] Moved scene '%s' onto Vertical Shorts canvas",
		     obs_source_get_name(src));
	}
}

obs_scene_t *ShortsDock::CreateVerticalScene(const char *name)
{
	if (!canvas)
		CreateView();
	if (!canvas)
		return nullptr;
	obs_scene_t *created = obs_canvas_scene_create(canvas, name);
	if (!created) {
		blog(LOG_ERROR, "[obs-shorts-vertical] obs_canvas_scene_create('%s') failed",
		     name ? name : "");
		return nullptr;
	}
	blog(LOG_INFO,
	     "[obs-shorts-vertical] obs_canvas_scene_create('%s') → scene=%p on Vertical Shorts canvas %p",
	     name ? name : "", (void *)created, (void *)canvas);
	return created;
}

void ShortsDock::SetCanvasSize(uint32_t width, uint32_t height)
{
	if (width < 2)
		width = 2;
	if (height < 2)
		height = 2;
	if (verticalWidth == width && verticalHeight == height && canvas && video)
		return;
	verticalWidth = width;
	verticalHeight = height;
	CreateView();
	if (outputs)
		outputs->SetVideo(video);
	if (canvas && scene)
		obs_canvas_set_channel(canvas, 0, obs_scene_get_source(scene));
}

void ShortsDock::SetActiveScene(obs_scene_t *newScene, bool withTransition)
{
	if (scene == newScene) {
		/* Scene pointer unchanged — still ensure the PROGRAM channel is
		 * bound so ACTIVATE/MAIN_VIEW reaches vertical scene items. */
		EnsureCanvasProgramChannel(false);
		return;
	}

	obs_source_t *oldSrc = scene ? obs_scene_get_source(scene) : nullptr;
	obs_source_t *newSrc = newScene ? obs_scene_get_source(newScene) : nullptr;

	if (scene) {
		obs_scene_release(scene);
		scene = nullptr;
	}

	if (newScene)
		scene = obs_scene_get_ref(newScene);

	/* Channel set on an ACTIVATE canvas uses MAIN_VIEW so capture devices
	 * receive activate_refs when visible. Do not also call
	 * obs_source_inc_showing — that double-counts show_refs. */
	if (canvas) {
		if (withTransition && oldSrc && newSrc && verticalTransitionDurationMs > 0) {
			obs_source_t *tr = EnsureVerticalTransitionSource(verticalTransitionName);
			if (tr) {
				obs_transition_set(tr, oldSrc);
				obs_canvas_set_channel(canvas, 0, tr);
				obs_transition_start(tr, OBS_TRANSITION_MODE_AUTO, verticalTransitionDurationMs, newSrc);
			} else {
				obs_canvas_set_channel(canvas, 0, newSrc);
			}
		} else {
			obs_canvas_set_channel(canvas, 0, newSrc);
		}
	}

	if (newSrc) {
		blog(LOG_INFO,
		     "[obs-shorts-vertical] Active vertical scene '%s' active=%d showing=%d "
		     "size=%ux%u canvas=%p video=%p",
		     obs_source_get_name(newSrc), (int)obs_source_active(newSrc), (int)obs_source_showing(newSrc),
		     obs_source_get_width(newSrc), obs_source_get_height(newSrc), (void *)canvas, (void *)video);
	}

	EnsurePreviewSceneShowing(true);
	EmitSourceUiChanged();
}

void ShortsDock::EnsureCanvasProgramChannel(bool forceRebind)
{
	if (!canvas || !scene)
		return;

	obs_source_t *want = obs_scene_get_source(scene);
	if (!want)
		return;

	obs_source_t *cur = obs_canvas_get_channel(canvas, 0);
	const bool same = (cur == want);
	if (cur)
		obs_source_release(cur);

	if (same && !forceRebind) {
		EnsurePreviewSceneShowing(true);
		return;
	}

	if (same && forceRebind) {
		/* obs_canvas_set_channel no-ops when the pointer is unchanged and
		 * will not re-run activate. Clear then rebind to refresh refs. */
		obs_canvas_set_channel(canvas, 0, nullptr);
	}

	obs_canvas_set_channel(canvas, 0, want);
	blog(LOG_INFO,
	     "[obs-shorts-vertical] PROGRAM channel 0 → '%s' (force=%d) active=%d showing=%d",
	     obs_source_get_name(want), (int)forceRebind, (int)obs_source_active(want),
	     (int)obs_source_showing(want));

	/* Keep dock preview show_refs aligned with the active scene. */
	EnsurePreviewSceneShowing(true);
}

void ShortsDock::EnsurePreviewSceneShowing(bool enable)
{
	/* OBS secondary-display pattern: while a source is drawn in an obs_display
	 * callback, hold an extra show_ref so capture/lifecycle stays correct even
	 * if the ACTIVATE canvas path is momentarily unbound. */
	obs_source_t *want = (enable && scene && !clearing && !shuttingDown) ? obs_scene_get_source(scene) : nullptr;

	if (previewShowingHeld && previewShowingSource) {
		if (want && previewShowingSource.Get() == want)
			return;
		obs_source_dec_showing(previewShowingSource);
		previewShowingSource = nullptr;
		previewShowingHeld = false;
	}

	if (want) {
		obs_source_inc_showing(want);
		previewShowingSource = want;
		previewShowingHeld = true;
		blog(LOG_INFO, "[obs-shorts-vertical] Preview show_ref +1 on '%s' (showing=%d active=%d)",
		     obs_source_get_name(want), (int)obs_source_showing(want), (int)obs_source_active(want));
	}
}

void ShortsDock::ValidateItemTransform(obs_sceneitem_t *item)
{
	if (!item)
		return;

	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!SourceIsVisual(source))
		return;

	obs_transform_info info{};
	obs_sceneitem_get_info2(item, &info);
	const float canvasW = float(verticalWidth > 0 ? verticalWidth : 1080);
	const float canvasH = float(verticalHeight > 0 ? verticalHeight : 1920);

	bool invalid = false;
	if (!obs_sceneitem_visible(item)) {
		/* New cameras must be visible by default. */
		obs_sceneitem_set_visible(item, true);
	}
	if (!std::isfinite(info.pos.x) || !std::isfinite(info.pos.y) || !std::isfinite(info.scale.x) ||
	    !std::isfinite(info.scale.y) || !std::isfinite(info.rot))
		invalid = true;
	if (info.bounds_type != OBS_BOUNDS_NONE) {
		if (!std::isfinite(info.bounds.x) || !std::isfinite(info.bounds.y) || info.bounds.x < 1.0f ||
		    info.bounds.y < 1.0f)
			invalid = true;
	} else {
		/* Native/original placement: reject far off-canvas / zero scale. */
		if (info.pos.x < -canvasW * 2.0f || info.pos.y < -canvasH * 2.0f || info.pos.x > canvasW * 3.0f ||
		    info.pos.y > canvasH * 3.0f)
			invalid = true;
		if (std::fabs(info.scale.x) < 0.0001f || std::fabs(info.scale.y) < 0.0001f)
			invalid = true;
	}

	obs_sceneitem_crop crop{};
	obs_sceneitem_get_crop(item, &crop);
	const uint32_t sw = source ? obs_source_get_width(source) : 0;
	const uint32_t sh = source ? obs_source_get_height(source) : 0;
	if (sw > 0 && sh > 0) {
		if ((int)crop.left + (int)crop.right >= (int)sw || (int)crop.top + (int)crop.bottom >= (int)sh)
			invalid = true;
	}

	if (invalid) {
		blog(LOG_WARNING,
		     "[obs-shorts-vertical] Invalid vertical transform on '%s' — repairing with Fit to Vertical Canvas",
		     source ? obs_source_get_name(source) : "?");
		StoreFillPosition(item, VerticalFillPosition::Center);
		ApplyVerticalFitMode(item, VerticalFitMode::FitInside, true);
	}
}

void ShortsDock::LogRenderPipeline(const char *reason)
{
	obs_source_t *channel0 = canvas ? obs_canvas_get_channel(canvas, 0) : nullptr;
	obs_source_t *sceneSrc = scene ? obs_scene_get_source(scene) : nullptr;

	size_t itemCount = 0;
	if (scene) {
		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *, void *p) -> bool {
				(*static_cast<size_t *>(p))++;
				return true;
			},
			&itemCount);
	}

	obs_display_t *display = preview ? preview->GetDisplay() : nullptr;
	blog(LOG_INFO,
	     "[obs-shorts-vertical] pipeline(%s): scene=%p '%s' scene_active=%d scene_showing=%d "
	     "items=%zu channel0=%p '%s' canvas=%p video=%p canvas_size=%ux%u draws=%llu "
	     "display=%p display_enabled=%d preview_show_held=%d",
	     reason ? reason : "?", (void *)scene, sceneSrc ? obs_source_get_name(sceneSrc) : "(null)",
	     sceneSrc ? (int)obs_source_active(sceneSrc) : -1, sceneSrc ? (int)obs_source_showing(sceneSrc) : -1,
	     itemCount, (void *)channel0, channel0 ? obs_source_get_name(channel0) : "(null)", (void *)canvas,
	     (void *)video, verticalWidth, verticalHeight, (unsigned long long)drawCallbackCount, (void *)display,
	     display ? (int)obs_display_enabled(display) : -1, (int)previewShowingHeld);

	if (scene) {
		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *item, void *) -> bool {
				obs_source_t *src = obs_sceneitem_get_source(item);
				if (!src)
					return true;
				vec2 pos{}, scale{}, bounds{};
				obs_sceneitem_crop crop{};
				obs_sceneitem_get_pos(item, &pos);
				obs_sceneitem_get_scale(item, &scale);
				obs_sceneitem_get_bounds(item, &bounds);
				obs_sceneitem_get_crop(item, &crop);
				blog(LOG_INFO,
				     "[obs-shorts-vertical]   item '%s' type=%s visible=%d "
				     "src_active=%d src_showing=%d src_enabled=%d size=%ux%u "
				     "pos=(%.1f,%.1f) scale=(%.3f,%.3f) bounds_type=%d bounds=(%.1f,%.1f) "
				     "crop=L%u R%u T%u B%u",
				     obs_source_get_name(src), obs_source_get_id(src),
				     (int)obs_sceneitem_visible(item), (int)obs_source_active(src),
				     (int)obs_source_showing(src), (int)obs_source_enabled(src),
				     obs_source_get_width(src), obs_source_get_height(src), pos.x, pos.y, scale.x,
				     scale.y, (int)obs_sceneitem_get_bounds_type(item), bounds.x, bounds.y, crop.left,
				     crop.right, crop.top, crop.bottom);
				return true;
			},
			nullptr);
	}

	if (channel0)
		obs_source_release(channel0);
}

obs_source_t *ShortsDock::EnsureVerticalTransitionSource(const QString &name)
{
	obs_frontend_source_list transitions = {};
	obs_frontend_get_transitions(&transitions);
	const char *id = "fade_transition";
	obs_data_t *copySettings = nullptr;
	for (size_t i = 0; i < transitions.sources.num; i++) {
		obs_source_t *src = transitions.sources.array[i];
		if (name == QString::fromUtf8(obs_source_get_name(src))) {
			id = obs_source_get_id(src);
			copySettings = obs_source_get_settings(src);
			break;
		}
	}
	obs_frontend_source_list_free(&transitions);

	if (verticalTransition) {
		if (QString::fromUtf8(obs_source_get_id(verticalTransition)) == QString::fromUtf8(id)) {
			if (copySettings)
				obs_data_release(copySettings);
			return verticalTransition;
		}
		verticalTransition = nullptr;
	}

	obs_source_t *priv = obs_source_create_private(id, "Vertical Transition", copySettings);
	if (copySettings)
		obs_data_release(copySettings);
	if (!priv)
		priv = obs_source_create_private("fade_transition", "Vertical Transition", nullptr);
	if (priv) {
		verticalTransition = priv;
		obs_source_release(priv);
	}
	return verticalTransition;
}

vsp::AutomationStatus ShortsDock::CurrentAutomationStatus() const
{
	return automationStatus;
}

QString ShortsDock::CurrentAutomationStatusText() const
{
	return automationStatusText;
}

/* ---------------- Companion dock API ---------------- */

void ShortsDock::PopulateScenesList(QListWidget *list)
{
	if (!list)
		return;
	const QString current = ActiveSceneUuid();
	list->clear();
	int select = -1;
	for (int i = 0; i < sceneOrder.size(); ++i) {
		const QString &uuid = sceneOrder[i];
		obs_scene_t *sc = FindVerticalSceneByUuid(uuid);
		if (!sc)
			continue;
		obs_source_t *src = obs_scene_get_source(sc);
		auto *row = new QListWidgetItem(QString::fromUtf8(obs_source_get_name(src)));
		row->setData(Qt::UserRole, uuid);
		list->addItem(row);
		if (uuid == current)
			select = list->count() - 1;
	}
	if (select >= 0)
		list->setCurrentRow(select);
}

void ShortsDock::RequestSelectScene(const QString &uuid)
{
	if (loadingSettings || clearing || uuid.isEmpty())
		return;
	obs_scene_t *sc = FindVerticalSceneByUuid(uuid);
	if (!sc)
		return;
	SetActiveScene(sc, true);
	if (automation) {
		obs_source_t *src = obs_scene_get_source(sc);
		automation->OnSceneChanged(uuid, QString::fromUtf8(obs_source_get_name(src)));
	}
}

void ShortsDock::RequestAddScene()
{
	bool ok = false;
	QString name = QInputDialog::getText(this, Translate("AddScene"), Translate("NewSceneName"), QLineEdit::Normal,
					     Translate("NewSceneName"), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;

	obs_scene_t *created = CreateVerticalScene(name.trimmed().toUtf8().constData());
	if (!created)
		return;
	obs_source_t *src = obs_scene_get_source(created);
	const char *uuid = src ? obs_source_get_uuid(src) : nullptr;
	if (uuid && *uuid) {
		const QString key = QString::fromUtf8(uuid);
		verticalScenes.insert(key, created);
		sceneOrder.append(key);
		SetActiveScene(created, false);
	}
	obs_scene_release(created);
	EmitSceneUiChanged();
}

void ShortsDock::RequestRemoveScene()
{
	const QString uuid = ActiveSceneUuid();
	if (uuid.isEmpty())
		return;
	if (QMessageBox::question(this, Translate("RemoveScene"), Translate("ConfirmRemoveScene")) != QMessageBox::Yes)
		return;

	obs_scene_t *sc = FindVerticalSceneByUuid(uuid);
	if (scene && ActiveSceneUuid() == uuid)
		SetActiveScene(nullptr, false);

	if (sc) {
		obs_source_t *src = obs_scene_get_source(sc);
		if (canvas)
			obs_canvas_scene_remove(sc);
		if (src)
			obs_source_remove(src);
	}

	verticalScenes.remove(uuid);
	sceneOrder.removeAll(uuid);
	if (!sceneOrder.isEmpty())
		RequestSelectScene(sceneOrder.first());
	else
		EnsureDefaultVerticalScene();
	EmitSceneUiChanged();
}

void ShortsDock::RequestDuplicateScene()
{
	const QString uuid = ActiveSceneUuid();
	obs_scene_t *srcScene = FindVerticalSceneByUuid(uuid);
	if (!srcScene)
		return;
	obs_source_t *src = obs_scene_get_source(srcScene);
	bool ok = false;
	QString name = QInputDialog::getText(this, Translate("DuplicateScene"), Translate("NewSceneName"),
					     QLineEdit::Normal,
					     QString::fromUtf8(obs_source_get_name(src)) + QStringLiteral(" Copy"), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;

	obs_scene_t *dup = obs_scene_duplicate(srcScene, name.trimmed().toUtf8().constData(), OBS_SCENE_DUP_REFS);
	if (!dup)
		return;
	if (canvas)
		obs_canvas_move_scene(dup, canvas);
	obs_source_t *dupSrc = obs_scene_get_source(dup);
	const char *newUuid = dupSrc ? obs_source_get_uuid(dupSrc) : nullptr;
	if (newUuid && *newUuid) {
		const QString key = QString::fromUtf8(newUuid);
		verticalScenes.insert(key, dup);
		sceneOrder.append(key);
		SetActiveScene(dup, false);
	}
	obs_scene_release(dup);
	EmitSceneUiChanged();
}

void ShortsDock::RequestRenameScene()
{
	const QString uuid = ActiveSceneUuid();
	obs_scene_t *sc = FindVerticalSceneByUuid(uuid);
	if (!sc)
		return;
	obs_source_t *src = obs_scene_get_source(sc);
	bool ok = false;
	QString name = QInputDialog::getText(this, Translate("RenameScene"), Translate("RenameScenePrompt"),
					     QLineEdit::Normal, QString::fromUtf8(obs_source_get_name(src)), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;
	RequestRenameSceneTo(name);
}

void ShortsDock::RequestRenameSceneTo(const QString &name)
{
	const QString trimmed = name.trimmed();
	if (trimmed.isEmpty())
		return;
	const QString uuid = ActiveSceneUuid();
	obs_scene_t *sc = FindVerticalSceneByUuid(uuid);
	if (!sc)
		return;
	obs_source_t *src = obs_scene_get_source(sc);
	if (!src)
		return;
	if (QString::fromUtf8(obs_source_get_name(src)) == trimmed)
		return;
	obs_source_set_name(src, trimmed.toUtf8().constData());
	EmitSceneUiChanged();
}

void ShortsDock::RequestSceneMoveUp()
{
	const QString uuid = ActiveSceneUuid();
	const int idx = sceneOrder.indexOf(uuid);
	if (idx <= 0)
		return;
	sceneOrder.swapItemsAt(idx, idx - 1);
	EmitSceneUiChanged();
}

void ShortsDock::RequestSceneMoveDown()
{
	const QString uuid = ActiveSceneUuid();
	const int idx = sceneOrder.indexOf(uuid);
	if (idx < 0 || idx >= sceneOrder.size() - 1)
		return;
	sceneOrder.swapItemsAt(idx, idx + 1);
	EmitSceneUiChanged();
}

void ShortsDock::RequestSceneMoveTop()
{
	const QString uuid = ActiveSceneUuid();
	const int idx = sceneOrder.indexOf(uuid);
	if (idx <= 0)
		return;
	sceneOrder.removeAt(idx);
	sceneOrder.prepend(uuid);
	EmitSceneUiChanged();
}

void ShortsDock::RequestSceneMoveBottom()
{
	const QString uuid = ActiveSceneUuid();
	const int idx = sceneOrder.indexOf(uuid);
	if (idx < 0 || idx >= sceneOrder.size() - 1)
		return;
	sceneOrder.removeAt(idx);
	sceneOrder.append(uuid);
	EmitSceneUiChanged();
}

void ShortsDock::PopulateSourcesList(QListWidget *list)
{
	if (!list)
		return;
	list->clear();
	if (!scene)
		return;

	/* Enumerate bottom→top, then reverse so the UI matches OBS (top row = topmost). */
	std::vector<obs_sceneitem_t *> items;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
			static_cast<std::vector<obs_sceneitem_t *> *>(param)->push_back(item);
			return true;
		},
		&items);

	for (auto it = items.rbegin(); it != items.rend(); ++it) {
		obs_sceneitem_t *item = *it;
		obs_source_t *source = obs_sceneitem_get_source(item);
		const QString name = QString::fromUtf8(obs_source_get_name(source));
		/* Display text stays empty — the row widget paints the name.
		 * Leaving text set causes QListWidget to paint it under/over the widget (overlap). */
		auto *row = new QListWidgetItem();
		row->setData(Qt::UserRole, QVariant::fromValue((qint64)obs_sceneitem_get_id(item)));
		row->setData(Qt::UserRole + 1, obs_sceneitem_visible(item));
		row->setData(Qt::UserRole + 2, obs_sceneitem_locked(item));
		row->setData(Qt::UserRole + 3, source ? obs_source_configurable(source) : false);
		row->setData(Qt::UserRole + 4, name);
		row->setToolTip(name);
		if (obs_sceneitem_selected(item))
			row->setSelected(true);
		list->addItem(row);
	}
}

void ShortsDock::RequestSelectSource(qint64 itemId)
{
	if (!scene || updatingTransform)
		return;
	obs_scene_enum_items(scene, ClearSelection, nullptr);
	obs_sceneitem_t *si = FindItemById(scene, itemId);
	if (si)
		obs_sceneitem_select(si, true);
	emit verticalTransformChanged();
}

void ShortsDock::RequestAddSource()
{
	ShowAddSourceMenu(this);
}

void ShortsDock::ShowAddSourceMenu(QWidget *button)
{
	if (!scene) {
		EnsureDefaultVerticalScene();
		if (!scene)
			return;
	}

	/* Fresh menu every open — never append onto a previous instance. */
	QMenu menu(button ? button : this);

	/* --- Create New (dedupe by stable unversioned source ID) --- */
	QMenu *createMenu = menu.addMenu(Translate("CreateNewSource"));
	size_t idx = 0;
	const char *typeId = nullptr;
	const char *unversioned = nullptr;
	struct TypeEntry {
		QString label;
		std::string unversionedId; /* primary key for menu dedupe */
	};
	std::vector<TypeEntry> types;
	QSet<QString> seenUnversioned;
	while (obs_enum_input_types2(idx++, &typeId, &unversioned)) {
		if (!typeId || !*typeId)
			continue;
		if (strcmp(typeId, "scene") == 0 || strcmp(typeId, "group") == 0)
			continue;
		const char *stable = (unversioned && *unversioned) ? unversioned : typeId;
		const QString key = QString::fromUtf8(stable);
		if (seenUnversioned.contains(key))
			continue;
		seenUnversioned.insert(key);
		/* Prefer display name of the latest versioned implementation. */
		const std::string latestId = vsp::ResolveLatestInputTypeId(stable);
		const char *display = obs_source_get_display_name(latestId.c_str());
		if (!display || !*display)
			display = obs_source_get_display_name(typeId);
		if (!display || !*display)
			continue;
		types.push_back({QString::fromUtf8(display), stable});
	}
	std::sort(types.begin(), types.end(),
		  [](const TypeEntry &a, const TypeEntry &b) { return a.label.localeAwareCompare(b.label) < 0; });
	blog(LOG_INFO, "[obs-shorts-vertical] Add Source menu: %zu unique input types (deduped by unversioned id)",
	     types.size());
	for (const TypeEntry &t : types) {
		QAction *act = createMenu->addAction(t.label);
		const std::string unversionedCopy = t.unversionedId;
		const QString labelCopy = t.label;
		connect(act, &QAction::triggered, this, [this, unversionedCopy, labelCopy]() {
			if (!scene)
				return;
			const std::string createId = vsp::ResolveLatestInputTypeId(unversionedCopy.c_str());
			if (vsp::IsVideoCaptureSourceId(unversionedCopy.c_str()) ||
			    vsp::IsVideoCaptureSourceId(createId.c_str())) {
				CreateIndependentVideoCapture(unversionedCopy, labelCopy);
				return;
			}
			const QString name = UniqueSourceName(labelCopy);
			obs_source_t *created =
				obs_source_create(createId.c_str(), name.toUtf8().constData(), nullptr, nullptr);
			if (!created) {
				blog(LOG_ERROR,
				     "[obs-shorts-vertical] obs_source_create failed for id='%s' (unversioned='%s')",
				     createId.c_str(), unversionedCopy.c_str());
				QMessageBox::warning(this, Translate("AddSource"), Translate("CreateSourceFailed"));
				return;
			}
			AddSourceToActiveScene(created, true);
			if (obs_source_configurable(created))
				obs_frontend_open_source_properties(created);
			obs_source_release(created);
			EmitSourceUiChanged();
		});
	}
	if (types.empty())
		createMenu->setEnabled(false);

	/* --- Add Existing Source (explicit reference; independent vertical transform) --- */
	QMenu *existingMenu = menu.addMenu(Translate("AddExistingSource"));
	existingMenu->setToolTip(Translate("SelectSource"));
	std::vector<OBSSource> existingSources;
	obs_enum_sources(
		[](void *param, obs_source_t *source) -> bool {
			auto *out = static_cast<std::vector<OBSSource> *>(param);
			if (obs_source_is_group(source))
				return true;
			const char *id = obs_source_get_id(source);
			if (id && (strcmp(id, "scene") == 0))
				return true;
			out->emplace_back(source);
			return true;
		},
		&existingSources);
	std::sort(existingSources.begin(), existingSources.end(), [](const OBSSource &a, const OBSSource &b) {
		return QString::fromUtf8(obs_source_get_name(a)).localeAwareCompare(
			       QString::fromUtf8(obs_source_get_name(b))) < 0;
	});
	for (OBSSource &src : existingSources) {
		const QString name = QString::fromUtf8(obs_source_get_name(src));
		const QString typeName = QString::fromUtf8(obs_source_get_display_name(obs_source_get_id(src)));
		QString label = typeName.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, typeName);
		QAction *act = existingMenu->addAction(label);
		connect(act, &QAction::triggered, this, [this, src]() {
			if (vsp::IsVideoCaptureSourceId(obs_source_get_id(src)) &&
			    vsp::VerticalSceneHasSource(scene, src)) {
				QMessageBox::information(this, Translate("AddSource"),
							 Translate("SharedCameraAlreadyAdded"));
				return;
			}
			AddSourceToActiveScene(src, true);
			if (vsp::IsVideoCaptureSourceId(obs_source_get_id(src)))
				NotifySharedCameraFeed();
			EmitSourceUiChanged();
		});
	}
	if (existingSources.empty())
		existingMenu->setEnabled(false);

	/* --- Add Existing Scene --- */
	QMenu *sceneMenu = menu.addMenu(Translate("AddExistingScene"));
	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *src = scenes.sources.array[i];
		/* Skip the active vertical scene source to avoid cycles. */
		if (scene && obs_scene_get_source(scene) == src)
			continue;
		const QString name = QString::fromUtf8(obs_source_get_name(src));
		OBSSource held = src;
		QAction *act = sceneMenu->addAction(name);
		connect(act, &QAction::triggered, this, [this, held]() {
			AddSourceToActiveScene(held, true);
			EmitSourceUiChanged();
		});
	}
	obs_frontend_source_list_free(&scenes);
	if (sceneMenu->actions().isEmpty())
		sceneMenu->setEnabled(false);

	menu.addSeparator();

	/* --- Share Existing Camera (explicit opt-in only; never silent) --- */
	const std::vector<vsp::CaptureSourceInfo> captures = vsp::EnumerateCaptureSources();
	QMenu *shareMenu = menu.addMenu(Translate("ShareExistingCamera"));
	shareMenu->setToolTip(Translate("ShareExistingCameraTip"));
	for (const vsp::CaptureSourceInfo &cap : captures) {
		const QString name = QString::fromStdString(cap.displayName);
		const QString typeName = QString::fromUtf8(obs_source_get_display_name(cap.typeId.c_str()));
		QString label = typeName.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, typeName);
		if (cap.width > 0 && cap.height > 0)
			label += QStringLiteral("  [%1×%2]").arg(cap.width).arg(cap.height);
		else if (!cap.deviceKey.empty())
			label += QStringLiteral("  [device]");
		QAction *act = shareMenu->addAction(label);
		OBSSource held = cap.source;
		connect(act, &QAction::triggered, this, [this, held]() {
			if (!scene || !held)
				return;
			if (vsp::VerticalSceneHasSource(scene, held)) {
				QMessageBox::information(this, Translate("AddSource"),
							 Translate("SharedCameraAlreadyAdded"));
				return;
			}
			AddSourceToActiveScene(held, true);
			NotifySharedCameraFeed();
			EmitSourceUiChanged();
		});
	}
	if (captures.empty())
		shareMenu->setEnabled(false);

	const QPoint pos = button ? button->mapToGlobal(QPoint(0, button->height()))
				  : QCursor::pos();
	menu.exec(pos);
}

void ShortsDock::RemoveVerticalItemsForSource(obs_source_t *source)
{
	if (!scene || !source)
		return;
	std::vector<obs_sceneitem_t *> items;
	struct Ctx {
		obs_source_t *source;
		std::vector<obs_sceneitem_t *> *items;
	} ctx{source, &items};
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
			auto *c = static_cast<Ctx *>(param);
			if (obs_sceneitem_get_source(item) == c->source)
				c->items->push_back(item);
			return true;
		},
		&ctx);
	for (obs_sceneitem_t *item : items)
		obs_sceneitem_remove(item);
}

void ShortsDock::NotifySharedCameraFeed()
{
	/* One informational notice per dock lifetime — avoid dialog spam. */
	if (sharedCameraNoticeShown)
		return;
	sharedCameraNoticeShown = true;
	QMessageBox::information(this, Translate("AddSource"), Translate("SharedCameraFeedInfo"));
}

void ShortsDock::LogCameraSourceDiagnostics(const char *phase, obs_source_t *source, obs_sceneitem_t *item,
					    bool creationReturnedNull) const
{
	if (creationReturnedNull) {
		blog(LOG_ERROR,
		     "[obs-shorts-vertical] Camera diag [%s]: source creation returned null",
		     phase ? phase : "?");
		return;
	}
	if (!source) {
		blog(LOG_ERROR, "[obs-shorts-vertical] Camera diag [%s]: source pointer is null",
		     phase ? phase : "?");
		return;
	}

	const char *id = obs_source_get_id(source);
	const char *name = obs_source_get_name(source);
	const std::string deviceKey = vsp::GetCaptureDeviceKey(source);
	const std::string deviceName = vsp::GetCaptureDeviceDisplayName(source);
	const uint32_t w = obs_source_get_width(source);
	const uint32_t h = obs_source_get_height(source);
	const uint32_t flags = obs_source_get_output_flags(source);
	const bool onScene = scene && vsp::VerticalSceneHasSource(scene, source);
	const bool itemVisible = item ? obs_sceneitem_visible(item) : false;
	const bool callbackSeesItems = drawCallbackCount > 0;
	const bool independent = IsIndependentCapture(source);

	blog(LOG_INFO,
	     "[obs-shorts-vertical] Camera diag [%s]: id=%s name='%s' device_id='%s' device_name='%s' "
	     "width=%u height=%u active=%d showing=%d enabled=%d flags=0x%x independent=%d "
	     "added_to_vertical_scene=%d item_visible=%d render_callback_alive=%d draw_frames=%llu",
	     phase ? phase : "?", id ? id : "(null)", name ? name : "(null)",
	     deviceKey.c_str(), deviceName.c_str(), w, h, (int)obs_source_active(source),
	     (int)obs_source_showing(source), (int)obs_source_enabled(source), flags,
	     (int)independent, (int)onScene, (int)itemVisible, (int)callbackSeesItems,
	     (unsigned long long)drawCallbackCount);

	if (w == 0 || h == 0) {
		blog(LOG_WARNING,
		     "[obs-shorts-vertical] Camera diag [%s]: dimensions are 0x0 — camera is NOT initialized "
		     "(do not treat as ready)",
		     phase ? phase : "?");
	}
}

void ShortsDock::LogCameraSettingsSnapshot(const char *phase, obs_source_t *source) const
{
	if (!source)
		return;
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	if (!settings) {
		blog(LOG_WARNING, "[obs-shorts-vertical] Camera settings [%s]: settings object is null",
		     phase ? phase : "?");
		return;
	}

	const char *videoDevice = obs_data_get_string(settings, "video_device");
	const char *videoDeviceId = obs_data_get_string(settings, "video_device_id");
	const char *lastVideoDeviceId = obs_data_get_string(settings, "last_video_device_id");
	const char *resolution = obs_data_get_string(settings, "resolution");
	const long long frameInterval = obs_data_get_int(settings, "frame_interval");
	const long long videoFormat = obs_data_get_int(settings, "video_format");
	const bool active = obs_data_get_bool(settings, "active");
	const bool deactivateWns = obs_data_get_bool(settings, "deactivate_when_not_showing");
	const char *colorSpace = obs_data_get_string(settings, "color_space");
	const char *colorRange = obs_data_get_string(settings, "color_range");

	blog(LOG_INFO,
	     "[obs-shorts-vertical] Camera settings [%s]: video_device='%s' video_device_id='%s' "
	     "last_video_device_id='%s' resolution='%s' frame_interval=%lld video_format=%lld "
	     "active=%d deactivate_when_not_showing=%d color_space='%s' color_range='%s' "
	     "source_size=%ux%u",
	     phase ? phase : "?", videoDevice ? videoDevice : "", videoDeviceId ? videoDeviceId : "",
	     lastVideoDeviceId ? lastVideoDeviceId : "", resolution ? resolution : "",
	     (long long)frameInterval, (long long)videoFormat, (int)active, (int)deactivateWns,
	     colorSpace ? colorSpace : "", colorRange ? colorRange : "", obs_source_get_width(source),
	     obs_source_get_height(source));

	if ((!videoDeviceId || !*videoDeviceId) && (!lastVideoDeviceId || !*lastVideoDeviceId) &&
	    (!videoDevice || !*videoDevice)) {
		blog(LOG_ERROR,
		     "[obs-shorts-vertical] Camera settings [%s]: device field is EMPTY — Properties selection "
		     "was not saved onto this source",
		     phase ? phase : "?");
	}
}

void ShortsDock::ApplySafeCaptureDefaults(obs_source_t *source)
{
	if (!source)
		return;
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	if (!settings)
		return;

	/* Baseline init: prefer device-default negotiation only.
	 * Do NOT force 1920x1080, 60 FPS, or a custom pixel format. */
	obs_data_set_int(settings, "res_type", 0);          /* ResType_Preferred */
	obs_data_set_string(settings, "resolution", "");
	obs_data_set_int(settings, "frame_interval", -1);   /* FPS_MATCHING / ignored for Preferred */
	obs_data_set_int(settings, "video_format", 0);      /* VideoFormat::Any */
	obs_data_set_bool(settings, "active", true);
	obs_data_set_bool(settings, "deactivate_when_not_showing", false);

	blog(LOG_INFO,
	     "[obs-shorts-vertical] Applying safe camera defaults on '%s' "
	     "(Preferred res/FPS, format=Any) — device_id retained",
	     obs_source_get_name(source));
	LogCameraSettingsSnapshot("before_safe_defaults_update", source);
	obs_source_update(source, settings);
	LogCameraSettingsSnapshot("after_safe_defaults_update", source);
}

void ShortsDock::ForceCaptureDeviceOpen(obs_source_t *source, bool applySafeDefaults)
{
	if (!source)
		return;

	const std::string key = vsp::GetCaptureDeviceKey(source);
	if (key.empty()) {
		blog(LOG_INFO,
		     "[obs-shorts-vertical] ForceCaptureDeviceOpen skipped for '%s' — no device ID yet "
		     "(wait for Properties to save video_device_id onto this same source %p)",
		     obs_source_get_name(source), (void *)source);
		return;
	}

	obs_source_set_enabled(source, true);

	if (applySafeDefaults)
		ApplySafeCaptureDefaults(source);

	OBSDataAutoRelease settings = obs_source_get_settings(source);
	if (settings) {
		obs_data_set_bool(settings, "active", true);
		obs_data_set_bool(settings, "deactivate_when_not_showing", false);
		/* win-dshow Update() only QueueActivate when input->active is already true.
		 * First open after Properties may still be inactive — SetActive(true) below. */
		obs_source_update(source, settings);
	}

	/* Activate once. Do NOT deactivate first — that tears down a graph that
	 * Properties / Update may have just started and prevents frames from arriving. */
	proc_handler_t *ph = obs_source_get_proc_handler(source);
	if (ph) {
		calldata_t cd;
		calldata_init(&cd);
		calldata_set_bool(&cd, "active", true);
		const bool ok = proc_handler_call(ph, "activate", &cd);
		calldata_free(&cd);
		blog(LOG_INFO,
		     "[obs-shorts-vertical] Camera proc activate(true) %s for '%s' (source=%p key=%s) "
		     "(no prior deactivate)",
		     ok ? "called" : "unavailable", obs_source_get_name(source), (void *)source, key.c_str());
	}

	EnsureCanvasProgramChannel(true);
	EnsurePreviewSceneShowing(true);

	blog(LOG_INFO,
	     "[obs-shorts-vertical] ForceCaptureDeviceOpen '%s' source=%p: active=%d showing=%d enabled=%d "
	     "size=%ux%u device_key=%s",
	     obs_source_get_name(source), (void *)source, (int)obs_source_active(source),
	     (int)obs_source_showing(source), (int)obs_source_enabled(source), obs_source_get_width(source),
	     obs_source_get_height(source), key.c_str());
}

const char *ShortsDock::ClassifyCameraInitFailure(obs_source_t *source) const
{
	if (!source)
		return "CaptureCreateFailed";

	const char *uuid = obs_source_get_uuid(source);
	if (!uuid || !*uuid)
		return "CaptureSourceDestroyed";

	const std::string key = vsp::GetCaptureDeviceKey(source);
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	const char *deviceId = settings ? obs_data_get_string(settings, "video_device_id") : nullptr;
	const char *deviceName = settings ? obs_data_get_string(settings, "video_device") : nullptr;

	if (key.empty() && (!deviceId || !*deviceId) && (!deviceName || !*deviceName))
		return "CaptureDeviceIdMissing";

	if ((!deviceId || !*deviceId) && deviceName && *deviceName)
		return "CaptureDeviceIdInvalid";

	if (deviceId && *deviceId) {
		OBSSourceAutoRelease peer = vsp::FindExistingCaptureByDeviceKey(key, source);
		if (peer && (obs_source_get_width(peer) > 0 || obs_source_get_height(peer) > 0))
			return "CaptureIndependentBusy";
	}

	if (!obs_source_enabled(source))
		return "CaptureNotActive";

	if (!scene || !vsp::VerticalSceneHasSource(scene, source))
		return "CaptureNotActive";

	const long long resType = settings ? obs_data_get_int(settings, "res_type") : 0;
	const char *resolution = settings ? obs_data_get_string(settings, "resolution") : nullptr;
	if (resType != 0 && resolution && *resolution)
		return "CaptureUnsupportedFormat";

	if (!obs_source_active(source) && !obs_source_showing(source))
		return "CaptureNotActive";

	if (obs_source_get_width(source) == 0 || obs_source_get_height(source) == 0) {
		if (deviceId && *deviceId)
			return "CaptureDeviceNotOpened";
		return "CaptureInitFailedZeroSize";
	}

	return "CaptureInitTimeout";
}

void ShortsDock::ReportCameraInitFailure(obs_source_t *source)
{
	obs_sceneitem_t *item = nullptr;
	if (source && scene) {
		struct Find {
			obs_source_t *src;
			obs_sceneitem_t *item;
		} find{source, nullptr};
		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *si, void *p) -> bool {
				auto *f = static_cast<Find *>(p);
				if (obs_sceneitem_get_source(si) == f->src) {
					f->item = si;
					return false;
				}
				return true;
			},
			&find);
		item = find.item;
	}

	const char *key = ClassifyCameraInitFailure(source);
	LogCameraSourceDiagnostics("CAMERA_INITIALIZATION_TIMEOUT", source, item, false);
	LogCameraSettingsSnapshot("CAMERA_INITIALIZATION_TIMEOUT", source);

	const std::string deviceKey = source ? vsp::GetCaptureDeviceKey(source) : std::string();
	blog(LOG_ERROR,
	     "[obs-shorts-vertical] Camera initialization timeout\n"
	     "  Source ID: %s\n"
	     "  Source name: %s\n"
	     "  Source pointer: %p\n"
	     "  Device ID present: %s\n"
	     "  Width: %u\n"
	     "  Height: %u\n"
	     "  Active: %d\n"
	     "  Showing: %d\n"
	     "  Scene item present: %s\n"
	     "  Scene item visible: %s\n"
	     "  Classified cause: %s",
	     source ? obs_source_get_id(source) : "(null)",
	     source ? obs_source_get_name(source) : "(null)", (void *)source,
	     deviceKey.empty() ? "no" : "yes", source ? obs_source_get_width(source) : 0,
	     source ? obs_source_get_height(source) : 0, source ? (int)obs_source_active(source) : 0,
	     source ? (int)obs_source_showing(source) : 0, item ? "yes" : "no",
	     item ? (obs_sceneitem_visible(item) ? "yes" : "no") : "n/a", key);

	QMessageBox::warning(this, Translate("AddSource"), Translate(key));
}

void ShortsDock::OnPendingCameraUpdate(void *data, calldata_t *)
{
	auto *dock = static_cast<ShortsDock *>(data);
	if (!dock)
		return;
	QMetaObject::invokeMethod(
		dock,
		[dock]() {
			if (dock->clearing || !dock->pendingCameraSource)
				return;
			obs_source_t *src = dock->pendingCameraSource;
			blog(LOG_INFO,
			     "[obs-shorts-vertical] Camera source update signal on retained source %p ('%s') "
			     "(same pointer Properties must have edited)",
			     (void *)src, obs_source_get_name(src));
			dock->LogCameraSettingsSnapshot("SETTINGS_AFTER_PROPERTIES_UPDATE", src);
			if (vsp::GetCaptureDeviceKey(src).empty()) {
				blog(LOG_WARNING,
				     "[obs-shorts-vertical] Update signal without device ID yet — waiting");
				return;
			}
			/* Activate with settings Properties just saved — do not wipe them. */
			dock->ForceCaptureDeviceOpen(src, false);
		},
		Qt::QueuedConnection);
}

bool ShortsDock::IsIndependentCapture(obs_source_t *source) const
{
	if (!source)
		return false;
	const char *uuid = obs_source_get_uuid(source);
	if (!uuid || !*uuid)
		return false;
	return independentCaptureUuids.contains(QString::fromUtf8(uuid));
}

bool ShortsDock::SourceUsedInFrontendScenes(obs_source_t *source)
{
	if (!source)
		return false;
	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	bool used = false;
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_scene_t *sc = obs_scene_from_source(scenes.sources.array[i]);
		if (sc && vsp::VerticalSceneHasSource(sc, source)) {
			used = true;
			break;
		}
	}
	obs_frontend_source_list_free(&scenes);
	return used;
}

void ShortsDock::MaybeDestroyPrivateCapture(obs_source_t *source)
{
	if (!source || !IsIndependentCapture(source))
		return;
	if (!vsp::IsVideoCaptureSourceId(obs_source_get_id(source)))
		return;
	if (scene && vsp::VerticalSceneHasSource(scene, source))
		return;
	for (auto it = verticalScenes.constBegin(); it != verticalScenes.constEnd(); ++it) {
		obs_scene_t *s = it.value();
		if (s && vsp::VerticalSceneHasSource(s, source))
			return;
	}
	if (SourceUsedInFrontendScenes(source))
		return;

	const char *uuid = obs_source_get_uuid(source);
	blog(LOG_INFO, "[obs-shorts-vertical] Destroying independent Vertical Shorts camera '%s' (no remaining items)",
	     obs_source_get_name(source));
	if (pendingCameraSource && pendingCameraSource.Get() == source) {
		pendingCameraUpdateSignal.Disconnect();
		pendingCameraSource = nullptr;
	}
	if (uuid && *uuid)
		independentCaptureUuids.remove(QString::fromUtf8(uuid));
	obs_source_remove(source);
}

void ShortsDock::WatchIndependentCaptureInit(obs_source_t *created)
{
	if (!created)
		return;

	pendingCameraUpdateSignal.Disconnect();
	pendingCameraSource = created;
	if (signal_handler_t *sh = obs_source_get_signal_handler(created)) {
		pendingCameraUpdateSignal.Connect(sh, "update", OnPendingCameraUpdate, this);
		blog(LOG_INFO,
		     "[obs-shorts-vertical] Watching retained camera source %p for settings updates "
		     "(Properties must edit THIS pointer — never recreate after OK)",
		     (void *)created);
	}

	OBSSource held = created;
	struct WatchState {
		bool deviceSelected = false;
		bool activateAttempted = false;
		bool safeDefaultsAttempted = false;
		bool warned = false;
		bool failScheduled = false;
		void *retainedPtr = nullptr;
	};
	QSharedPointer<WatchState> state(new WatchState());
	state->retainedPtr = (void *)created;

	/* Poll for device ID / size. Activate at most twice (once raw, once with safe defaults).
	 * Never deactivate→activate on every tick — that prevents DirectShow from staying open. */
	const int delaysMs[] = {250, 500, 1000, 1500, 2000, 3000, 4000, 5000, 6500, 8000, 10000};
	for (int delay : delaysMs) {
		QTimer::singleShot(delay, this, [this, held, state]() {
			if (clearing || !held || state->warned)
				return;

			if (pendingCameraSource && pendingCameraSource.Get() != held.Get()) {
				blog(LOG_ERROR,
				     "[obs-shorts-vertical] CAMERA SOURCE POINTER MISMATCH: pending=%p held=%p "
				     "original=%p — source was replaced after Properties (bug)",
				     (void *)pendingCameraSource.Get(), (void *)held.Get(), state->retainedPtr);
			}
			if (held.Get() != state->retainedPtr) {
				blog(LOG_ERROR,
				     "[obs-shorts-vertical] Retained camera pointer changed — source recreated");
			}

			obs_sceneitem_t *item = nullptr;
			if (scene) {
				struct Find {
					obs_source_t *src;
					obs_sceneitem_t *item;
				} find{held, nullptr};
				obs_scene_enum_items(
					scene,
					[](obs_scene_t *, obs_sceneitem_t *si, void *p) -> bool {
						auto *f = static_cast<Find *>(p);
						if (obs_sceneitem_get_source(si) == f->src) {
							f->item = si;
							return false;
						}
						return true;
					},
					&find);
				item = find.item;
				if (item && !obs_sceneitem_visible(item)) {
					obs_sceneitem_set_visible(item, true);
					blog(LOG_WARNING,
					     "[obs-shorts-vertical] Camera scene item was hidden — forcing visible");
				}
			}

			const std::string key = vsp::GetCaptureDeviceKey(held);
			if (key.empty()) {
				/* Still waiting for the user to pick a device in Properties — no error yet. */
				return;
			}

			LogCameraSourceDiagnostics("init_poll", held, item, false);

			if (!state->deviceSelected) {
				state->deviceSelected = true;
				LogCameraSettingsSnapshot("SETTINGS_AFTER_DEVICE_SELECTED", held);
				blog(LOG_INFO,
				     "[obs-shorts-vertical] Device ID present on retained source %p "
				     "(not recreated) — starting ≤10s initialization window",
				     (void *)held.Get());
			}

			const uint32_t w = obs_source_get_width(held);
			const uint32_t h = obs_source_get_height(held);
			if (w > 0 && h > 0) {
				EnsureCanvasProgramChannel(true);
				EnsurePreviewSceneShowing(true);
				blog(LOG_INFO,
				     "[obs-shorts-vertical] CAMERA INITIALIZED: source=%p '%s' id=%s %ux%u "
				     "active=%d showing=%d (expect live frames)",
				     (void *)held.Get(), obs_source_get_name(held), obs_source_get_id(held), w, h,
				     (int)obs_source_active(held), (int)obs_source_showing(held));
				state->warned = true;
				pendingCameraUpdateSignal.Disconnect();
				pendingCameraSource = nullptr;
				return;
			}

			/* First activation attempt with Properties-saved settings only. */
			if (!state->activateAttempted) {
				state->activateAttempted = true;
				ForceCaptureDeviceOpen(held, false);
			}

			/* Schedule a single 10s failure window from first device sighting. */
			if (!state->failScheduled) {
				state->failScheduled = true;
				QTimer::singleShot(10000, this, [this, held, state]() {
					if (clearing || !held || state->warned)
						return;
					if (obs_source_get_width(held) > 0 && obs_source_get_height(held) > 0)
						return;

					/* One last attempt: device-default negotiation only. */
					if (!state->safeDefaultsAttempted) {
						state->safeDefaultsAttempted = true;
						blog(LOG_WARNING,
						     "[obs-shorts-vertical] Still 0x0 at ~10s — retrying with "
						     "device-default Preferred settings (no forced 1080p/60)");
						ForceCaptureDeviceOpen(held, true);
						QTimer::singleShot(2000, this, [this, held, state]() {
							if (clearing || !held || state->warned)
								return;
							if (obs_source_get_width(held) > 0 &&
							    obs_source_get_height(held) > 0) {
								blog(LOG_INFO,
								     "[obs-shorts-vertical] CAMERA INITIALIZED after "
								     "safe defaults: %ux%u",
								     obs_source_get_width(held),
								     obs_source_get_height(held));
								state->warned = true;
								pendingCameraUpdateSignal.Disconnect();
								pendingCameraSource = nullptr;
								return;
							}
							state->warned = true;
							ReportCameraInitFailure(held);
							pendingCameraUpdateSignal.Disconnect();
							pendingCameraSource = nullptr;
						});
						return;
					}

					state->warned = true;
					ReportCameraInitFailure(held);
					pendingCameraUpdateSignal.Disconnect();
					pendingCameraSource = nullptr;
				});
			}
		});
	}
}

void ShortsDock::CreateIndependentVideoCapture(const std::string &unversionedId, const QString &label)
{
	if (!scene) {
		EnsureDefaultVerticalScene();
		if (!scene) {
			blog(LOG_ERROR, "[obs-shorts-vertical] Camera create aborted: no active Vertical Scene");
			return;
		}
	}

	std::string createId = vsp::ResolveVideoCaptureSourceId();
	if (createId.empty())
		createId = vsp::ResolveLatestInputTypeId(unversionedId.c_str());
	if (createId.empty() || !vsp::IsVideoCaptureSourceId(createId.c_str())) {
		blog(LOG_ERROR,
		     "[obs-shorts-vertical] Camera create FAILED: no supported Video Capture Device id "
		     "(menu_unversioned='%s' resolved='%s')",
		     unversionedId.c_str(), createId.c_str());
		LogCameraSourceDiagnostics("create_id_invalid", nullptr, nullptr, true);
		QMessageBox::warning(this, Translate("AddSource"), Translate("CaptureCreateFailed"));
		return;
	}

	const char *display = obs_source_get_display_name(createId.c_str());
	const uint32_t flags = obs_get_source_output_flags(createId.c_str());
	blog(LOG_INFO,
	     "[obs-shorts-vertical] Creating Vertical Shorts camera: source_id=%s display='%s' "
	     "flags=0x%x (obs_source_create — retained source, not temporary)",
	     createId.c_str(), display ? display : "", flags);

	const QString name = UniqueSourceName(label.isEmpty()
						      ? QString::fromUtf8(display ? display : "Video Capture Device")
						      : label);

	/* Minimal create settings. Leave device unset until Properties.
	 * Prefer win-dshow defaults: do not force custom resolution/FPS/format.
	 * active=true matches OBS defaults so Update() after Properties can QueueActivate;
	 * open is a no-op until video_device_id exists. */
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_bool(settings, "active", true);
	obs_data_set_bool(settings, "deactivate_when_not_showing", false);
	obs_data_set_int(settings, "res_type", 0);        /* Preferred / device default */
	obs_data_set_int(settings, "frame_interval", -1); /* matching / ignored for Preferred */
	obs_data_set_int(settings, "video_format", 0);    /* Any */

	obs_source_t *created =
		obs_source_create(createId.c_str(), name.toUtf8().constData(), settings, nullptr);
	if (!created) {
		blog(LOG_ERROR,
		     "[obs-shorts-vertical] obs_source_create RETURNED NULL for id='%s' name='%s'",
		     createId.c_str(), name.toUtf8().constData());
		LogCameraSourceDiagnostics("obs_source_create_null", nullptr, nullptr, true);
		QMessageBox::warning(this, Translate("AddSource"), Translate("CaptureCreateFailed"));
		return;
	}

	blog(LOG_INFO,
	     "[obs-shorts-vertical] obs_source_create SUCCEEDED: retained_source=%p id=%s name='%s' uuid=%s",
	     (void *)created, obs_source_get_id(created), obs_source_get_name(created),
	     obs_source_get_uuid(created) ? obs_source_get_uuid(created) : "");

	LogCameraSourceDiagnostics("after_create", created, nullptr, false);
	LogCameraSettingsSnapshot("SETTINGS_BEFORE_PROPERTIES", created);
	{
		const char *uuid = obs_source_get_uuid(created);
		if (uuid && *uuid)
			independentCaptureUuids.insert(QString::fromUtf8(uuid));
	}

	obs_sceneitem_t *item = AddSourceToActiveScene(created, true);
	if (!item) {
		blog(LOG_ERROR,
		     "[obs-shorts-vertical] Camera '%s' created but obs_scene_add FAILED",
		     obs_source_get_name(created));
		LogCameraSourceDiagnostics("add_to_scene_failed", created, nullptr, false);
		independentCaptureUuids.remove(QString::fromUtf8(obs_source_get_uuid(created)));
		obs_source_release(created);
		QMessageBox::warning(this, Translate("AddSource"), Translate("CaptureCreateFailed"));
		return;
	}

	obs_source_set_enabled(created, true);
	obs_sceneitem_set_visible(item, true);

	LogCameraSourceDiagnostics("after_add_to_scene", created, item, false);
	EnsureCanvasProgramChannel(true);
	EnsurePreviewSceneShowing(true);

	/* Retain BEFORE Properties so the configured source is never a temporary. */
	WatchIndependentCaptureInit(created);

	if (obs_source_configurable(created)) {
		blog(LOG_INFO,
		     "[obs-shorts-vertical] Opening OBS Properties for retained camera source %p ('%s') "
		     "— OK must write video_device_id onto THIS source",
		     (void *)created, obs_source_get_name(created));
		obs_frontend_open_source_properties(created);
	} else {
		blog(LOG_ERROR, "[obs-shorts-vertical] Camera source is not configurable");
		QMessageBox::warning(this, Translate("AddSource"), Translate("CaptureCreateFailed"));
	}

	/* Scene item + pendingCameraSource hold refs; release create ref only. */
	obs_source_release(created);
	EmitSourceUiChanged();
}

bool ShortsDock::SourceIsVisual(obs_source_t *source)
{
	if (!source)
		return false;
	const uint32_t flags = obs_source_get_output_flags(source);
	return (flags & OBS_SOURCE_VIDEO) != 0;
}

uint32_t ShortsDock::FillBoundsAlignment(VerticalFillPosition pos)
{
	switch (pos) {
	case VerticalFillPosition::Left:
		return OBS_ALIGN_LEFT | OBS_ALIGN_CENTER;
	case VerticalFillPosition::Right:
		return OBS_ALIGN_RIGHT | OBS_ALIGN_CENTER;
	case VerticalFillPosition::Center:
	default:
		return OBS_ALIGN_CENTER;
	}
}

void ShortsDock::StoreFitMode(obs_sceneitem_t *item, VerticalFitMode mode)
{
	if (!item)
		return;
	/* Borrowed pointer — do not release. */
	obs_data_t *priv = obs_sceneitem_get_private_settings(item);
	if (!priv)
		return;
	const char *value = "fill";
	switch (mode) {
	case VerticalFitMode::FitInside:
		value = "fit";
		break;
	case VerticalFitMode::Original:
		value = "original";
		break;
	case VerticalFitMode::Stretch:
		value = "stretch";
		break;
	case VerticalFitMode::Fill:
	default:
		value = "fill";
		break;
	}
	obs_data_set_string(priv, "vsp_fit_mode", value);
}

void ShortsDock::StoreFillPosition(obs_sceneitem_t *item, VerticalFillPosition pos)
{
	if (!item)
		return;
	obs_data_t *priv = obs_sceneitem_get_private_settings(item);
	if (!priv)
		return;
	const char *value = "center";
	switch (pos) {
	case VerticalFillPosition::Left:
		value = "left";
		break;
	case VerticalFillPosition::Right:
		value = "right";
		break;
	case VerticalFillPosition::Center:
	default:
		value = "center";
		break;
	}
	obs_data_set_string(priv, "vsp_fill_pos", value);
}

VerticalFitMode ShortsDock::LoadFitMode(obs_sceneitem_t *item, VerticalFitMode fallback)
{
	if (!item)
		return fallback;
	obs_data_t *priv = obs_sceneitem_get_private_settings(item);
	if (!priv)
		return fallback;
	const char *value = obs_data_get_string(priv, "vsp_fit_mode");
	if (!value || !*value)
		return fallback;
	if (strcmp(value, "fit") == 0)
		return VerticalFitMode::FitInside;
	if (strcmp(value, "original") == 0)
		return VerticalFitMode::Original;
	if (strcmp(value, "stretch") == 0)
		return VerticalFitMode::Stretch;
	if (strcmp(value, "fill") == 0)
		return VerticalFitMode::Fill;
	return fallback;
}

VerticalFillPosition ShortsDock::LoadFillPosition(obs_sceneitem_t *item)
{
	if (!item)
		return VerticalFillPosition::Center;
	obs_data_t *priv = obs_sceneitem_get_private_settings(item);
	if (!priv)
		return VerticalFillPosition::Center;
	const char *value = obs_data_get_string(priv, "vsp_fill_pos");
	if (value && strcmp(value, "left") == 0)
		return VerticalFillPosition::Left;
	if (value && strcmp(value, "right") == 0)
		return VerticalFillPosition::Right;
	return VerticalFillPosition::Center;
}

void ShortsDock::ApplyVerticalFitMode(obs_sceneitem_t *item, VerticalFitMode mode, bool persist)
{
	if (!item)
		return;

	obs_source_t *source = obs_sceneitem_get_source(item);
	const uint32_t sw = source ? obs_source_get_width(source) : 0;
	const uint32_t sh = source ? obs_source_get_height(source) : 0;
	/* ALWAYS use the active Vertical Shorts canvas — never main OBS base/output size. */
	const float canvasW = float(verticalWidth > 0 ? verticalWidth : 1080);
	const float canvasH = float(verticalHeight > 0 ? verticalHeight : 1920);
	const VerticalFillPosition fillPos = LoadFillPosition(item);

	if (persist)
		StoreFitMode(item, mode);

	/* Fit/Fill start from a clean crop so the whole source participates. */
	if (mode == VerticalFitMode::Fill || mode == VerticalFitMode::FitInside || mode == VerticalFitMode::Stretch) {
		obs_sceneitem_crop zeroCrop = {0, 0, 0, 0};
		obs_sceneitem_set_crop(item, &zeroCrop);
	}

	obs_transform_info info{};
	obs_sceneitem_get_info2(item, &info);
	info.rot = 0.0f;

	/* Prefer explicit uniform scale + center (same scale X/Y). This matches:
	 *   Fit  = min(canvasW/srcW, canvasH/srcH)
	 *   Fill = max(canvasW/srcW, canvasH/srcH)
	 * and avoids silent no-ops when bounds state is corrupt. */
	if ((mode == VerticalFitMode::FitInside || mode == VerticalFitMode::Fill) && sw > 0 && sh > 0 &&
	    canvasW > 0.0f && canvasH > 0.0f) {
		const float sx = canvasW / float(sw);
		const float sy = canvasH / float(sh);
		const float scale = (mode == VerticalFitMode::FitInside) ? std::min(sx, sy) : std::max(sx, sy);
		const float scaledW = float(sw) * scale;
		const float scaledH = float(sh) * scale;

		float posX = (canvasW - scaledW) * 0.5f;
		float posY = (canvasH - scaledH) * 0.5f;
		/* Optional horizontal bias for Fill Position (Left/Center/Right). */
		if (mode == VerticalFitMode::Fill) {
			switch (fillPos) {
			case VerticalFillPosition::Left:
				posX = 0.0f;
				break;
			case VerticalFillPosition::Right:
				posX = canvasW - scaledW;
				break;
			case VerticalFillPosition::Center:
			default:
				break;
			}
		}

		vec2_set(&info.scale, scale, scale);
		vec2_set(&info.pos, posX, posY);
		info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
		vec2_set(&info.bounds, 0.0f, 0.0f);
		info.bounds_type = OBS_BOUNDS_NONE;
		info.bounds_alignment = OBS_ALIGN_CENTER;
		info.crop_to_bounds = false;
		obs_sceneitem_set_info2(item, &info);

		blog(LOG_INFO,
		     "[obs-shorts-vertical] %s '%s': source=%ux%u canvas=%ux%u scale=%.6f "
		     "scaled=%.1fx%.1f pos=(%.1f,%.1f) fill_pos=%d",
		     mode == VerticalFitMode::FitInside ? "Fit to Vertical Canvas" : "Fill Vertical Canvas",
		     source ? obs_source_get_name(source) : "?", sw, sh, (uint32_t)canvasW, (uint32_t)canvasH, scale,
		     scaledW, scaledH, posX, posY, (int)fillPos);
		return;
	}

	if ((mode == VerticalFitMode::FitInside || mode == VerticalFitMode::Fill) && (sw == 0 || sh == 0)) {
		blog(LOG_WARNING,
		     "[obs-shorts-vertical] %s deferred: source '%s' has no dimensions yet (0x0) — will retry",
		     mode == VerticalFitMode::FitInside ? "Fit" : "Fill",
		     source ? obs_source_get_name(source) : "?");
		ScheduleDeferredFit(item, 40);
		return;
	}

	switch (mode) {
	case VerticalFitMode::Original:
		vec2_set(&info.scale, 1.0f, 1.0f);
		vec2_set(&info.bounds, 0.0f, 0.0f);
		info.bounds_type = OBS_BOUNDS_NONE;
		info.bounds_alignment = OBS_ALIGN_CENTER;
		info.crop_to_bounds = false;
		info.alignment = OBS_ALIGN_CENTER;
		vec2_set(&info.pos, canvasW * 0.5f, canvasH * 0.5f);
		break;
	case VerticalFitMode::Stretch:
		/* Manual-only distorting fill. Never used as automatic default. */
		vec2_set(&info.scale, 1.0f, 1.0f);
		vec2_set(&info.pos, 0.0f, 0.0f);
		info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
		vec2_set(&info.bounds, canvasW, canvasH);
		info.bounds_type = OBS_BOUNDS_STRETCH;
		info.bounds_alignment = OBS_ALIGN_CENTER;
		info.crop_to_bounds = false;
		break;
	case VerticalFitMode::Fill:
	case VerticalFitMode::FitInside:
	default:
		/* Fallback bounds path if sizes somehow invalid after checks above. */
		vec2_set(&info.scale, 1.0f, 1.0f);
		vec2_set(&info.pos, 0.0f, 0.0f);
		info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
		vec2_set(&info.bounds, canvasW, canvasH);
		info.bounds_type =
			(mode == VerticalFitMode::Fill) ? OBS_BOUNDS_SCALE_OUTER : OBS_BOUNDS_SCALE_INNER;
		info.bounds_alignment = OBS_ALIGN_CENTER;
		info.crop_to_bounds = (mode == VerticalFitMode::Fill);
		break;
	}

	obs_sceneitem_set_info2(item, &info);

	blog(LOG_INFO,
	     "[obs-shorts-vertical] Applied fit mode=%d fill_pos=%d to '%s' source=%ux%u canvas=%ux%u "
	     "bounds_type=%d",
	     (int)mode, (int)fillPos, source ? obs_source_get_name(source) : "?", sw, sh, (uint32_t)canvasW,
	     (uint32_t)canvasH, (int)info.bounds_type);
}

void ShortsDock::FitSceneItemToCanvas(obs_sceneitem_t *item)
{
	/* Default for new visual sources: Fit to Vertical Canvas (entire image visible). */
	ApplyVerticalFitMode(item, VerticalFitMode::FitInside, true);
}

void ShortsDock::ReapplyStoredFitModes()
{
	if (!scene)
		return;

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
			auto *self = static_cast<ShortsDock *>(param);
			obs_source_t *source = obs_sceneitem_get_source(item);
			if (!SourceIsVisual(source))
				return true;
			/* Preserve stored mode (Fill/Fit/Original/Stretch) against the new canvas. */
			const VerticalFitMode mode = LoadFitMode(item, VerticalFitMode::FitInside);
			self->ApplyVerticalFitMode(item, mode, false);
			return true;
		},
		this);
}

void ShortsDock::ScheduleDeferredFit(obs_sceneitem_t *item, int attemptsLeft)
{
	if (!item || attemptsLeft <= 0)
		return;

	/* Keep a strong ref across the timer — source size may appear after activate. */
	OBSSceneItem held = item;
	QTimer::singleShot(200, this, [this, held, attemptsLeft]() {
		if (clearing || !held)
			return;
		obs_source_t *src = obs_sceneitem_get_source(held);
		const uint32_t sw = src ? obs_source_get_width(src) : 0;
		const uint32_t sh = src ? obs_source_get_height(src) : 0;
		if (sw > 0 && sh > 0) {
			const VerticalFitMode mode = LoadFitMode(held, VerticalFitMode::FitInside);
			ApplyVerticalFitMode(held, mode, true);
			ValidateItemTransform(held);
			/* Rebind after size appears so ACTIVATE walks the now-sized VCD. */
			EnsureCanvasProgramChannel(true);
			LogRenderPipeline("DeferredFit");
			emit verticalTransformChanged();
			return;
		}
		ScheduleDeferredFit(held, attemptsLeft - 1);
	});
}

obs_sceneitem_t *ShortsDock::AddSourceToActiveScene(obs_source_t *source, bool fitIfSized)
{
	if (!scene || !source)
		return nullptr;

	/* Add to the active Vertical Scene. For independent private cameras this is
	 * Vertical Shorts ownership; for explicit Add Existing / Share this is a
	 * reference with an independent vertical scene-item transform. */
	obs_sceneitem_t *item = obs_scene_add(scene, source);
	if (!item) {
		blog(LOG_WARNING, "[obs-shorts-vertical] obs_scene_add failed for '%s'", obs_source_get_name(source));
		return nullptr;
	}

	obs_source_set_enabled(source, true);
	obs_sceneitem_set_visible(item, true);
	obs_sceneitem_set_locked(item, false);
	obs_scene_enum_items(scene, ClearSelection, nullptr);
	obs_sceneitem_select(item, true);

	const uint32_t sw = obs_source_get_width(source);
	const uint32_t sh = obs_source_get_height(source);
	const bool visual = SourceIsVisual(source);
	if (fitIfSized && visual) {
		StoreFillPosition(item, VerticalFillPosition::Center);
		StoreFitMode(item, VerticalFitMode::FitInside);
		if (sw > 0 && sh > 0) {
			ApplyVerticalFitMode(item, VerticalFitMode::FitInside, true);
			ValidateItemTransform(item);
		} else {
			ScheduleDeferredFit(item, 40); /* ~8s for devices that start late */
		}
	}

	/* PROGRAM channel must reference the active vertical scene so ACTIVATE
	 * walks the tree and capture devices receive activate_refs.
	 * Force-rebind so a newly added VCD is included in the activation walk. */
	EnsureCanvasProgramChannel(true);

	/* Second tick: some capture devices only report size after activate_refs. */
	if (fitIfSized && visual && (obs_source_get_width(source) == 0 || obs_source_get_height(source) == 0))
		ScheduleDeferredFit(item, 40);

	obs_source_t *channel0 = canvas ? obs_canvas_get_channel(canvas, 0) : nullptr;
	blog(LOG_INFO,
	     "[obs-shorts-vertical] Added '%s' (%s) source=%p to vertical scene: item_visible=%d "
	     "source_active=%d source_showing=%d source_enabled=%d size=%ux%u canvas=%ux%u fit=Fit "
	     "channel0=%s",
	     obs_source_get_name(source), obs_source_get_id(source), (void *)source, (int)obs_sceneitem_visible(item),
	     (int)obs_source_active(source), (int)obs_source_showing(source), (int)obs_source_enabled(source),
	     obs_source_get_width(source), obs_source_get_height(source), verticalWidth, verticalHeight,
	     channel0 ? obs_source_get_name(channel0) : "(null)");
	if (channel0)
		obs_source_release(channel0);
	LogRenderPipeline("AddSourceToActiveScene");

	return item;
}

void ShortsDock::RequestRemoveSource()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	if (QMessageBox::question(this, Translate("RemoveSource"), Translate("ConfirmRemoveSource")) != QMessageBox::Yes)
		return;
	for (obs_sceneitem_t *item : selected) {
		obs_source_t *src = obs_sceneitem_get_source(item);
		OBSSource held = src; /* keep alive across item remove */
		obs_sceneitem_remove(item);
		MaybeDestroyPrivateCapture(held);
	}
	EmitSourceUiChanged();
}

void ShortsDock::RequestToggleSourceVisible()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected)
		obs_sceneitem_set_visible(item, !obs_sceneitem_visible(item));
	EmitSourceUiChanged();
}

void ShortsDock::RequestToggleSourceLock()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected)
		obs_sceneitem_set_locked(item, !obs_sceneitem_locked(item));
	EmitSourceUiChanged();
}

void ShortsDock::RequestToggleSourceVisibleById(qint64 itemId)
{
	if (!scene)
		return;
	obs_sceneitem_t *item = FindItemById(scene, itemId);
	if (!item)
		return;
	obs_sceneitem_set_visible(item, !obs_sceneitem_visible(item));
	EmitSourceUiChanged();
}

void ShortsDock::RequestToggleSourceLockById(qint64 itemId)
{
	if (!scene)
		return;
	obs_sceneitem_t *item = FindItemById(scene, itemId);
	if (!item)
		return;
	obs_sceneitem_set_locked(item, !obs_sceneitem_locked(item));
	EmitSourceUiChanged();
}

void ShortsDock::RequestSourceProperties()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_source_t *source = obs_sceneitem_get_source(selected.front());
	if (source && obs_source_configurable(source))
		obs_frontend_open_source_properties(source);
}

void ShortsDock::RequestSourceFilters()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_source_t *source = obs_sceneitem_get_source(selected.front());
	if (source)
		obs_frontend_open_source_filters(source);
}

void ShortsDock::RequestRenameSource()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_source_t *source = obs_sceneitem_get_source(selected.front());
	if (!source)
		return;
	bool ok = false;
	const QString current = QString::fromUtf8(obs_source_get_name(source));
	QString name = QInputDialog::getText(this, Translate("RenameSource"), Translate("RenameSourcePrompt"),
					     QLineEdit::Normal, current, &ok);
	if (!ok)
		return;
	name = name.trimmed();
	if (name.isEmpty() || name == current)
		return;
	OBSSourceAutoRelease clash = obs_get_source_by_name(name.toUtf8().constData());
	if (clash && clash.Get() != source) {
		QMessageBox::warning(this, Translate("RenameSource"), Translate("RenameSourceExists"));
		return;
	}
	/* Renames the shared OBS source (same as main OBS). */
	obs_source_set_name(source, name.toUtf8().constData());
	EmitSourceUiChanged();
}

void ShortsDock::RequestDuplicateSource()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_source_t *source = obs_sceneitem_get_source(selected.front());
	if (!source)
		return;

	/* Capture sources: never open a second hardware session via duplicate. */
	if (vsp::IsVideoCaptureSourceId(obs_source_get_id(source))) {
		const auto reply = QMessageBox::question(this, Translate("DuplicateSource"),
							 Translate("DuplicateCaptureShareHint"),
							 QMessageBox::Yes | QMessageBox::No);
		if (reply != QMessageBox::Yes)
			return;
		obs_sceneitem_t *orig = selected.front();
		obs_sceneitem_t *item = AddSourceToActiveScene(source, false);
		if (item) {
			obs_transform_info info{};
			obs_sceneitem_get_info2(orig, &info);
			obs_sceneitem_set_info2(item, &info);
			obs_sceneitem_crop crop{};
			obs_sceneitem_get_crop(orig, &crop);
			obs_sceneitem_set_crop(item, &crop);
		}
		EmitSourceUiChanged();
		return;
	}

	const QString base = QString::fromUtf8(obs_source_get_name(source));
	const QString name = UniqueSourceName(base);
	obs_source_t *dup = obs_source_duplicate(source, name.toUtf8().constData(), false);
	if (!dup) {
		QMessageBox::warning(this, Translate("DuplicateSource"), Translate("CreateSourceFailed"));
		return;
	}
	obs_sceneitem_t *orig = selected.front();
	obs_sceneitem_t *item = AddSourceToActiveScene(dup, false);
	if (item) {
		obs_transform_info info{};
		obs_sceneitem_get_info2(orig, &info);
		obs_sceneitem_set_info2(item, &info);
		obs_sceneitem_crop crop{};
		obs_sceneitem_get_crop(orig, &crop);
		obs_sceneitem_set_crop(item, &crop);
	}
	obs_source_release(dup);
	EmitSourceUiChanged();
}

void ShortsDock::RequestCopySource()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_source_t *source = obs_sceneitem_get_source(selected.front());
	if (!source)
		return;
	g_sourceClipboard.id = obs_source_get_id(source) ? obs_source_get_id(source) : "";
	OBSDataAutoRelease cur = obs_source_get_settings(source);
	obs_data_t *copy = obs_data_create();
	if (cur)
		obs_data_apply(copy, cur);
	g_sourceClipboard.settings = copy; /* OBSData takes ownership */
	g_sourceClipboard.hotkeys = obs_hotkeys_save_source(source);
	g_sourceClipboard.valid = !g_sourceClipboard.id.empty();
}

void ShortsDock::RequestPasteSource()
{
	if (!scene || !g_sourceClipboard.valid)
		return;
	OBSDataAutoRelease settings = obs_data_create();
	if (g_sourceClipboard.settings)
		obs_data_apply(settings, g_sourceClipboard.settings);

	const char *display = obs_source_get_display_name(g_sourceClipboard.id.c_str());
	const QString base = display && *display ? QString::fromUtf8(display) : QStringLiteral("Source");
	const QString name = UniqueSourceName(base);

	const std::string createId = vsp::ResolveLatestInputTypeId(g_sourceClipboard.id.c_str());
	const bool isCapture = vsp::IsVideoCaptureSourceId(createId.c_str()) ||
			       vsp::IsVideoCaptureSourceId(g_sourceClipboard.id.c_str());

	if (isCapture && settings)
		obs_data_set_bool(settings, "active", true);

	obs_source_t *created =
		obs_source_create(createId.empty() ? g_sourceClipboard.id.c_str() : createId.c_str(),
				  name.toUtf8().constData(), settings, g_sourceClipboard.hotkeys);
	if (!created) {
		blog(LOG_ERROR, "[obs-shorts-vertical] Paste obs_source_create RETURNED NULL id='%s'",
		     createId.c_str());
		QMessageBox::warning(this, Translate("PasteSource"), Translate("CreateSourceFailed"));
		return;
	}
	if (isCapture) {
		const char *uuid = obs_source_get_uuid(created);
		if (uuid && *uuid)
			independentCaptureUuids.insert(QString::fromUtf8(uuid));
	}
	AddSourceToActiveScene(created, true);
	if (isCapture) {
		WatchIndependentCaptureInit(created);
		/* Paste already carries device settings — open now with safe defaults. */
		ForceCaptureDeviceOpen(created, true);
	}
	obs_source_release(created);
	EmitSourceUiChanged();
}

void ShortsDock::RequestCopyTransform()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_sceneitem_get_info2(selected.front(), &g_transformClipboard.info);
	obs_sceneitem_get_crop(selected.front(), &g_transformClipboard.crop);
	g_transformClipboard.valid = true;
}

void ShortsDock::RequestPasteTransform()
{
	if (!scene || !g_transformClipboard.valid)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected) {
		if (obs_sceneitem_locked(item))
			continue;
		/* Applies only to the Vertical Shorts scene item. */
		obs_sceneitem_set_info2(item, &g_transformClipboard.info);
		obs_sceneitem_set_crop(item, &g_transformClipboard.crop);
	}
	emit verticalTransformChanged();
	EmitSourceUiChanged();
}

void ShortsDock::RequestSourceMoveUp()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_sceneitem_set_order(selected.front(), OBS_ORDER_MOVE_UP);
	EmitSourceUiChanged();
}

void ShortsDock::RequestSourceMoveDown()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_sceneitem_set_order(selected.front(), OBS_ORDER_MOVE_DOWN);
	EmitSourceUiChanged();
}

void ShortsDock::RequestSourceMoveTop()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_sceneitem_set_order(selected.front(), OBS_ORDER_MOVE_TOP);
	EmitSourceUiChanged();
}

void ShortsDock::RequestSourceMoveBottom()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_sceneitem_set_order(selected.front(), OBS_ORDER_MOVE_BOTTOM);
	EmitSourceUiChanged();
}

void ShortsDock::RequestReorderSources(const QList<qint64> &topToBottomIds)
{
	if (!scene || topToBottomIds.isEmpty())
		return;

	std::vector<obs_sceneitem_t *> order;
	order.reserve((size_t)topToBottomIds.size());
	/* API wants bottom→top. */
	for (int i = topToBottomIds.size() - 1; i >= 0; --i) {
		obs_sceneitem_t *item = FindItemById(scene, topToBottomIds[i]);
		if (item)
			order.push_back(item);
	}
	if (order.empty())
		return;
	obs_scene_reorder_items(scene, order.data(), order.size());
	EmitSourceUiChanged();
}

bool ShortsDock::HasSelectedVerticalSource() const
{
	if (!scene)
		return false;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	return !selected.empty();
}

bool ShortsDock::HasSourceClipboard() const
{
	return g_sourceClipboard.valid;
}

bool ShortsDock::HasTransformClipboard() const
{
	return g_transformClipboard.valid;
}

void ShortsDock::RequestFitToScreen()
{
	/* Legacy name — now means Fit Inside (show entire source). */
	RequestFitInsideVerticalCanvas();
}

void ShortsDock::RequestFillVerticalCanvas()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_sceneitem_t *item = selected.front();
	if (obs_sceneitem_locked(item))
		return;
	obs_sceneitem_set_visible(item, true);
	ApplyVerticalFitMode(item, VerticalFitMode::Fill, true);
	ValidateItemTransform(item);
	EnsureCanvasProgramChannel(true);
	emit verticalTransformChanged();
	EmitSourceUiChanged();
}

void ShortsDock::RequestFitInsideVerticalCanvas()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_sceneitem_t *item = selected.front();
	if (obs_sceneitem_locked(item))
		return;
	obs_sceneitem_set_visible(item, true);
	ApplyVerticalFitMode(item, VerticalFitMode::FitInside, true);
	ValidateItemTransform(item);
	EnsureCanvasProgramChannel(true);
	emit verticalTransformChanged();
	EmitSourceUiChanged();
}

void ShortsDock::RequestOriginalSize()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	ApplyVerticalFitMode(selected.front(), VerticalFitMode::Original, true);
	emit verticalTransformChanged();
}

void ShortsDock::RequestStretchToScreen()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	ApplyVerticalFitMode(selected.front(), VerticalFitMode::Stretch, true);
	emit verticalTransformChanged();
}

void ShortsDock::RequestSetFillPosition(VerticalFillPosition pos)
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_sceneitem_t *item = selected.front();
	StoreFillPosition(item, pos);
	/* Fill Position applies to Fill mode (cover + crop). Selecting a side enables Fill. */
	ApplyVerticalFitMode(item, VerticalFitMode::Fill, true);
	emit verticalTransformChanged();
}

VerticalFitMode ShortsDock::SelectedFitMode() const
{
	if (!scene)
		return VerticalFitMode::FitInside;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return VerticalFitMode::FitInside;
	return LoadFitMode(selected.front(), VerticalFitMode::FitInside);
}

VerticalFillPosition ShortsDock::SelectedFillPosition() const
{
	if (!scene)
		return VerticalFillPosition::Center;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return VerticalFillPosition::Center;
	return LoadFillPosition(selected.front());
}

void ShortsDock::AppendTransformFitMenu(QMenu *transformMenu)
{
	if (!transformMenu)
		return;

	/* Primary canvas sizing commands — Fit first (most important). */
	transformMenu->addAction(Translate("FitToVerticalCanvas"), this, &ShortsDock::RequestFitInsideVerticalCanvas);
	transformMenu->addAction(Translate("FillVerticalCanvas"), this, &ShortsDock::RequestFillVerticalCanvas);
	transformMenu->addAction(Translate("CenterOnVerticalCanvas"), this, &ShortsDock::RequestCenterToScreen);
}

void ShortsDock::RequestCenterToScreen()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_sceneitem_t *item = selected.front();
	if (obs_sceneitem_locked(item))
		return;

	/* Center using Vertical Shorts canvas dims + current transformed item size. */
	const float canvasW = float(verticalWidth > 0 ? verticalWidth : 1080);
	const float canvasH = float(verticalHeight > 0 ? verticalHeight : 1920);

	obs_transform_info info{};
	obs_sceneitem_get_info2(item, &info);
	/* Normalize to top-left alignment so pos math is unambiguous. */
	info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	obs_sceneitem_set_info2(item, &info);

	vec2 size = GetItemSize(item);
	vec2 pos;
	vec2_set(&pos, (canvasW - size.x) * 0.5f, (canvasH - size.y) * 0.5f);
	if (!std::isfinite(pos.x) || !std::isfinite(pos.y))
		return;
	obs_sceneitem_set_pos(item, &pos);

	blog(LOG_INFO,
	     "[obs-shorts-vertical] Center on Vertical Canvas: canvas=%.0fx%.0f item=%.1fx%.1f pos=(%.1f,%.1f)",
	     canvasW, canvasH, size.x, size.y, pos.x, pos.y);
	emit verticalTransformChanged();
	EmitSourceUiChanged();
}

void ShortsDock::RequestCenterHorizontally()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_sceneitem_t *item = selected.front();
	if (obs_sceneitem_locked(item))
		return;
	vec2 size = GetItemSize(item);
	vec2 pos;
	obs_sceneitem_get_pos(item, &pos);
	pos.x = (float(verticalWidth) - size.x) * 0.5f;
	obs_sceneitem_set_pos(item, &pos);
	emit verticalTransformChanged();
}

void ShortsDock::RequestCenterVertically()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_sceneitem_t *item = selected.front();
	if (obs_sceneitem_locked(item))
		return;
	vec2 size = GetItemSize(item);
	vec2 pos;
	obs_sceneitem_get_pos(item, &pos);
	pos.y = (float(verticalHeight) - size.y) * 0.5f;
	obs_sceneitem_set_pos(item, &pos);
	emit verticalTransformChanged();
}

void ShortsDock::RequestRotateDegrees(float delta)
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected) {
		if (obs_sceneitem_locked(item))
			continue;
		obs_sceneitem_set_rot(item, obs_sceneitem_get_rot(item) + delta);
	}
	emit verticalTransformChanged();
}

void ShortsDock::RequestFlipHorizontal()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected) {
		if (obs_sceneitem_locked(item))
			continue;
		vec2 scale;
		obs_sceneitem_get_scale(item, &scale);
		scale.x = -scale.x;
		obs_sceneitem_set_scale(item, &scale);
	}
	emit verticalTransformChanged();
}

void ShortsDock::RequestFlipVertical()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected) {
		if (obs_sceneitem_locked(item))
			continue;
		vec2 scale;
		obs_sceneitem_get_scale(item, &scale);
		scale.y = -scale.y;
		obs_sceneitem_set_scale(item, &scale);
	}
	emit verticalTransformChanged();
}

void ShortsDock::RequestResetTransform()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	/* Reset to native size at top-left; remember as Original for canvas preset changes. */
	obs_sceneitem_t *item = selected.front();
	StoreFitMode(item, VerticalFitMode::Original);
	StoreFillPosition(item, VerticalFillPosition::Center);
	obs_transform_info info{};
	obs_sceneitem_get_info2(item, &info);
	vec2_set(&info.pos, 0.0f, 0.0f);
	vec2_set(&info.scale, 1.0f, 1.0f);
	info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	info.rot = 0.0f;
	vec2_set(&info.bounds, 0.0f, 0.0f);
	info.bounds_type = OBS_BOUNDS_NONE;
	info.bounds_alignment = OBS_ALIGN_CENTER;
	info.crop_to_bounds = false;
	obs_sceneitem_set_info2(item, &info);
	obs_sceneitem_crop crop = {0, 0, 0, 0};
	obs_sceneitem_set_crop(item, &crop);
	emit verticalTransformChanged();
}

void ShortsDock::RequestEditTransform()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;

	obs_sceneitem_t *item = selected.front();
	OBSSceneItem held = item;
	obs_source_t *source = obs_sceneitem_get_source(item);
	const QString title =
		QString::fromUtf8(Translate("EditTransformTitle")).arg(QString::fromUtf8(obs_source_get_name(source)));

	auto *dlg = new QDialog(this);
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setWindowTitle(title);
	dlg->setWindowFlags(dlg->windowFlags() & ~Qt::WindowContextHelpButtonHint);

	auto *form = new QFormLayout();
	auto *posX = new QDoubleSpinBox(dlg);
	auto *posY = new QDoubleSpinBox(dlg);
	auto *rot = new QDoubleSpinBox(dlg);
	auto *sizeX = new QDoubleSpinBox(dlg);
	auto *sizeY = new QDoubleSpinBox(dlg);
	auto *align = new QComboBox(dlg);
	auto *boundsType = new QComboBox(dlg);
	auto *boundsAlign = new QComboBox(dlg);
	auto *boundsW = new QDoubleSpinBox(dlg);
	auto *boundsH = new QDoubleSpinBox(dlg);
	auto *cropL = new QSpinBox(dlg);
	auto *cropR = new QSpinBox(dlg);
	auto *cropT = new QSpinBox(dlg);
	auto *cropB = new QSpinBox(dlg);

	for (auto *s : {posX, posY, rot, sizeX, sizeY, boundsW, boundsH}) {
		s->setRange(-100000.0, 100000.0);
		s->setDecimals(2);
	}
	rot->setRange(-3600.0, 3600.0);
	for (auto *s : {cropL, cropR, cropT, cropB})
		s->setRange(0, 100000);

	const QStringList alignLabels = {Translate("AlignTopLeft"),    Translate("AlignTop"),
					 Translate("AlignTopRight"),   Translate("AlignCenterLeft"),
					 Translate("AlignCenter"),     Translate("AlignCenterRight"),
					 Translate("AlignBottomLeft"), Translate("AlignBottom"),
					 Translate("AlignBottomRight")};
	align->addItems(alignLabels);
	boundsAlign->addItems(alignLabels);
	boundsType->addItem(Translate("BoundsNone"), (int)OBS_BOUNDS_NONE);
	boundsType->addItem(Translate("BoundsStretch"), (int)OBS_BOUNDS_STRETCH);
	boundsType->addItem(Translate("BoundsScaleInner"), (int)OBS_BOUNDS_SCALE_INNER);
	boundsType->addItem(Translate("BoundsScaleOuter"), (int)OBS_BOUNDS_SCALE_OUTER);
	boundsType->addItem(Translate("BoundsScaleToWidth"), (int)OBS_BOUNDS_SCALE_TO_WIDTH);
	boundsType->addItem(Translate("BoundsScaleToHeight"), (int)OBS_BOUNDS_SCALE_TO_HEIGHT);
	boundsType->addItem(Translate("BoundsMaxOnly"), (int)OBS_BOUNDS_MAX_ONLY);

	form->addRow(Translate("PositionX"), posX);
	form->addRow(Translate("PositionY"), posY);
	form->addRow(Translate("Rotation"), rot);
	form->addRow(Translate("ScaleWidth"), sizeX);
	form->addRow(Translate("ScaleHeight"), sizeY);
	form->addRow(Translate("Alignment"), align);
	form->addRow(Translate("BoundingBoxType"), boundsType);
	form->addRow(Translate("BoundingBoxAlignment"), boundsAlign);
	form->addRow(Translate("BoundingBoxWidth"), boundsW);
	form->addRow(Translate("BoundingBoxHeight"), boundsH);
	form->addRow(Translate("CropLeft"), cropL);
	form->addRow(Translate("CropRight"), cropR);
	form->addRow(Translate("CropTop"), cropT);
	form->addRow(Translate("CropBottom"), cropB);

	auto applyFromControls = [=]() {
		if (!held || obs_sceneitem_locked(held))
			return;
		obs_source_t *src = obs_sceneitem_get_source(held);
		const uint32_t cx = std::max(1u, obs_source_get_width(src));
		const uint32_t cy = std::max(1u, obs_source_get_height(src));

		obs_transform_info info{};
		obs_sceneitem_get_info2(held, &info);
		vec2_set(&info.pos, (float)posX->value(), (float)posY->value());
		info.rot = (float)rot->value();
		vec2_set(&info.scale, float(sizeX->value() / double(cx)), float(sizeY->value() / double(cy)));
		info.alignment = kAlignIndexTable[std::clamp(align->currentIndex(), 0, 8)];
		info.bounds_type = (obs_bounds_type)boundsType->currentData().toInt();
		info.bounds_alignment = kAlignIndexTable[std::clamp(boundsAlign->currentIndex(), 0, 8)];
		vec2_set(&info.bounds, (float)boundsW->value(), (float)boundsH->value());
		obs_sceneitem_set_info2(held, &info);

		obs_sceneitem_crop crop{};
		crop.left = (uint32_t)cropL->value();
		crop.right = (uint32_t)cropR->value();
		crop.top = (uint32_t)cropT->value();
		crop.bottom = (uint32_t)cropB->value();
		obs_sceneitem_set_crop(held, &crop);
		emit this->verticalTransformChanged();
	};

	auto refreshControls = [=]() {
		if (!held)
			return;
		obs_transform_info info{};
		obs_sceneitem_crop crop{};
		obs_sceneitem_get_info2(held, &info);
		obs_sceneitem_get_crop(held, &crop);
		obs_source_t *src = obs_sceneitem_get_source(held);
		const double cx = double(std::max(1u, obs_source_get_width(src)));
		const double cy = double(std::max(1u, obs_source_get_height(src)));

		const QSignalBlocker b1(posX), b2(posY), b3(rot), b4(sizeX), b5(sizeY), b6(align), b7(boundsType),
			b8(boundsAlign), b9(boundsW), b10(boundsH), b11(cropL), b12(cropR), b13(cropT), b14(cropB);
		posX->setValue(info.pos.x);
		posY->setValue(info.pos.y);
		rot->setValue(info.rot);
		sizeX->setValue(info.scale.x * cx);
		sizeY->setValue(info.scale.y * cy);
		align->setCurrentIndex(AlignToIndex(info.alignment));
		const int bt = boundsType->findData((int)info.bounds_type);
		boundsType->setCurrentIndex(bt >= 0 ? bt : 0);
		boundsAlign->setCurrentIndex(AlignToIndex(info.bounds_alignment));
		boundsW->setValue(info.bounds.x);
		boundsH->setValue(info.bounds.y);
		cropL->setValue((int)crop.left);
		cropR->setValue((int)crop.right);
		cropT->setValue((int)crop.top);
		cropB->setValue((int)crop.bottom);
		const bool enabled = !obs_sceneitem_locked(held);
		for (QWidget *w : std::initializer_list<QWidget *>{posX, posY, rot, sizeX, sizeY, align, boundsType,
								   boundsAlign, boundsW, boundsH, cropL, cropR, cropT,
								   cropB})
			w->setEnabled(enabled);
	};

	refreshControls();
	connect(posX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), dlg, applyFromControls);
	connect(posY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), dlg, applyFromControls);
	connect(rot, QOverload<double>::of(&QDoubleSpinBox::valueChanged), dlg, applyFromControls);
	connect(sizeX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), dlg, applyFromControls);
	connect(sizeY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), dlg, applyFromControls);
	connect(align, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, applyFromControls);
	connect(boundsType, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, applyFromControls);
	connect(boundsAlign, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, applyFromControls);
	connect(boundsW, QOverload<double>::of(&QDoubleSpinBox::valueChanged), dlg, applyFromControls);
	connect(boundsH, QOverload<double>::of(&QDoubleSpinBox::valueChanged), dlg, applyFromControls);
	connect(cropL, QOverload<int>::of(&QSpinBox::valueChanged), dlg, applyFromControls);
	connect(cropR, QOverload<int>::of(&QSpinBox::valueChanged), dlg, applyFromControls);
	connect(cropT, QOverload<int>::of(&QSpinBox::valueChanged), dlg, applyFromControls);
	connect(cropB, QOverload<int>::of(&QSpinBox::valueChanged), dlg, applyFromControls);
	connect(this, &ShortsDock::verticalTransformChanged, dlg, refreshControls);

	auto selectHeld = [this, held]() {
		if (!held || !scene)
			return;
		obs_scene_enum_items(scene, ClearSelection, nullptr);
		obs_sceneitem_select(held, true);
	};

	auto *quickRow = new QHBoxLayout();
	auto *fitBtn = new QPushButton(Translate("FitToCanvas"), dlg);
	auto *fillBtn = new QPushButton(Translate("FillCanvas"), dlg);
	auto *centerBtn = new QPushButton(Translate("CenterOnCanvas"), dlg);
	auto *resetQuickBtn = new QPushButton(Translate("ResetTransform"), dlg);
	quickRow->addWidget(fitBtn);
	quickRow->addWidget(fillBtn);
	quickRow->addWidget(centerBtn);
	quickRow->addWidget(resetQuickBtn);
	/* Same working transform functions as the right-click menu. */
	connect(fitBtn, &QPushButton::clicked, this, [this, selectHeld, refreshControls]() {
		selectHeld();
		RequestFitInsideVerticalCanvas();
		refreshControls();
	});
	connect(fillBtn, &QPushButton::clicked, this, [this, selectHeld, refreshControls]() {
		selectHeld();
		RequestFillVerticalCanvas();
		refreshControls();
	});
	connect(centerBtn, &QPushButton::clicked, this, [this, selectHeld, refreshControls]() {
		selectHeld();
		RequestCenterToScreen();
		refreshControls();
	});
	connect(resetQuickBtn, &QPushButton::clicked, this, [this, selectHeld, refreshControls]() {
		selectHeld();
		RequestResetTransform();
		refreshControls();
	});

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
	connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);

	auto *root = new QVBoxLayout(dlg);
	root->addLayout(form);
	root->addLayout(quickRow);
	root->addWidget(buttons);
	dlg->resize(420, dlg->sizeHint().height());
	dlg->show();
	dlg->raise();
	dlg->activateWindow();
}

void ShortsDock::RequestCropDialog()
{
	/* Crop lives in Edit Transform; open that dialog for a single place to edit. */
	RequestEditTransform();
}

void ShortsDock::RequestTransformEdited(double x, double y, double w, double h, double rot)
{
	if (updatingTransform || !scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_sceneitem_t *item = selected.front();
	if (obs_sceneitem_locked(item))
		return;

	vec2 pos;
	vec2_set(&pos, (float)x, (float)y);
	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_set_rot(item, (float)rot);

	obs_source_t *source = obs_sceneitem_get_source(item);
	uint32_t baseW = std::max(1u, obs_source_get_width(source));
	uint32_t baseH = std::max(1u, obs_source_get_height(source));
	obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);
	float innerW = float(baseW - crop.left - crop.right);
	float innerH = float(baseH - crop.top - crop.bottom);
	if (innerW < 1.0f)
		innerW = 1.0f;
	if (innerH < 1.0f)
		innerH = 1.0f;
	vec2 scale;
	vec2_set(&scale, (float)w / innerW, (float)h / innerH);
	obs_sceneitem_set_scale(item, &scale);
}

void ShortsDock::PopulateTransformControls(QDoubleSpinBox *x, QDoubleSpinBox *y, QDoubleSpinBox *w, QDoubleSpinBox *h,
					   QDoubleSpinBox *rot)
{
	if (!x || !y || !w || !h || !rot)
		return;
	updatingTransform = true;
	std::vector<obs_sceneitem_t *> selected;
	if (scene)
		obs_scene_enum_items(scene, CollectSelected, &selected);
	bool enable = !selected.empty() && !obs_sceneitem_locked(selected.front());
	x->setEnabled(enable);
	y->setEnabled(enable);
	w->setEnabled(enable);
	h->setEnabled(enable);
	rot->setEnabled(enable);
	if (!selected.empty()) {
		obs_sceneitem_t *item = selected.front();
		ItemTransform info = ReadItemTransform(item);
		obs_source_t *source = obs_sceneitem_get_source(item);
		obs_sceneitem_crop crop;
		obs_sceneitem_get_crop(item, &crop);
		float bw = float(obs_source_get_width(source) - crop.left - crop.right) * info.scale.x;
		float bh = float(obs_source_get_height(source) - crop.top - crop.bottom) * info.scale.y;
		x->setValue(info.pos.x);
		y->setValue(info.pos.y);
		w->setValue(bw);
		h->setValue(bh);
		rot->setValue(info.rot);
	}
	updatingTransform = false;
}

void ShortsDock::PopulateTransitions(QComboBox *combo, QSpinBox *duration)
{
	if (!combo || !duration)
		return;
	combo->clear();
	obs_frontend_source_list transitions = {};
	obs_frontend_get_transitions(&transitions);
	int select = -1;
	for (size_t i = 0; i < transitions.sources.num; i++) {
		obs_source_t *src = transitions.sources.array[i];
		const QString name = QString::fromUtf8(obs_source_get_name(src));
		combo->addItem(name, name);
		if (name == verticalTransitionName)
			select = (int)i;
	}
	obs_frontend_source_list_free(&transitions);
	if (select >= 0)
		combo->setCurrentIndex(select);
	else if (combo->count() > 0) {
		combo->setCurrentIndex(0);
		verticalTransitionName = combo->currentData().toString();
	}
	duration->setValue(verticalTransitionDurationMs);
}

void ShortsDock::RequestSetTransition(const QString &name)
{
	if (loadingSettings || name.isEmpty())
		return;
	verticalTransitionName = name;
	EnsureVerticalTransitionSource(name);
	/* Do not call obs_frontend_set_current_transition — vertical only. */
}

void ShortsDock::RequestSetTransitionDuration(int ms)
{
	if (loadingSettings)
		return;
	verticalTransitionDurationMs = ms < 0 ? 0 : (ms > 10000 ? 10000 : ms);
}

void ShortsDock::RequestPreviewTransition()
{
	if (!scene || !canvas)
		return;
	obs_source_t *cur = obs_scene_get_source(scene);
	obs_source_t *tr = EnsureVerticalTransitionSource(verticalTransitionName);
	if (!tr || !cur)
		return;
	obs_transition_set(tr, cur);
	obs_canvas_set_channel(canvas, 0, tr);
	obs_transition_start(tr, OBS_TRANSITION_MODE_AUTO, verticalTransitionDurationMs, cur);
}

void ShortsDock::RequestTriggerTransition()
{
	if (sceneOrder.size() < 2)
		return;
	const QString cur = ActiveSceneUuid();
	int idx = sceneOrder.indexOf(cur);
	int next = (idx + 1) % sceneOrder.size();
	RequestSelectScene(sceneOrder[next]);
}

void ShortsDock::OnGoLive()
{
	if (!outputs)
		return;
	if (shuttingDown) {
		QMessageBox::warning(this, Translate("GoLive"), Translate("ObsShuttingDown"));
		goLiveBtn->setChecked(false);
		return;
	}
	if (outputs->IsStreaming()) {
		outputs->StopStreaming();
		return;
	}

	QString err;
	QString field;
	if (!outputs->ValidateActiveDestination(&err, &field)) {
		goLiveBtn->setChecked(false);
		QMessageBox box(QMessageBox::Warning, Translate("GoLive"), err, QMessageBox::NoButton, this);
		auto *openBtn = box.addButton(Translate("OpenStreamingSettings"), QMessageBox::AcceptRole);
		box.addButton(QMessageBox::Cancel);
		box.exec();
		if (box.clickedButton() == openBtn)
			OpenSettingsStreaming();
		return;
	}

	if (!outputs->StartStreaming(&err)) {
		goLiveBtn->setChecked(false);
		QMessageBox box(QMessageBox::Warning, Translate("GoLive"), err, QMessageBox::NoButton, this);
		auto *openBtn = box.addButton(Translate("OpenStreamingSettings"), QMessageBox::AcceptRole);
		box.addButton(QMessageBox::Cancel);
		box.exec();
		if (box.clickedButton() == openBtn)
			OpenSettingsStreaming();
	}
}

void ShortsDock::OnRecord()
{
	if (!outputs)
		return;
	if (outputs->IsRecording()) {
		if (automation && automation->IsAutomationOwnedRecording() &&
		    settings.confirmManualStopDuringAutomation) {
			const auto reply = QMessageBox::question(
				this, Translate("Record"), Translate("ConfirmStopAutomatedRecording"),
				QMessageBox::Yes | QMessageBox::No);
			if (reply != QMessageBox::Yes) {
				recordBtn->setChecked(true);
				return;
			}
		}
		recordingStartedManually = false;
		if (automation)
			automation->SetManualRecordingActive(false);
		outputs->StopRecording();
		return;
	}
	QString err;
	if (!outputs->StartRecording(&err)) {
		recordBtn->setChecked(false);
		QMessageBox::warning(this, Translate("Record"), err);
		return;
	}
	recordingStartedManually = true;
	if (automation)
		automation->SetManualRecordingActive(true);
}

void ShortsDock::OnShortClip()
{
	if (!outputs)
		return;
	HandleClipSaveResult(outputs->SaveShortClip(), ClipKind::Short);
}

void ShortsDock::OnLongClip()
{
	if (!outputs)
		return;
	HandleClipSaveResult(outputs->SaveLongClip(), ClipKind::Long);
}

void ShortsDock::PopulateClipPresetCombos()
{
	if (!shortClipPresetCombo || !longClipPresetCombo)
		return;

	shortClipPresetCombo->clear();
	shortClipPresetCombo->addItem(Translate("Clip10"), (int)vsp::ShortClipPreset::Sec10);
	shortClipPresetCombo->addItem(Translate("Clip20"), (int)vsp::ShortClipPreset::Sec20);
	shortClipPresetCombo->addItem(Translate("Clip30"), (int)vsp::ShortClipPreset::Sec30);
	shortClipPresetCombo->addItem(Translate("Clip60"), (int)vsp::ShortClipPreset::Sec60);
	if (settings.shortClipPreset == vsp::ShortClipPreset::Custom) {
		shortClipPresetCombo->addItem(Translate("ClipCustom") +
						      QStringLiteral(" (%1s)").arg(settings.customShortClipSeconds),
					      (int)vsp::ShortClipPreset::Custom);
	}

	longClipPresetCombo->clear();
	longClipPresetCombo->addItem(Translate("LongClip2Min"), (int)vsp::LongClipPreset::Min2);
	longClipPresetCombo->addItem(Translate("LongClip3Min"), (int)vsp::LongClipPreset::Min3);
	longClipPresetCombo->addItem(Translate("LongClip4Min"), (int)vsp::LongClipPreset::Min4);
	longClipPresetCombo->addItem(Translate("LongClip5Min"), (int)vsp::LongClipPreset::Min5);
	if (settings.longClipPreset == vsp::LongClipPreset::Custom) {
		const int mins = settings.customLongClipSeconds / 60;
		const int secs = settings.customLongClipSeconds % 60;
		longClipPresetCombo->addItem(Translate("LongClipCustom") +
						     QStringLiteral(" (%1:%2)")
							     .arg(mins)
							     .arg(secs, 2, 10, QLatin1Char('0')),
					     (int)vsp::LongClipPreset::Custom);
	}
}

void ShortsDock::SyncClipPresetControls()
{
	if (!shortClipPresetCombo || !longClipPresetCombo)
		return;

	syncingClipPresets = true;
	PopulateClipPresetCombos();

	auto selectPreset = [](QComboBox *combo, int value) {
		const int idx = combo->findData(value);
		if (idx >= 0)
			combo->setCurrentIndex(idx);
		else if (combo->count() > 0)
			combo->setCurrentIndex(0);
	};

	selectPreset(shortClipPresetCombo, (int)settings.shortClipPreset);
	selectPreset(longClipPresetCombo, (int)settings.longClipPreset);

	const int shortSec = vsp::EffectiveShortClipSeconds(settings);
	const int longSec = vsp::EffectiveLongClipSeconds(settings);
	if (shortClipBtn) {
		shortClipBtn->setToolTip(Translate("ShortClipTip") + QStringLiteral(" — ") +
					 QString::number(shortSec) + QStringLiteral("s"));
	}
	if (longClipBtn) {
		longClipBtn->setToolTip(Translate("LongClipTip") + QStringLiteral(" — ") +
					 QString::number(longSec) + QStringLiteral("s"));
	}

	syncingClipPresets = false;
}

void ShortsDock::ApplyClipPresetChange()
{
	bool restartBuffer = false;
	if (outputs)
		outputs->ApplySettings(settings, &restartBuffer);

	if (restartBuffer && outputs && outputs->IsClipBufferActive()) {
		const auto reply = QMessageBox::question(this, Translate("Settings"), Translate("BufferRestartWarning"),
							 QMessageBox::Yes | QMessageBox::No);
		if (reply == QMessageBox::Yes) {
			outputs->StopClipBuffer();
			QString err;
			outputs->EnsureClipBuffer(&err);
		}
	} else {
		EnsureBufferIfConfigured();
	}

	SyncClipPresetControls();
	obs_frontend_save();
}

void ShortsDock::OnShortClipPresetChanged(int index)
{
	if (syncingClipPresets || !shortClipPresetCombo || index < 0)
		return;

	settings.shortClipPreset =
		static_cast<vsp::ShortClipPreset>(shortClipPresetCombo->itemData(index).toInt());
	ApplyClipPresetChange();
}

void ShortsDock::OnLongClipPresetChanged(int index)
{
	if (syncingClipPresets || !longClipPresetCombo || index < 0)
		return;

	settings.longClipPreset = static_cast<vsp::LongClipPreset>(longClipPresetCombo->itemData(index).toInt());
	ApplyClipPresetChange();
}

void ShortsDock::PopulateCanvasPresetCombo()
{
	if (!canvasPresetCombo)
		return;

	canvasPresetCombo->clear();
	canvasPresetCombo->addItem(Translate("PresetYouTube"), (int)vsp::CanvasPreset::YouTubeVertical);
	canvasPresetCombo->addItem(Translate("PresetTikTok"), (int)vsp::CanvasPreset::TikTokVertical);
	canvasPresetCombo->addItem(Translate("PresetTwitch"), (int)vsp::CanvasPreset::TwitchVertical);
	canvasPresetCombo->addItem(Translate("PresetInstagram"), (int)vsp::CanvasPreset::InstagramVertical);
	canvasPresetCombo->addItem(Translate("PresetCustom"), (int)vsp::CanvasPreset::Custom);
}

void ShortsDock::SyncCanvasPresetControl()
{
	if (!canvasPresetCombo)
		return;

	syncingCanvasPreset = true;
	if (canvasPresetCombo->count() == 0)
		PopulateCanvasPresetCombo();

	const int idx = canvasPresetCombo->findData((int)settings.canvasPreset);
	if (idx >= 0)
		canvasPresetCombo->setCurrentIndex(idx);
	else if (canvasPresetCombo->count() > 0)
		canvasPresetCombo->setCurrentIndex(0);
	syncingCanvasPreset = false;
}

void ShortsDock::ApplyCanvasPresetChange()
{
	const uint32_t oldW = verticalWidth;
	const uint32_t oldH = verticalHeight;
	ApplyCanvasFromSettings();

	if (verticalWidth != oldW || verticalHeight != oldH) {
		CreateView();
		if (outputs)
			outputs->SetVideo(video);
		if (canvas && scene)
			obs_canvas_set_channel(canvas, 0, obs_scene_get_source(scene));
		/* Preserve each item's fit mode; recalculate against the new canvas size. */
		ReapplyStoredFitModes();
		emit verticalTransformChanged();
	}

	obs_frontend_save();
}

void ShortsDock::OnCanvasPresetChanged(int index)
{
	if (syncingCanvasPreset || !canvasPresetCombo || index < 0)
		return;

	settings.canvasPreset = static_cast<vsp::CanvasPreset>(canvasPresetCombo->itemData(index).toInt());
	ApplyCanvasPresetChange();
}

void ShortsDock::HandleClipSaveResult(const ClipSaveInfo &info, ClipKind kind)
{
	const QString title = Translate(kind == ClipKind::Long ? "LongClip" : "ShortClip");
	if (info.result == ClipSaveResult::Ok)
		return;
	if (info.result == ClipSaveResult::PartialAvailable) {
		if (!settings.saveAvailableWhenShort) {
			QMessageBox::warning(this, title, info.message);
			return;
		}
		const auto reply = QMessageBox::question(
			this, title, info.message + QString("\n\n") + Translate("SaveAvailablePortion"),
			QMessageBox::Yes | QMessageBox::No);
		if (reply == QMessageBox::Yes && outputs) {
			const ClipSaveInfo again = outputs->SaveClipOfDuration(info.availableSeconds, kind, true);
			if (again.result != ClipSaveResult::Ok)
				QMessageBox::warning(this, title, again.message);
		}
		return;
	}
	QMessageBox::warning(this, title, info.message.isEmpty() ? Translate("ClipFailedGeneric") : info.message);
}

void ShortsDock::CollectSceneLists(QStringList &names, QStringList &uuids) const
{
	names.clear();
	uuids.clear();
	for (const QString &uuid : sceneOrder) {
		obs_scene_t *sc = FindVerticalSceneByUuid(uuid);
		if (!sc)
			continue;
		obs_source_t *src = obs_scene_get_source(sc);
		names << QString::fromUtf8(obs_source_get_name(src));
		uuids << uuid;
	}
}

void ShortsDock::OnSettings()
{
	OpenSettingsStreaming(false);
}

void ShortsDock::ApplySettingsResult(const vsp::PluginSettings &next, bool wantsAutomationReset)
{
	const uint32_t oldW = verticalWidth;
	const uint32_t oldH = verticalHeight;
	settings = next;
	ApplyCanvasFromSettings();

	bool restartBuffer = false;
	if (outputs)
		outputs->ApplySettings(settings, &restartBuffer);

	if (wantsAutomationReset && automation)
		automation->ResetRuntimeState();
	if (automation)
		automation->ApplySettings(settings);

	if (verticalWidth != oldW || verticalHeight != oldH) {
		CreateView();
		if (outputs)
			outputs->SetVideo(video);
		if (canvas && scene)
			obs_canvas_set_channel(canvas, 0, obs_scene_get_source(scene));
		ReapplyStoredFitModes();
		emit verticalTransformChanged();
	}

	if (restartBuffer && outputs && outputs->IsClipBufferActive()) {
		const auto reply = QMessageBox::question(this, Translate("Settings"), Translate("BufferRestartWarning"),
							 QMessageBox::Yes | QMessageBox::No);
		if (reply == QMessageBox::Yes) {
			outputs->StopClipBuffer();
			QString err;
			outputs->EnsureClipBuffer(&err);
		}
	} else {
		EnsureBufferIfConfigured();
	}

	SyncCanvasPresetControl();
	SyncClipPresetControls();
}

void ShortsDock::OpenSettingsStreaming(bool focusStreaming)
{
	QStringList names, uuids;
	CollectSceneLists(names, uuids);
	const QString statusText = automation ? automation->StatusText() : QString();
	const auto status = automation ? automation->Status() : vsp::AutomationStatus::Disabled;

	SettingsDialog dlg(settings, outputs.get(), names, uuids, status, statusText, this);
	if (focusStreaming)
		dlg.FocusStreamingTab();

	/* Apply updates runtime immediately without closing; OK also emits applied then closes.
	 * Cancel discards unapplied edits (already-persisted destination secrets remain). */
	connect(&dlg, &SettingsDialog::applied, this, [&]() {
		ApplySettingsResult(dlg.result(), dlg.WantsAutomationReset());
	});
	dlg.exec();
}

void ShortsDock::OnStreamingChanged(bool active)
{
	if (!goLiveBtn)
		return;
	goLiveBtn->setChecked(active);
	goLiveBtn->setText(QString::fromUtf8(active ? "\U0001F534" : "\U0001F7E2"));
	goLiveBtn->setToolTip(Translate(active ? "StopGoLive" : "GoLive"));
	if (automation) {
		if (active)
			automation->OnVerticalLiveStarted();
		else
			automation->OnVerticalLiveStopped();
	}
}

void ShortsDock::OnRecordingChanged(bool active)
{
	if (!recordBtn)
		return;
	recordBtn->setChecked(active);
	recordBtn->setText(QString::fromUtf8("\u23FA\uFE0F"));
	recordBtn->setToolTip(Translate(active ? "StopRecord" : "Record"));
	if (!active)
		recordingStartedManually = false;
	if (automation) {
		automation->SetManualRecordingActive(recordingStartedManually && active);
		automation->OnVerticalRecordingChanged(active);
	}
}

void ShortsDock::OnClipSaved(const QString &path, ClipKind kind)
{
	const QString title = Translate(kind == ClipKind::Long ? "LongClip" : "ShortClip");
	QMessageBox::information(this, title, QString::fromUtf8(Translate("ClipSaved")).arg(path));
}

void ShortsDock::OnAutomationStatus(vsp::AutomationStatus status, const QString &text)
{
	automationStatus = status;
	automationStatusText = text;
}

void ShortsDock::OnAutomationNotify(const QString &title, const QString &message)
{
	QMessageBox::information(this, title, message);
}

void ShortsDock::OnBufferStatus(BufferStatus, const QString &)
{
}

void ShortsDock::EnsureBufferIfConfigured()
{
	if (!outputs || clearing || shuttingDown)
		return;
	if (!settings.clipBufferEnabled || !settings.autoStartClipBuffer)
		return;
	QString err;
	outputs->EnsureClipBuffer(&err);
}

void ShortsDock::SaveSettings(obs_data_t *data)
{
	/* Persist secrets to OS credential store; settings blob never contains keys. */
	for (const vsp::StreamDestination &d : settings.destinations) {
		vsp::SaveSecret(vsp::KeyTarget(d.id), d.streamKey);
		if (!d.password.isEmpty())
			vsp::SaveSecret(vsp::PasswordTarget(d.id), d.password);
		else
			vsp::DeleteSecret(vsp::PasswordTarget(d.id));
	}
	vsp::SaveSettingsToData(data, settings, verticalWidth, verticalHeight);
	/* Never claim a downgrade when a newer schema was loaded by an older plugin. */
	if (configSchemaTooNew && loadedConfigSchema > vsp::kConfigSchemaVersion) {
		obs_data_set_int(data, "config_schema", loadedConfigSchema);
		blog(LOG_WARNING,
		     "[obs-shorts-vertical] Preserving newer config_schema=%d (plugin understands %d)",
		     loadedConfigSchema, vsp::kConfigSchemaVersion);
	}
	SaveHotkeys(data);

	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (const QString &uuid : sceneOrder) {
		obs_scene_t *sc = FindVerticalSceneByUuid(uuid);
		if (!sc)
			continue;
		OBSDataAutoRelease obj = obs_data_create();
		obs_data_set_string(obj, "uuid", uuid.toUtf8().constData());
		obs_data_set_string(obj, "main_uuid", uuid.toUtf8().constData());
		OBSDataAutoRelease sceneData = obs_save_source(obs_scene_get_source(sc));
		obs_data_set_obj(obj, "source", sceneData);
		obs_data_array_push_back(arr, obj);
	}
	obs_data_set_array(data, "scenes", arr);
	obs_data_set_array(data, "vertical_mirrors", arr);
	obs_data_set_string(data, "vertical_transition", verticalTransitionName.toUtf8().constData());
	obs_data_set_int(data, "vertical_transition_ms", verticalTransitionDurationMs);
	obs_data_set_string(data, "active_vertical_scene", ActiveSceneUuid().toUtf8().constData());
}

void ShortsDock::LoadSettings(obs_data_t *data)
{
	loadingSettings = true;

	const int storedSchema = vsp::ReadConfigSchema(data);
	const auto schemaAction = vsp::ClassifyConfigSchema(storedSchema);
	configSchemaTooNew = false;
	loadedConfigSchema = storedSchema;

	if (schemaAction == vsp::ConfigSchemaAction::RefuseDowngrade) {
		configSchemaTooNew = true;
		loadedConfigSchema = storedSchema;
		blog(LOG_WARNING,
		     "[obs-shorts-vertical] Config schema %d is newer than plugin schema %d; "
		     "loading best-effort without downgrading",
		     storedSchema, vsp::kConfigSchemaVersion);
	} else if (schemaAction == vsp::ConfigSchemaAction::MigrateForward) {
		if (!vsp::BackupConfigBlob(data, "pre-schema-migration")) {
			blog(LOG_WARNING,
			     "[obs-shorts-vertical] Could not write config backup before schema migration");
		}
		vsp::MigrateConfigSchema(data, storedSchema, vsp::kConfigSchemaVersion);
		loadedConfigSchema = vsp::kConfigSchemaVersion;
	} else {
		loadedConfigSchema = storedSchema > 0 ? storedSchema : vsp::kConfigSchemaVersion;
	}

	settings = vsp::LoadSettingsFromData(data, verticalWidth, verticalHeight);
	vsp::EnsureDefaultDestinations(settings);

	for (vsp::StreamDestination &d : settings.destinations) {
		if (!d.streamKey.isEmpty()) {
			vsp::SaveSecret(vsp::KeyTarget(d.id), d.streamKey);
		} else {
			QString key;
			vsp::LoadSecret(vsp::KeyTarget(d.id), &key);
			d.streamKey = key;
		}
		if (!d.password.isEmpty()) {
			vsp::SaveSecret(vsp::PasswordTarget(d.id), d.password);
		} else {
			QString pass;
			vsp::LoadSecret(vsp::PasswordTarget(d.id), &pass);
			d.password = pass;
		}
	}

	LoadHotkeys(data);
	ApplyCanvasFromSettings();
	CreateView();

	if (outputs)
		outputs->ApplySettings(settings);

	verticalScenes.clear();
	sceneOrder.clear();

	obs_data_array_t *arr = obs_data_get_array(data, "vertical_mirrors");
	if (!arr)
		arr = obs_data_get_array(data, "scenes");

	if (arr) {
		const size_t count = obs_data_array_count(arr);
		for (size_t i = 0; i < count; i++) {
			OBSDataAutoRelease obj = obs_data_array_item(arr, i);
			const char *uuid = obs_data_get_string(obj, "uuid");
			if (!uuid || !*uuid)
				uuid = obs_data_get_string(obj, "main_uuid");
			obs_data_t *sourceData = obs_data_get_obj(obj, "source");
			if (!sourceData)
				continue;
			obs_source_t *src = obs_load_source(sourceData);
			obs_data_release(sourceData);
			if (!src)
				continue;
			obs_scene_t *loaded = obs_scene_from_source(src);
			if (loaded) {
				if (canvas)
					obs_canvas_move_scene(loaded, canvas);
				const char *realUuid = obs_source_get_uuid(src);
				const QString key = realUuid && *realUuid ? QString::fromUtf8(realUuid)
									  : QString::fromUtf8(uuid ? uuid : "");
				if (!key.isEmpty()) {
					verticalScenes.insert(key, loaded);
					sceneOrder.append(key);
				}
			}
			obs_source_release(src);
		}
		obs_data_array_release(arr);
	}

	const char *trName = obs_data_get_string(data, "vertical_transition");
	if (trName && *trName)
		verticalTransitionName = QString::fromUtf8(trName);
	if (obs_data_has_user_value(data, "vertical_transition_ms"))
		verticalTransitionDurationMs = (int)obs_data_get_int(data, "vertical_transition_ms");

	if (outputs)
		outputs->ApplySettings(settings);

	loadingSettings = false;
	RefreshVerticalWorkspace(true);
	if (automation)
		automation->ApplySettings(settings);

	const char *active = obs_data_get_string(data, "active_vertical_scene");
	if (active && *active)
		RequestSelectScene(QString::fromUtf8(active));
	EmitSceneUiChanged();
	emit verticalTransitionsChanged();
	SyncCanvasPresetControl();
	SyncClipPresetControls();
}

void ShortsDock::FrontendEvent(enum obs_frontend_event event, void *private_data)
{
	auto *dock = static_cast<ShortsDock *>(private_data);
	if (!dock || dock->clearing)
		return;

	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		QMetaObject::invokeMethod(dock, [dock]() {
			if (dock->automation)
				dock->automation->OnObsFinishedLoading();
			/* Video system is fully up — refresh canvas mix + PROGRAM channel
			 * so capture devices activate on the Vertical Shorts path. */
			dock->BootstrapVerticalCanvasPipeline();
			dock->RefreshVerticalWorkspace(true);
			dock->MaybeBootstrapIndependentCamera();
			dock->EnsureBufferIfConfigured();
			dock->EmitSceneUiChanged();
			emit dock->verticalTransitionsChanged();
			dock->LogRenderPipeline("FINISHED_LOADING");
		}, Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_EXIT:
	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
		dock->shuttingDown = true;
		if (dock->automation)
			dock->automation->OnObsShutdown();
		if (dock->outputs)
			dock->outputs->StopAll();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		QMetaObject::invokeMethod(dock, [dock]() {
			if (dock->automation)
				dock->automation->OnMainStreamingStarted();
		}, Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		QMetaObject::invokeMethod(dock, [dock]() {
			if (dock->automation)
				dock->automation->OnMainStreamingStopped();
		}, Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_TRANSITION_LIST_CHANGED:
		QMetaObject::invokeMethod(dock, [dock]() { emit dock->verticalTransitionsChanged(); },
					  Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		QMetaObject::invokeMethod(dock, [dock]() { dock->EmitSourceUiChanged(); }, Qt::QueuedConnection);
		break;
	default:
		break;
	}
}
void ShortsDock::UpdatePreviewScale(int cx, int cy)
{
	GetScaleAndCenterPos((int)verticalWidth, (int)verticalHeight, cx, cy, previewX, previewY,
			     previewScale);
}

void ShortsDock::showEvent(QShowEvent *event)
{
	QFrame::showEvent(event);
	if (preview) {
		preview->CreateDisplay(true);
		if (preview->GetDisplay())
			obs_display_set_enabled(preview->GetDisplay(), true);
	}
	EnsureCanvasProgramChannel(true);
	EnsurePreviewSceneShowing(true);
}

void ShortsDock::DrawCallback(void *data, uint32_t cx, uint32_t cy)
{
	static_cast<ShortsDock *>(data)->DrawPreview(cx, cy);
}

void ShortsDock::DrawPreview(uint32_t cx, uint32_t cy)
{
	drawCallbackCount++;

	/* One-shot proof the OBS display callback is alive (not a white QWidget). */
	if (drawCallbackCount == 1) {
		size_t itemCount = 0;
		if (scene) {
			obs_scene_enum_items(
				scene,
				[](obs_scene_t *, obs_sceneitem_t *, void *p) -> bool {
					(*static_cast<size_t *>(p))++;
					return true;
				},
				&itemCount);
		}
		obs_source_t *sceneSrc = scene ? obs_scene_get_source(scene) : nullptr;
		blog(LOG_INFO,
		     "[obs-shorts-vertical] VerticalPreview draw callback executing: width=%u height=%u "
		     "activeVerticalScene='%s' sceneItemCount=%zu canvas=%p",
		     cx, cy, sceneSrc ? obs_source_get_name(sceneSrc) : "(null)", itemCount, (void *)canvas);
	}

	const uint32_t canvasW = verticalWidth > 0 ? verticalWidth : 1080;
	const uint32_t canvasH = verticalHeight > 0 ? verticalHeight : 1920;
	UpdatePreviewScale((int)cx, (int)cy);

	/* Self-heal on the UI thread: empty PROGRAM channel 0 means ACTIVATE never
	 * reached vertical scene items (blank VCD preview). Do not rebind here —
	 * this callback runs on the graphics thread. */
	if (canvas && scene) {
		obs_source_t *channel0 = obs_canvas_get_channel(canvas, 0);
		if (!channel0) {
			QMetaObject::invokeMethod(
				this,
				[this]() {
					if (!clearing)
						EnsureCanvasProgramChannel(true);
				},
				Qt::QueuedConnection);
		} else {
			obs_source_release(channel0);
		}
	}

	/* Throttled pipeline diagnostics (~every 5s while drawing). */
	const uint64_t now = os_gettime_ns();
	if (lastPipelineLogNs == 0 || now - lastPipelineLogNs > 5000000000ULL) {
		lastPipelineLogNs = now;
		LogRenderPipeline("DrawPreview");
	}

	gs_viewport_push();
	gs_projection_push();

	/* Dark OBS-style clear — never white. */
	vec4 clearColor;
	vec4_set(&clearColor, 0.155f, 0.155f, 0.155f, 1.0f);
	gs_clear(GS_CLEAR_COLOR, &clearColor, 0.0f, 0);

	/* Guard against zero-size / off-screen projection (would look blank/white). */
	if (cx < 2 || cy < 2 || canvasW < 2 || canvasH < 2) {
		gs_projection_pop();
		gs_viewport_pop();
		return;
	}

	const int vpX = previewX;
	const int vpY = previewY;
	const int vpW = std::max(1, (int)(previewScale * canvasW));
	const int vpH = std::max(1, (int)(previewScale * canvasH));
	gs_ortho(0.0f, (float)canvasW, 0.0f, (float)canvasH, -100.0f, 100.0f);
	gs_set_viewport(vpX, vpY, vpW, vpH);

	/* OBS-style solid backdrop (vertex buffer + matrix), not gs_draw_sprite. */
	if (!box) {
		gs_render_start(true);
		gs_vertex2f(0.0f, 0.0f);
		gs_vertex2f(0.0f, 1.0f);
		gs_vertex2f(1.0f, 1.0f);
		gs_vertex2f(0.0f, 0.0f);
		gs_vertex2f(1.0f, 1.0f);
		gs_vertex2f(1.0f, 0.0f);
		box = gs_render_save();
	}
	if (box) {
		gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
		gs_eparam_t *colorParam = gs_effect_get_param_by_name(solid, "color");
		gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
		vec4 boxColor;
		vec4_set(&boxColor, 0.0f, 0.0f, 0.0f, 1.0f);
		gs_effect_set_vec4(colorParam, &boxColor);
		gs_technique_begin(tech);
		gs_technique_begin_pass(tech, 0);
		gs_matrix_push();
		gs_matrix_identity();
		gs_matrix_scale3f((float)canvasW, (float)canvasH, 1.0f);
		gs_load_vertexbuffer(box);
		gs_draw(GS_TRISTRIP, 0, 0);
		gs_matrix_pop();
		gs_technique_end_pass(tech);
		gs_technique_end(tech);
		gs_load_vertexbuffer(nullptr);
	}

	/* Render the Vertical Shorts canvas (channel 0 = Vertical Scene 1).
	 * Prefer obs_canvas_render — the canvas owns the PROGRAM view for this dock.
	 * Fallback to rendering channel 0 / scene source if needed. */
	if (canvas) {
		obs_canvas_render(canvas);
		obs_source_t *channel0 = obs_canvas_get_channel(canvas, 0);
		if (!channel0) {
			QMetaObject::invokeMethod(
				this,
				[this]() {
					if (!clearing) {
						EnsureCanvasProgramChannel(true);
						EnsurePreviewSceneShowing(true);
					}
				},
				Qt::QueuedConnection);
		} else {
			obs_source_release(channel0);
		}
	} else if (scene) {
		obs_source_t *source = obs_scene_get_source(scene);
		if (source)
			obs_source_video_render(source);
	}

	gs_load_vertexbuffer(nullptr);
	DrawSceneEditing();

	gs_projection_pop();
	gs_viewport_pop();
}

void ShortsDock::DrawSceneEditing()
{
	if (!scene)
		return;

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");

	vec4 color;
	vec4_set(&color, 1.0f, 1.0f, 1.0f, 0.85f);
	gs_eparam_t *param = gs_effect_get_param_by_name(solid, "color");
	gs_effect_set_vec4(param, &color);

	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);

	if (!rectFill) {
		gs_render_start(true);
		gs_vertex2f(0.0f, 0.0f);
		gs_vertex2f(0.0f, 1.0f);
		gs_vertex2f(1.0f, 1.0f);
		gs_vertex2f(0.0f, 0.0f);
		gs_vertex2f(1.0f, 1.0f);
		gs_vertex2f(1.0f, 0.0f);
		rectFill = gs_render_save();
	}

	obs_scene_enum_items(scene, DrawSelectedItem, this);

	gs_technique_end_pass(tech);
	gs_technique_end(tech);
	gs_load_vertexbuffer(nullptr);
}

bool ShortsDock::DrawSelectedItem(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	if (!obs_sceneitem_selected(item))
		return true;

	auto *dock = static_cast<ShortsDock *>(param);

	matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	gs_matrix_push();
	gs_matrix_mul(&boxTransform);

	vec2 scale;
	vec2_set(&scale, dock->previewScale, dock->previewScale);
	DrawRect(HANDLE_RADIUS, scale);

	gs_load_vertexbuffer(dock->rectFill);
	DrawSquareAtPos(0.0f, 0.0f, HANDLE_RADIUS);
	DrawSquareAtPos(0.0f, 1.0f, HANDLE_RADIUS);
	DrawSquareAtPos(1.0f, 0.0f, HANDLE_RADIUS);
	DrawSquareAtPos(1.0f, 1.0f, HANDLE_RADIUS);
	DrawSquareAtPos(0.5f, 0.0f, HANDLE_RADIUS);
	DrawSquareAtPos(0.0f, 0.5f, HANDLE_RADIUS);
	DrawSquareAtPos(0.5f, 1.0f, HANDLE_RADIUS);
	DrawSquareAtPos(1.0f, 0.5f, HANDLE_RADIUS);

	gs_matrix_pop();
	return true;
}

std::unique_ptr<OBSEventFilter> ShortsDock::BuildEventFilter()
{
	return std::make_unique<OBSEventFilter>(
		[this](QObject *obj, QEvent *event) { return HandlePreviewEvent(obj, event); });
}

bool ShortsDock::HandlePreviewEvent(QObject *, QEvent *event)
{
	switch (event->type()) {
	case QEvent::MouseButtonPress: {
		auto *mouse = static_cast<QMouseEvent *>(event);
		if (mouse->button() == Qt::RightButton) {
			ShowContextMenu(mouse->globalPosition().toPoint());
			return true;
		}
		if (mouse->button() != Qt::LeftButton || locked)
			return false;

		startPos = GetMouseEventPos(mouse);
		mousePos = startPos;
		mouseDown = true;
		mouseMoved = false;
		lastMoveOffset.x = 0.0f;
		lastMoveOffset.y = 0.0f;

		GetStretchHandleData(startPos);
		mouseOverItems = stretchHandle == ItemHandle::None && SelectedAtPos(startPos);
		if (stretchHandle == ItemHandle::None && !mouseOverItems)
			DoSelect(startPos);

		EmitSourceUiChanged();
		return true;
	}
	case QEvent::MouseButtonRelease: {
		auto *mouse = static_cast<QMouseEvent *>(event);
		if (mouse->button() != Qt::LeftButton)
			return false;

		if (mouseDown && !mouseMoved && stretchHandle == ItemHandle::None)
			DoSelect(GetMouseEventPos(mouse));

		mouseDown = false;
		mouseMoved = false;
		stretchHandle = ItemHandle::None;
		stretchItem = nullptr;
		if (preview)
			preview->setCursor(Qt::ArrowCursor);
		EmitSourceUiChanged();
		return true;
	}
	case QEvent::MouseMove: {
		auto *mouse = static_cast<QMouseEvent *>(event);
		vec2 pos = GetMouseEventPos(mouse);
		mousePos = pos;

		if (!mouseDown) {
			GetStretchHandleData(pos);
			UpdateCursor((uint32_t)stretchHandle);
			stretchHandle = ItemHandle::None;
			stretchItem = nullptr;
			return false;
		}

		if (locked)
			return true;

		vec2 offset;
		vec2_sub(&offset, &pos, &startPos);
		if (!mouseMoved && vec2_len(&offset) < 2.0f)
			return true;

		mouseMoved = true;

		if (stretchHandle != ItemHandle::None)
			StretchItem(pos);
		else if (mouseOverItems)
			MoveItems(pos);

		emit verticalTransformChanged();
		return true;
	}
	case QEvent::ContextMenu: {
		auto *ctx = static_cast<QContextMenuEvent *>(event);
		ShowContextMenu(ctx->globalPos());
		return true;
	}
	default:
		return false;
	}
}

void ShortsDock::ShowContextMenu(const QPoint &globalPos)
{
	if (!scene)
		return;

	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	const bool has = !selected.empty();

	QMenu menu(this);
	QAction *props = menu.addAction(Translate("SourceProperties"), this, &ShortsDock::RequestSourceProperties);
	props->setToolTip(Translate("SourcePropertiesTip"));
	props->setEnabled(has && selected.front() &&
			  obs_source_configurable(obs_sceneitem_get_source(selected.front())));
	QAction *filters = menu.addAction(Translate("SourceFilters"), this, &ShortsDock::RequestSourceFilters);
	filters->setToolTip(Translate("SourceFiltersTip"));
	filters->setEnabled(has);
	menu.addSeparator();

	QMenu *transformMenu = menu.addMenu(Translate("Transform"));
	transformMenu->setEnabled(has);
	QAction *editTf = transformMenu->addAction(Translate("EditTransform"), this, &ShortsDock::RequestEditTransform);
	editTf->setToolTip(Translate("EditTransformTip"));
	transformMenu->addSeparator();
	AppendTransformFitMenu(transformMenu);
	transformMenu->addSeparator();
	transformMenu->addAction(Translate("ResetTransform"), this, &ShortsDock::RequestResetTransform);
	transformMenu->addAction(Translate("Rotate90CW"), this, [this]() { RequestRotateDegrees(90.0f); });
	transformMenu->addAction(Translate("Rotate90CCW"), this, [this]() { RequestRotateDegrees(-90.0f); });
	transformMenu->addAction(Translate("Rotate180"), this, [this]() { RequestRotateDegrees(180.0f); });
	transformMenu->addAction(Translate("FlipHorizontal"), this, &ShortsDock::RequestFlipHorizontal);
	transformMenu->addAction(Translate("FlipVertical"), this, &ShortsDock::RequestFlipVertical);

	menu.addSeparator();
	QAction *lockAction = menu.addAction(Translate("ToggleLock"), this, &ShortsDock::RequestToggleSourceLock);
	lockAction->setEnabled(has);
	QAction *visAction = menu.addAction(Translate("ToggleVisible"), this, &ShortsDock::RequestToggleSourceVisible);
	visAction->setEnabled(has);

	menu.exec(globalPos);
}

vec2 ShortsDock::GetMouseEventPos(QMouseEvent *event)
{
	/* Match OBSBasicPreview: logical widget coords → canvas space. */
	const float pixelRatio = preview ? (float)preview->devicePixelRatioF() : 1.0f;
	const float scale = pixelRatio / std::max(previewScale, 0.0001f);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	const float mx = (float)event->position().x();
	const float my = (float)event->position().y();
#else
	const float mx = (float)event->localPos().x();
	const float my = (float)event->localPos().y();
#endif
	vec2 pos;
	vec2_set(&pos, (mx - (float)previewX / pixelRatio) * scale, (my - (float)previewY / pixelRatio) * scale);
	return pos;
}

vec2 ShortsDock::GetItemSize(obs_sceneitem_t *item)
{
	vec2 size{};
	if (!item)
		return size;

	if (obs_sceneitem_get_bounds_type(item) != OBS_BOUNDS_NONE) {
		obs_sceneitem_get_bounds(item, &size);
		return size;
	}

	obs_source_t *source = obs_sceneitem_get_source(item);
	obs_sceneitem_crop crop{};
	vec2 scale{};
	obs_sceneitem_get_scale(item, &scale);
	obs_sceneitem_get_crop(item, &crop);
	size.x = std::max(float((int)obs_source_get_width(source) - crop.left - crop.right), 0.0f);
	size.y = std::max(float((int)obs_source_get_height(source) - crop.top - crop.bottom), 0.0f);
	vec2_mul(&size, &size, &scale);
	return size;
}

vec3 ShortsDock::CalculateStretchPos(const vec3 &tl, const vec3 &br) const
{
	vec3 pos{};
	if (!stretchItem)
		return pos;

	const uint32_t alignment = obs_sceneitem_get_alignment(stretchItem);
	if (alignment & OBS_ALIGN_LEFT)
		pos.x = tl.x;
	else if (alignment & OBS_ALIGN_RIGHT)
		pos.x = br.x;
	else
		pos.x = (br.x - tl.x) * 0.5f + tl.x;

	if (alignment & OBS_ALIGN_TOP)
		pos.y = tl.y;
	else if (alignment & OBS_ALIGN_BOTTOM)
		pos.y = br.y;
	else
		pos.y = (br.y - tl.y) * 0.5f + tl.y;
	return pos;
}

OBSSceneItem ShortsDock::GetItemAtPos(const vec2 &pos, bool selectBelow)
{
	if (!scene)
		return nullptr;

	ItemAtPosData data;
	data.pos = pos;
	data.selectBelow = selectBelow;
	obs_scene_enum_items(scene, ItemAtPosFilter, &data);
	return data.item ? data.item : data.selectedBelow;
}

bool ShortsDock::SelectedAtPos(const vec2 &pos)
{
	if (!scene)
		return false;
	SelectedAtPosData data;
	data.pos = pos;
	obs_scene_enum_items(scene, SelectedAtPosFilter, &data);
	return data.selected;
}

void ShortsDock::DoSelect(const vec2 &pos)
{
	if (!scene)
		return;
	obs_sceneitem_t *item = GetItemAtPos(pos, false);
	obs_scene_enum_items(scene, ClearSelection, nullptr);
	if (item)
		obs_sceneitem_select(item, true);
	EmitSourceUiChanged();
}

void ShortsDock::GetStretchHandleData(const vec2 &pos)
{
	stretchHandle = ItemHandle::None;
	stretchItem = nullptr;
	if (!scene || locked)
		return;

	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;

	obs_sceneitem_t *item = selected.front();
	if (obs_sceneitem_locked(item))
		return;

	matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	vec3 tl, tr, bl, br, tc, cl, cr, bc;
	vec3_set(&tl, 0.0f, 0.0f, 0.0f);
	vec3_set(&tr, 1.0f, 0.0f, 0.0f);
	vec3_set(&bl, 0.0f, 1.0f, 0.0f);
	vec3_set(&br, 1.0f, 1.0f, 0.0f);
	vec3_set(&tc, 0.5f, 0.0f, 0.0f);
	vec3_set(&cl, 0.0f, 0.5f, 0.0f);
	vec3_set(&cr, 1.0f, 0.5f, 0.0f);
	vec3_set(&bc, 0.5f, 1.0f, 0.0f);

	auto toScreen = [&](vec3 &v) { vec3_transform(&v, &v, &boxTransform); };
	toScreen(tl);
	toScreen(tr);
	toScreen(bl);
	toScreen(br);
	toScreen(tc);
	toScreen(cl);
	toScreen(cr);
	toScreen(bc);

	/* Larger, DPI-aware hit target so corners are easy to grab. */
	const float pixelRatio = preview ? (float)preview->devicePixelRatioF() : 1.0f;
	const float radius = std::max(12.0f, HANDLE_RADIUS * 2.5f * pixelRatio) / std::max(previewScale, 0.0001f);
	vec3 mouse;
	vec3_set(&mouse, pos.x, pos.y, 0.0f);

	struct HandleTest {
		vec3 *p;
		ItemHandle handle;
	} tests[] = {
		{&tl, ItemHandle::TopLeft},     {&tr, ItemHandle::TopRight},
		{&bl, ItemHandle::BottomLeft},  {&br, ItemHandle::BottomRight},
		{&tc, ItemHandle::TopCenter},   {&bc, ItemHandle::BottomCenter},
		{&cl, ItemHandle::CenterLeft},  {&cr, ItemHandle::CenterRight},
	};

	for (auto &t : tests) {
		if (vec3_dist(&mouse, t.p) <= radius) {
			stretchHandle = t.handle;
			stretchItem = item;
			break;
		}
	}

	if (stretchHandle == ItemHandle::None)
		return;

	stretchItemSize = GetItemSize(item);

	vec3 itemUL;
	vec3_from_vec4(&itemUL, &boxTransform.t);
	const float itemRot = obs_sceneitem_get_rot(item);

	/* OBS-style item ↔ screen matrices from box UL + rotation. */
	matrix4_identity(&itemToScreen);
	matrix4_rotate_aa4f(&itemToScreen, &itemToScreen, 0.0f, 0.0f, 1.0f, RAD(itemRot));
	matrix4_translate3f(&itemToScreen, &itemToScreen, itemUL.x, itemUL.y, 0.0f);

	matrix4_identity(&screenToItem);
	matrix4_translate3f(&screenToItem, &screenToItem, -itemUL.x, -itemUL.y, 0.0f);
	matrix4_rotate_aa4f(&screenToItem, &screenToItem, 0.0f, 0.0f, 1.0f, RAD(-itemRot));

	obs_sceneitem_get_crop(item, &startCrop);
	obs_sceneitem_get_pos(item, &startItemPos);

	obs_source_t *source = obs_sceneitem_get_source(item);
	vec2_set(&cropSize, float(obs_source_get_width(source) - startCrop.left - startCrop.right),
		 float(obs_source_get_height(source) - startCrop.top - startCrop.bottom));
}

void ShortsDock::MoveItems(const vec2 &pos)
{
	if (!scene)
		return;

	vec2 offset;
	vec2_sub(&offset, &pos, &startPos);
	vec2_sub(&offset, &offset, &lastMoveOffset);

	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected) {
		if (obs_sceneitem_locked(item))
			continue;
		vec2 itemPos;
		obs_sceneitem_get_pos(item, &itemPos);
		vec2_add(&itemPos, &itemPos, &offset);
		obs_sceneitem_set_pos(item, &itemPos);
	}

	vec2_sub(&lastMoveOffset, &pos, &startPos);
}

void ShortsDock::StretchItem(const vec2 &pos)
{
	if (!stretchItem || obs_sceneitem_locked(stretchItem))
		return;

	obs_source_t *source = obs_sceneitem_get_source(stretchItem);
	const uint32_t source_cx = source ? obs_source_get_width(source) : 0;
	const uint32_t source_cy = source ? obs_source_get_height(source) : 0;
	if (!source_cx || !source_cy)
		return;

	const obs_bounds_type boundsType = obs_sceneitem_get_bounds_type(stretchItem);
	const uint32_t stretchFlags = (uint32_t)stretchHandle;

	vec3 tl{}, br{};
	vec3_set(&br, stretchItemSize.x, stretchItemSize.y, 0.0f);

	vec3 pos3{};
	vec3_set(&pos3, pos.x, pos.y, 0.0f);
	vec3_transform(&pos3, &pos3, &screenToItem);

	if (stretchFlags & ITEM_LEFT)
		tl.x = pos3.x;
	else if (stretchFlags & ITEM_RIGHT)
		br.x = pos3.x;

	if (stretchFlags & ITEM_TOP)
		tl.y = pos3.y;
	else if (stretchFlags & ITEM_BOTTOM)
		br.y = pos3.y;

	if (tl.x > br.x)
		std::swap(tl.x, br.x);
	if (tl.y > br.y)
		std::swap(tl.y, br.y);

	vec2 size{};
	vec2_set(&size, br.x - tl.x, br.y - tl.y);
	vec2_abs(&size, &size);
	size.x = std::max(1.0f, size.x);
	size.y = std::max(1.0f, size.y);

	if (boundsType != OBS_BOUNDS_NONE) {
		/* Fitted items use bounds — corner drag must update bounds, not scale. */
		obs_sceneitem_set_bounds(stretchItem, &size);
	} else {
		vec2 baseSize{};
		vec2_set(&baseSize, float(source_cx), float(source_cy));
		obs_sceneitem_crop crop{};
		obs_sceneitem_get_crop(stretchItem, &crop);
		baseSize.x -= float(crop.left + crop.right);
		baseSize.y -= float(crop.top + crop.bottom);
		if (baseSize.x < 1.0f)
			baseSize.x = 1.0f;
		if (baseSize.y < 1.0f)
			baseSize.y = 1.0f;
		vec2 scale{};
		vec2_div(&scale, &size, &baseSize);
		obs_sceneitem_set_scale(stretchItem, &scale);
	}

	pos3 = CalculateStretchPos(tl, br);
	vec3_transform(&pos3, &pos3, &itemToScreen);
	vec2 newPos{};
	vec2_set(&newPos, std::round(pos3.x), std::round(pos3.y));
	obs_sceneitem_set_pos(stretchItem, &newPos);
}

void ShortsDock::UpdateCursor(uint32_t flags)
{
	QWidget *target = preview ? static_cast<QWidget *>(preview) : static_cast<QWidget *>(this);
	if (flags == (ITEM_TOP | ITEM_LEFT) || flags == (ITEM_BOTTOM | ITEM_RIGHT))
		target->setCursor(Qt::SizeFDiagCursor);
	else if (flags == (ITEM_TOP | ITEM_RIGHT) || flags == (ITEM_BOTTOM | ITEM_LEFT))
		target->setCursor(Qt::SizeBDiagCursor);
	else if (flags & (ITEM_LEFT | ITEM_RIGHT))
		target->setCursor(Qt::SizeHorCursor);
	else if (flags & (ITEM_TOP | ITEM_BOTTOM))
		target->setCursor(Qt::SizeVerCursor);
	else
		target->setCursor(Qt::ArrowCursor);
}

void ShortsDock::RegisterHotkeys()
{
	hkShortClip = obs_hotkey_register_frontend("VerticalShorts.SaveShortClip", "Vertical Shorts: Save Short Clip",
						   HotkeyThunk, this);
	hkLongClip = obs_hotkey_register_frontend("VerticalShorts.SaveLongClip", "Vertical Shorts: Save Long Clip",
						  HotkeyThunk, this);
	hkStartRec = obs_hotkey_register_frontend("VerticalShorts.StartRecording", "Vertical Shorts: Start Vertical Recording",
						  HotkeyThunk, this);
	hkStopRec = obs_hotkey_register_frontend("VerticalShorts.StopRecording", "Vertical Shorts: Stop Vertical Recording",
						 HotkeyThunk, this);
	hkToggleRec = obs_hotkey_register_frontend("VerticalShorts.ToggleRecording", "Vertical Shorts: Toggle Vertical Recording",
						   HotkeyThunk, this);
	hkStartLive = obs_hotkey_register_frontend("VerticalShorts.StartLive", "Vertical Shorts: Start Vertical Live Output",
						   HotkeyThunk, this);
	hkStopLive = obs_hotkey_register_frontend("VerticalShorts.StopLive", "Vertical Shorts: Stop Vertical Live Output",
						  HotkeyThunk, this);
	hkSettings = obs_hotkey_register_frontend("VerticalShorts.OpenSettings", "Vertical Shorts: Open Settings", HotkeyThunk,
						  this);
}

void ShortsDock::UnregisterHotkeys()
{
	auto unreg = [](obs_hotkey_id &id) {
		if (id != OBS_INVALID_HOTKEY_ID) {
			obs_hotkey_unregister(id);
			id = OBS_INVALID_HOTKEY_ID;
		}
	};
	unreg(hkShortClip);
	unreg(hkLongClip);
	unreg(hkStartRec);
	unreg(hkStopRec);
	unreg(hkToggleRec);
	unreg(hkStartLive);
	unreg(hkStopLive);
	unreg(hkSettings);
}

void ShortsDock::SaveHotkeys(obs_data_t *data) const
{
	auto saveOne = [&](obs_hotkey_id id, const char *key) {
		obs_data_array_t *arr = obs_hotkey_save(id);
		if (arr) {
			obs_data_set_array(data, key, arr);
			obs_data_array_release(arr);
		}
	};
	saveOne(hkShortClip, "hotkey_short_clip");
	saveOne(hkLongClip, "hotkey_long_clip");
	saveOne(hkStartRec, "hotkey_start_rec");
	saveOne(hkStopRec, "hotkey_stop_rec");
	saveOne(hkToggleRec, "hotkey_toggle_rec");
	saveOne(hkStartLive, "hotkey_start_live");
	saveOne(hkStopLive, "hotkey_stop_live");
	saveOne(hkSettings, "hotkey_settings");
}

void ShortsDock::LoadHotkeys(obs_data_t *data)
{
	auto load = [&](obs_hotkey_id id, const char *key) {
		obs_data_array_t *arr = obs_data_get_array(data, key);
		if (arr) {
			obs_hotkey_load(id, arr);
			obs_data_array_release(arr);
		}
	};
	load(hkShortClip, "hotkey_short_clip");
	load(hkLongClip, "hotkey_long_clip");
	load(hkStartRec, "hotkey_start_rec");
	load(hkStopRec, "hotkey_stop_rec");
	load(hkToggleRec, "hotkey_toggle_rec");
	load(hkStartLive, "hotkey_start_live");
	load(hkStopLive, "hotkey_stop_live");
	load(hkSettings, "hotkey_settings");
}

void ShortsDock::HotkeyThunk(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	auto *dock = static_cast<ShortsDock *>(data);
	if (!dock || dock->clearing || dock->shuttingDown)
		return;
	QMetaObject::invokeMethod(
		dock,
		[dock, id]() {
			if (id == dock->hkShortClip)
				dock->HotkeySaveShortClip();
			else if (id == dock->hkLongClip)
				dock->HotkeySaveLongClip();
			else if (id == dock->hkStartRec)
				dock->HotkeyStartRecording();
			else if (id == dock->hkStopRec)
				dock->HotkeyStopRecording();
			else if (id == dock->hkToggleRec)
				dock->HotkeyToggleRecording();
			else if (id == dock->hkStartLive)
				dock->HotkeyStartLive();
			else if (id == dock->hkStopLive)
				dock->HotkeyStopLive();
			else if (id == dock->hkSettings)
				dock->HotkeyOpenSettings();
		},
		Qt::QueuedConnection);
}

void ShortsDock::HotkeySaveShortClip()
{
	OnShortClip();
}
void ShortsDock::HotkeySaveLongClip()
{
	OnLongClip();
}
void ShortsDock::HotkeyStartRecording()
{
	if (outputs && !outputs->IsRecording())
		OnRecord();
}
void ShortsDock::HotkeyStopRecording()
{
	if (outputs && outputs->IsRecording())
		OnRecord();
}
void ShortsDock::HotkeyToggleRecording()
{
	OnRecord();
}
void ShortsDock::HotkeyStartLive()
{
	if (outputs && !outputs->IsStreaming())
		OnGoLive();
}
void ShortsDock::HotkeyStopLive()
{
	if (outputs && outputs->IsStreaming())
		OnGoLive();
}
void ShortsDock::HotkeyOpenSettings()
{
	OnSettings();
}
