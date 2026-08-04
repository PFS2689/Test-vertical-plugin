#include "shorts-dock.hpp"
#include "credential-store.hpp"
#include "display-helpers.hpp"
#include "settings-dialog.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>
#include <QtMath>

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
	CreateView();
	EnsureDefaultVerticalScene();
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

	if (scene) {
		obs_source_t *src = obs_scene_get_source(scene);
		if (src)
			obs_source_dec_showing(src);
	}

	DestroyView();

	if (scene) {
		obs_scene_release(scene);
		scene = nullptr;
	}

	verticalTransition = nullptr;
	transitionPreviewScene = nullptr;
	verticalScenes.clear();
	sceneOrder.clear();

	obs_enter_graphics();
	gs_vertexbuffer_destroy(rectFill);
	rectFill = nullptr;
	obs_leave_graphics();
}

void ShortsDock::BuildUI()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);
	root->setSpacing(4);

	preview = new OBSQTDisplay(this);
	preview->setMinimumSize(120, 120);
	preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	previewEventFilter = BuildEventFilter();
	preview->installEventFilter(previewEventFilter.get());

	auto addDrawCallback = [this]() {
		obs_display_t *display = preview->GetDisplay();
		if (display)
			obs_display_add_draw_callback(display, DrawCallback, this);
	};
	connect(preview, &OBSQTDisplay::DisplayCreated, addDrawCallback);
	root->addWidget(preview, 1);

	controlsBar = new QWidget(this);
	controlsBar->setObjectName(QStringLiteral("vsControlsBar"));
	auto *bar = new QHBoxLayout(controlsBar);
	bar->setContentsMargins(0, 0, 0, 0);
	bar->setSpacing(6);

	goLiveBtn = new QPushButton(QString::fromUtf8("\U0001F7E2 ") + Translate("GoLive"), controlsBar);
	goLiveBtn->setCheckable(true);
	goLiveBtn->setToolTip(Translate("GoLiveTip"));
	goLiveBtn->setAccessibleName(Translate("GoLive"));
	connect(goLiveBtn, &QPushButton::clicked, this, &ShortsDock::OnGoLive);

	recordBtn = new QPushButton(QString::fromUtf8("\u23FA\uFE0F ") + Translate("Record"), controlsBar);
	recordBtn->setCheckable(true);
	recordBtn->setToolTip(Translate("RecordTip"));
	recordBtn->setAccessibleName(Translate("Record"));
	connect(recordBtn, &QPushButton::clicked, this, &ShortsDock::OnRecord);

	shortClipBtn = new QPushButton(QString::fromUtf8("\U0001F4F8 ") + Translate("ShortClip"), controlsBar);
	shortClipBtn->setToolTip(Translate("ShortClipTip"));
	shortClipBtn->setAccessibleName(Translate("ShortClip"));
	connect(shortClipBtn, &QPushButton::clicked, this, &ShortsDock::OnShortClip);

	longClipBtn = new QPushButton(QString::fromUtf8("\U0001F4F7 ") + Translate("LongClip"), controlsBar);
	longClipBtn->setToolTip(Translate("LongClipTip"));
	longClipBtn->setAccessibleName(Translate("LongClip"));
	connect(longClipBtn, &QPushButton::clicked, this, &ShortsDock::OnLongClip);

	settingsBtn = new QPushButton(QString::fromUtf8("\u2699\uFE0F ") + Translate("Settings"), controlsBar);
	settingsBtn->setToolTip(Translate("SettingsTip"));
	settingsBtn->setAccessibleName(Translate("Settings"));
	connect(settingsBtn, &QPushButton::clicked, this, &ShortsDock::OnSettings);

	bar->addWidget(goLiveBtn);
	bar->addWidget(recordBtn);
	bar->addWidget(shortClipBtn);
	bar->addWidget(longClipBtn);
	bar->addWidget(settingsBtn);
	bar->addStretch(1);
	root->addWidget(controlsBar);
}

void ShortsDock::ApplyCanvasFromSettings()
{
	vsp::CanvasSizeForPreset(settings.canvasPreset, settings.customWidth, settings.customHeight, verticalWidth,
				 verticalHeight);
}

void ShortsDock::RefreshVerticalWorkspace(bool force)
{
	UNUSED_PARAMETER(force);
	CreateView();
	if (outputs)
		outputs->SetVideo(video);
	EnsureDefaultVerticalScene();
	if (scene)
		SetActiveScene(scene, false);
	EmitSourceUiChanged();
}

void ShortsDock::EnsureDefaultVerticalScene()
{
	if (!verticalScenes.isEmpty())
		return;

	obs_scene_t *created = obs_scene_create_private("Vertical Scene");
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
	DestroyView();

	view = obs_view_create();
	if (!view) {
		blog(LOG_WARNING, "[obs-shorts-vertical] obs_view_create failed");
		return;
	}

	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi)) {
		blog(LOG_WARNING, "[obs-shorts-vertical] obs_get_video_info failed");
		return;
	}

	struct obs_video_info viewOvi = ovi;
	viewOvi.base_width = verticalWidth;
	viewOvi.base_height = verticalHeight;
	viewOvi.output_width = verticalWidth;
	viewOvi.output_height = verticalHeight;
	video = obs_view_add2(view, &viewOvi);
	if (!video)
		blog(LOG_WARNING, "[obs-shorts-vertical] obs_view_add2 failed");
}

void ShortsDock::DestroyView()
{
	if (view) {
		obs_view_remove(view);
		obs_view_destroy(view);
		view = nullptr;
		video = nullptr;
	}
}

void ShortsDock::SetCanvasSize(uint32_t width, uint32_t height)
{
	if (width < 2)
		width = 2;
	if (height < 2)
		height = 2;
	if (verticalWidth == width && verticalHeight == height && view && video)
		return;
	verticalWidth = width;
	verticalHeight = height;
	CreateView();
	if (outputs)
		outputs->SetVideo(video);
	if (view && scene)
		obs_view_set_source(view, 0, obs_scene_get_source(scene));
}

void ShortsDock::SetActiveScene(obs_scene_t *newScene, bool withTransition)
{
	if (scene == newScene)
		return;

	obs_source_t *oldSrc = scene ? obs_scene_get_source(scene) : nullptr;
	obs_source_t *newSrc = newScene ? obs_scene_get_source(newScene) : nullptr;

	if (scene) {
		if (oldSrc)
			obs_source_dec_showing(oldSrc);
		obs_scene_release(scene);
		scene = nullptr;
	}

	if (newScene) {
		scene = obs_scene_get_ref(newScene);
		if (scene) {
			obs_source_t *cur = obs_scene_get_source(scene);
			if (cur)
				obs_source_inc_showing(cur);
		}
	}

	if (view) {
		if (withTransition && oldSrc && newSrc && verticalTransitionDurationMs > 0) {
			obs_source_t *tr = EnsureVerticalTransitionSource(verticalTransitionName);
			if (tr) {
				obs_transition_set(tr, oldSrc);
				obs_view_set_source(view, 0, tr);
				obs_transition_start(tr, OBS_TRANSITION_MODE_AUTO, verticalTransitionDurationMs, newSrc);
			} else {
				obs_view_set_source(view, 0, newSrc);
			}
		} else {
			obs_view_set_source(view, 0, newSrc);
		}
	}

	EmitSourceUiChanged();
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

	obs_scene_t *created = obs_scene_create_private(name.trimmed().toUtf8().constData());
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

	if (scene && ActiveSceneUuid() == uuid)
		SetActiveScene(nullptr, false);

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

	obs_scene_t *dup = obs_scene_duplicate(srcScene, name.trimmed().toUtf8().constData(), OBS_SCENE_DUP_PRIVATE_REFS);
	if (!dup)
		return;
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
	obs_source_set_name(src, name.trimmed().toUtf8().constData());
	EmitSceneUiChanged();
}

void ShortsDock::PopulateSourcesList(QListWidget *list)
{
	if (!list)
		return;
	list->clear();
	if (!scene)
		return;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
			auto *lw = static_cast<QListWidget *>(param);
			obs_source_t *source = obs_sceneitem_get_source(item);
			QString label = QString::fromUtf8(obs_source_get_name(source));
			if (!obs_sceneitem_visible(item))
				label += QStringLiteral(" [hid]");
			if (obs_sceneitem_locked(item))
				label += QStringLiteral(" [lock]");
			auto *row = new QListWidgetItem(label);
			row->setData(Qt::UserRole, QVariant::fromValue((qint64)obs_sceneitem_get_id(item)));
			if (obs_sceneitem_selected(item))
				row->setSelected(true);
			lw->addItem(row);
			return true;
		},
		list);
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
	if (!scene) {
		EnsureDefaultVerticalScene();
		if (!scene)
			return;
	}

	std::vector<std::string> names;
	std::vector<OBSSource> sources;
	struct EnumData {
		std::vector<std::string> *names;
		std::vector<OBSSource> *sources;
	} data{&names, &sources};

	obs_enum_sources(
		[](void *param, obs_source_t *source) -> bool {
			auto *d = static_cast<EnumData *>(param);
			uint32_t flags = obs_source_get_output_flags(source);
			if ((flags & OBS_SOURCE_VIDEO) == 0)
				return true;
			if (obs_source_is_group(source))
				return true;
			d->names->emplace_back(obs_source_get_name(source));
			d->sources->emplace_back(source);
			return true;
		},
		&data);

	if (names.empty()) {
		QMessageBox::information(this, Translate("AddSource"), Translate("NoSources"));
		return;
	}

	QStringList items;
	for (const auto &n : names)
		items << QString::fromUtf8(n.c_str());

	bool ok = false;
	QString chosen =
		QInputDialog::getItem(this, Translate("AddSource"), Translate("SelectSource"), items, 0, false, &ok);
	if (!ok || chosen.isEmpty())
		return;

	for (size_t i = 0; i < names.size(); i++) {
		if (chosen != QString::fromUtf8(names[i].c_str()))
			continue;
		/* Add into the vertical scene only — never touch horizontal production. */
		obs_scene_add(scene, sources[i]);
		break;
	}
	EmitSourceUiChanged();
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
	for (obs_sceneitem_t *item : selected)
		obs_sceneitem_remove(item);
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

void ShortsDock::RequestSourceProperties()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_source_t *source = obs_sceneitem_get_source(selected.front());
	if (source)
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

void ShortsDock::RequestFitToScreen()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_sceneitem_t *item = selected.front();
	obs_source_t *source = obs_sceneitem_get_source(item);
	uint32_t sw = std::max(1u, obs_source_get_width(source));
	uint32_t sh = std::max(1u, obs_source_get_height(source));
	float scale = std::min(float(verticalWidth) / float(sw), float(verticalHeight) / float(sh));
	vec2 s;
	vec2_set(&s, scale, scale);
	obs_sceneitem_set_scale(item, &s);
	vec2 pos;
	vec2_set(&pos, (float(verticalWidth) - float(sw) * scale) * 0.5f,
		 (float(verticalHeight) - float(sh) * scale) * 0.5f);
	obs_sceneitem_set_pos(item, &pos);
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
	obs_sceneitem_t *item = selected.front();
	obs_source_t *source = obs_sceneitem_get_source(item);
	uint32_t sw = std::max(1u, obs_source_get_width(source));
	uint32_t sh = std::max(1u, obs_source_get_height(source));
	vec2 s;
	vec2_set(&s, float(verticalWidth) / float(sw), float(verticalHeight) / float(sh));
	obs_sceneitem_set_scale(item, &s);
	vec2 pos;
	vec2_set(&pos, 0.0f, 0.0f);
	obs_sceneitem_set_pos(item, &pos);
	emit verticalTransformChanged();
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
	ItemTransform info = ReadItemTransform(item);
	obs_source_t *source = obs_sceneitem_get_source(item);
	float w = float(obs_source_get_width(source)) * info.scale.x;
	float h = float(obs_source_get_height(source)) * info.scale.y;
	vec2 pos;
	vec2_set(&pos, (float(verticalWidth) - w) * 0.5f, (float(verticalHeight) - h) * 0.5f);
	obs_sceneitem_set_pos(item, &pos);
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
	obs_sceneitem_t *item = selected.front();
	vec2 one;
	vec2_set(&one, 1.0f, 1.0f);
	vec2 zero;
	vec2_set(&zero, 0.0f, 0.0f);
	obs_sceneitem_set_pos(item, &zero);
	obs_sceneitem_set_scale(item, &one);
	obs_sceneitem_set_rot(item, 0.0f);
	obs_sceneitem_crop crop = {0, 0, 0, 0};
	obs_sceneitem_set_crop(item, &crop);
	emit verticalTransformChanged();
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
	if (!scene || !view)
		return;
	obs_source_t *cur = obs_scene_get_source(scene);
	obs_source_t *tr = EnsureVerticalTransitionSource(verticalTransitionName);
	if (!tr || !cur)
		return;
	obs_transition_set(tr, cur);
	obs_view_set_source(view, 0, tr);
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

void ShortsDock::OpenSettingsStreaming(bool focusStreaming)
{
	QStringList names, uuids;
	CollectSceneLists(names, uuids);
	const QString statusText = automation ? automation->StatusText() : QString();
	const auto status = automation ? automation->Status() : vsp::AutomationStatus::Disabled;

	SettingsDialog dlg(settings, outputs.get(), names, uuids, status, statusText, this);
	if (focusStreaming)
		dlg.FocusStreamingTab();
	if (dlg.exec() != QDialog::Accepted)
		return;

	const uint32_t oldW = verticalWidth;
	const uint32_t oldH = verticalHeight;
	settings = dlg.result();
	ApplyCanvasFromSettings();

	bool restartBuffer = false;
	if (outputs)
		outputs->ApplySettings(settings, &restartBuffer);

	if (dlg.WantsAutomationReset() && automation)
		automation->ResetRuntimeState();
	if (automation)
		automation->ApplySettings(settings);

	if (verticalWidth != oldW || verticalHeight != oldH) {
		CreateView();
		if (outputs)
			outputs->SetVideo(video);
		if (view && scene)
			obs_view_set_source(view, 0, obs_scene_get_source(scene));
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
}

void ShortsDock::OnStreamingChanged(bool active)
{
	if (!goLiveBtn)
		return;
	goLiveBtn->setChecked(active);
	goLiveBtn->setText(QString::fromUtf8(active ? "\U0001F534 " : "\U0001F7E2 ") +
			   QString::fromUtf8(Translate(active ? "StopGoLive" : "GoLive")));
	goLiveBtn->setToolTip(Translate(active ? "StopGoLiveTip" : "GoLiveTip"));
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
	recordBtn->setText(QString::fromUtf8("\u23FA\uFE0F ") +
			   QString::fromUtf8(Translate(active ? "StopRecord" : "Record")));
	recordBtn->setToolTip(Translate(active ? "StopRecordTip" : "RecordTip"));
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
			dock->EnsureBufferIfConfigured();
			dock->EmitSceneUiChanged();
			emit dock->verticalTransitionsChanged();
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

void ShortsDock::DrawCallback(void *data, uint32_t cx, uint32_t cy)
{
	static_cast<ShortsDock *>(data)->DrawPreview(cx, cy);
}

void ShortsDock::DrawPreview(uint32_t cx, uint32_t cy)
{
	if (!scene)
		return;

	const uint32_t canvasW = verticalWidth;
	const uint32_t canvasH = verticalHeight;
	UpdatePreviewScale((int)cx, (int)cy);

	gs_viewport_push();
	gs_projection_push();

	gs_ortho(0.0f, (float)canvasW, 0.0f, (float)canvasH, -100.0f, 100.0f);
	gs_set_viewport(previewX, previewY, (int)(previewScale * canvasW), (int)(previewScale * canvasH));

	obs_source_t *source = obs_scene_get_source(scene);
	if (source)
		obs_source_video_render(source);

	DrawSceneEditing();

	gs_projection_pop();
	gs_viewport_pop();
}

void ShortsDock::DrawSceneEditing()
{
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

	QMenu menu(this);
	menu.addAction(Translate("FitToScreen"), this, &ShortsDock::RequestFitToScreen);
	menu.addAction(Translate("StretchToScreen"), this, &ShortsDock::RequestStretchToScreen);
	menu.addAction(Translate("CenterToScreen"), this, &ShortsDock::RequestCenterToScreen);
	menu.addAction(Translate("ResetTransform"), this, &ShortsDock::RequestResetTransform);
	menu.addSeparator();
	menu.addAction(Translate("SourceProperties"), this, &ShortsDock::RequestSourceProperties);
	menu.addAction(Translate("SourceFilters"), this, &ShortsDock::RequestSourceFilters);
	menu.addSeparator();

	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	const bool has = !selected.empty();

	QAction *lockAction = menu.addAction(Translate("ToggleLock"), this, &ShortsDock::RequestToggleSourceLock);
	lockAction->setEnabled(has);
	QAction *visAction = menu.addAction(Translate("ToggleVisible"), this, &ShortsDock::RequestToggleSourceVisible);
	visAction->setEnabled(has);

	menu.exec(globalPos);
}

vec2 ShortsDock::GetMouseEventPos(QMouseEvent *event)
{
	float pixelRatio = (float)preview->devicePixelRatioF();
	float scale = pixelRatio * previewScale;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	const float mx = (float)event->position().x();
	const float my = (float)event->position().y();
#else
	const float mx = (float)event->localPos().x();
	const float my = (float)event->localPos().y();
#endif
	vec2 pos;
	vec2_set(&pos, (mx - (float)previewX / pixelRatio) * pixelRatio / scale,
		 (my - (float)previewY / pixelRatio) * pixelRatio / scale);
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

	ItemTransform info = ReadItemTransform(item);

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

	float radius = HANDLE_RADIUS * 1.5f / previewScale;
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

	obs_sceneitem_get_crop(item, &startCrop);
	obs_sceneitem_get_pos(item, &startItemPos);

	obs_source_t *source = obs_sceneitem_get_source(item);
	vec2_set(&cropSize, float(obs_source_get_width(source) - startCrop.left - startCrop.right),
		 float(obs_source_get_height(source) - startCrop.top - startCrop.bottom));

	info = ReadItemTransform(item);
	vec2_set(&stretchItemSize, cropSize.x * info.scale.x, cropSize.y * info.scale.y);

	matrix4_identity(&itemToScreen);
	matrix4_translate3f(&itemToScreen, &itemToScreen, startItemPos.x, startItemPos.y, 0.0f);
	matrix4_rotate_aa4f(&itemToScreen, &itemToScreen, 0.0f, 0.0f, 1.0f, RAD(info.rot));
	matrix4_inv(&screenToItem, &itemToScreen);
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

	vec3 mouse;
	vec3_set(&mouse, pos.x, pos.y, 0.0f);
	vec3_transform(&mouse, &mouse, &screenToItem);

	vec2 scale;

	uint32_t handle = (uint32_t)stretchHandle;
	float width = stretchItemSize.x;
	float height = stretchItemSize.y;

	float tl_x = (handle & ITEM_RIGHT) ? 0.0f : mouse.x;
	float tl_y = (handle & ITEM_BOTTOM) ? 0.0f : mouse.y;
	float br_x = (handle & ITEM_RIGHT) ? mouse.x : width;
	float br_y = (handle & ITEM_BOTTOM) ? mouse.y : height;

	if (!(handle & (ITEM_LEFT | ITEM_RIGHT))) {
		tl_x = 0.0f;
		br_x = width;
	}
	if (!(handle & (ITEM_TOP | ITEM_BOTTOM))) {
		tl_y = 0.0f;
		br_y = height;
	}

	if (br_x < tl_x)
		std::swap(br_x, tl_x);
	if (br_y < tl_y)
		std::swap(br_y, tl_y);

	float newW = std::max(1.0f, br_x - tl_x);
	float newH = std::max(1.0f, br_y - tl_y);

	if (cropSize.x > 0.0f && cropSize.y > 0.0f)
		vec2_set(&scale, newW / cropSize.x, newH / cropSize.y);
	else
		vec2_set(&scale, 1.0f, 1.0f);

	obs_sceneitem_set_scale(stretchItem, &scale);

	vec3 newPos;
	vec3_set(&newPos, tl_x, tl_y, 0.0f);
	vec3_transform(&newPos, &newPos, &itemToScreen);
	vec2 p;
	vec2_set(&p, newPos.x, newPos.y);
	obs_sceneitem_set_pos(stretchItem, &p);
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
