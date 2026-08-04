#include "shorts-dock.hpp"
#include "audio-mixer-panel.hpp"
#include "display-helpers.hpp"
#include "settings-dialog.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
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
	setMinimumWidth(420);
	setMinimumHeight(360);

	settings.recordingPath = vsp::DefaultRecordingPath();
	vsp::CanvasSizeForPreset(settings.canvasPreset, settings.customWidth, settings.customHeight, verticalWidth,
				 verticalHeight);

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
	if (outputs) {
		outputs->SetVideo(video);
		outputs->ApplySettings(settings);
	}
	if (automation)
		automation->ApplySettings(settings);

	obs_frontend_add_event_callback(FrontendEvent, this);
	RegisterHotkeys();

	RefreshVerticalWorkspace(true);
	RefreshScenesList();
	RefreshTransitions();
	RefreshMixer();

	/* Start clip buffer promptly when configured — no artificial multi-second delay. */
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

	verticalMirrors.clear();

	obs_enter_graphics();
	gs_vertexbuffer_destroy(rectFill);
	rectFill = nullptr;
	obs_leave_graphics();
}

void ShortsDock::BuildUI()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(6, 6, 6, 6);
	root->setSpacing(6);

	auto *workspaceHint = new QLabel(Translate("VerticalWorkspaceHint"), this);
	workspaceHint->setWordWrap(true);
	workspaceHint->setStyleSheet(QStringLiteral("color: #aaa; font-size: 11px;"));
	root->addWidget(workspaceHint);

	autoIndicator = new QLabel(this);
	autoIndicator->setVisible(false);
	autoIndicator->setStyleSheet(QStringLiteral("color: #f0c040; font-size: 11px;"));
	root->addWidget(autoIndicator);

	auto *splitter = new QSplitter(Qt::Horizontal, this);

	auto *left = new QWidget(splitter);
	auto *leftLayout = new QVBoxLayout(left);
	leftLayout->setContentsMargins(0, 0, 0, 0);
	leftLayout->setSpacing(4);

	leftLayout->addWidget(new QLabel(Translate("Scenes"), left));
	scenesList = new QListWidget(left);
	scenesList->setSelectionMode(QAbstractItemView::SingleSelection);
	connect(scenesList, &QListWidget::itemSelectionChanged, this, &ShortsDock::OnSceneSelectionChanged);
	leftLayout->addWidget(scenesList, 1);

	auto *sceneBtns = new QHBoxLayout();
	auto *addSceneBtn = MakeToolButton(left, QStringLiteral("+"), Translate("AddScene"));
	auto *removeSceneBtn = MakeToolButton(left, QStringLiteral("\u2212"), Translate("RemoveScene"));
	auto *dupSceneBtn = MakeToolButton(left, QStringLiteral("Dup"), Translate("DuplicateScene"));
	auto *renameSceneBtn = MakeToolButton(left, QStringLiteral("Ren"), Translate("RenameScene"));
	connect(addSceneBtn, &QToolButton::clicked, this, &ShortsDock::OnAddScene);
	connect(removeSceneBtn, &QToolButton::clicked, this, &ShortsDock::OnRemoveScene);
	connect(dupSceneBtn, &QToolButton::clicked, this, &ShortsDock::OnDuplicateScene);
	connect(renameSceneBtn, &QToolButton::clicked, this, &ShortsDock::OnRenameScene);
	sceneBtns->addWidget(addSceneBtn);
	sceneBtns->addWidget(removeSceneBtn);
	sceneBtns->addWidget(dupSceneBtn);
	sceneBtns->addWidget(renameSceneBtn);
	sceneBtns->addStretch(1);
	leftLayout->addLayout(sceneBtns);

	leftLayout->addWidget(new QLabel(Translate("Sources"), left));
	sourcesList = new QListWidget(left);
	sourcesList->setSelectionMode(QAbstractItemView::SingleSelection);
	connect(sourcesList, &QListWidget::itemSelectionChanged, this, &ShortsDock::OnSourceSelectionChanged);
	leftLayout->addWidget(sourcesList, 1);

	auto *sourceBtns = new QHBoxLayout();
	auto *addSrcBtn = MakeToolButton(left, QStringLiteral("+"), Translate("AddSource"));
	auto *removeSrcBtn = MakeToolButton(left, QStringLiteral("\u2212"), Translate("RemoveSource"));
	auto *visBtn = MakeToolButton(left, QStringLiteral("Vis"), Translate("ToggleVisible"));
	auto *lockBtn = MakeToolButton(left, QStringLiteral("Lock"), Translate("ToggleLock"));
	auto *propsBtn = MakeToolButton(left, QStringLiteral("Prop"), Translate("SourceProperties"));
	auto *filtersBtn = MakeToolButton(left, QStringLiteral("Filt"), Translate("SourceFilters"));
	auto *upBtn = MakeToolButton(left, QStringLiteral("Up"), Translate("MoveSourceUp"));
	auto *downBtn = MakeToolButton(left, QStringLiteral("Dn"), Translate("MoveSourceDown"));
	connect(addSrcBtn, &QToolButton::clicked, this, &ShortsDock::OnAddSource);
	connect(removeSrcBtn, &QToolButton::clicked, this, &ShortsDock::OnRemoveSource);
	connect(visBtn, &QToolButton::clicked, this, &ShortsDock::OnToggleSourceVisible);
	connect(lockBtn, &QToolButton::clicked, this, &ShortsDock::OnToggleSourceLock);
	connect(propsBtn, &QToolButton::clicked, this, &ShortsDock::OnSourceProperties);
	connect(filtersBtn, &QToolButton::clicked, this, &ShortsDock::OnSourceFilters);
	connect(upBtn, &QToolButton::clicked, this, &ShortsDock::OnSourceMoveUp);
	connect(downBtn, &QToolButton::clicked, this, &ShortsDock::OnSourceMoveDown);
	for (auto *b : {addSrcBtn, removeSrcBtn, visBtn, lockBtn, propsBtn, filtersBtn, upBtn, downBtn})
		sourceBtns->addWidget(b);
	sourceBtns->addStretch(1);
	leftLayout->addLayout(sourceBtns);

	leftLayout->addWidget(new QLabel(Translate("Transform"), left));
	auto *form = new QGridLayout();
	posXSpin = new QDoubleSpinBox(left);
	posYSpin = new QDoubleSpinBox(left);
	sizeWSpin = new QDoubleSpinBox(left);
	sizeHSpin = new QDoubleSpinBox(left);
	rotSpin = new QDoubleSpinBox(left);
	for (auto *spin : {posXSpin, posYSpin, sizeWSpin, sizeHSpin}) {
		spin->setRange(-100000.0, 100000.0);
		spin->setDecimals(1);
		spin->setSingleStep(1.0);
		connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
			&ShortsDock::OnTransformEdited);
	}
	rotSpin->setRange(-360.0, 360.0);
	rotSpin->setDecimals(1);
	rotSpin->setSingleStep(1.0);
	connect(rotSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShortsDock::OnTransformEdited);

	form->addWidget(new QLabel(Translate("PositionX"), left), 0, 0);
	form->addWidget(posXSpin, 0, 1);
	form->addWidget(new QLabel(Translate("PositionY"), left), 0, 2);
	form->addWidget(posYSpin, 0, 3);
	form->addWidget(new QLabel(Translate("SizeW"), left), 1, 0);
	form->addWidget(sizeWSpin, 1, 1);
	form->addWidget(new QLabel(Translate("SizeH"), left), 1, 2);
	form->addWidget(sizeHSpin, 1, 3);
	form->addWidget(new QLabel(Translate("Rotation"), left), 2, 0);
	form->addWidget(rotSpin, 2, 1);
	leftLayout->addLayout(form);

	auto *transformButtons = new QHBoxLayout();
	auto *fitBtn = new QPushButton(Translate("FitToScreen"), left);
	auto *stretchBtn = new QPushButton(Translate("StretchToScreen"), left);
	auto *centerBtn = new QPushButton(Translate("CenterToScreen"), left);
	auto *resetBtn = new QPushButton(Translate("ResetTransform"), left);
	connect(fitBtn, &QPushButton::clicked, this, &ShortsDock::OnFitToScreen);
	connect(stretchBtn, &QPushButton::clicked, this, &ShortsDock::OnStretchToScreen);
	connect(centerBtn, &QPushButton::clicked, this, &ShortsDock::OnCenterToScreen);
	connect(resetBtn, &QPushButton::clicked, this, &ShortsDock::OnResetTransform);
	transformButtons->addWidget(fitBtn);
	transformButtons->addWidget(stretchBtn);
	transformButtons->addWidget(centerBtn);
	transformButtons->addWidget(resetBtn);
	leftLayout->addLayout(transformButtons);

	left->setMinimumWidth(240);
	splitter->addWidget(left);

	auto *center = new QWidget(splitter);
	auto *centerLayout = new QGridLayout(center);
	centerLayout->setContentsMargins(0, 0, 0, 0);
	centerLayout->setSpacing(0);

	preview = new OBSQTDisplay(center);
	preview->setMinimumSize(160, 160);
	preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	previewEventFilter = BuildEventFilter();
	preview->installEventFilter(previewEventFilter.get());

	auto addDrawCallback = [this]() {
		obs_display_t *display = preview->GetDisplay();
		if (display)
			obs_display_add_draw_callback(display, DrawCallback, this);
	};
	connect(preview, &OBSQTDisplay::DisplayCreated, addDrawCallback);
	centerLayout->addWidget(preview, 0, 0);

	controlsOverlay = new QWidget(center);
	controlsOverlay->setObjectName("vsControlsOverlay");
	auto *overlayLayout = new QHBoxLayout(controlsOverlay);
	overlayLayout->setContentsMargins(8, 8, 8, 8);
	overlayLayout->setSpacing(6);

	goLiveBtn = new QPushButton(QString::fromUtf8("\U0001F7E2 ") + Translate("GoLive"), controlsOverlay);
	goLiveBtn->setCheckable(true);
	goLiveBtn->setToolTip(Translate("GoLiveTip"));
	connect(goLiveBtn, &QPushButton::clicked, this, &ShortsDock::OnGoLive);

	recordBtn = new QPushButton(QString::fromUtf8("\u23FA\uFE0F ") + Translate("Record"), controlsOverlay);
	recordBtn->setCheckable(true);
	recordBtn->setToolTip(Translate("RecordTip"));
	connect(recordBtn, &QPushButton::clicked, this, &ShortsDock::OnRecord);

	shortClipBtn = new QPushButton(QString::fromUtf8("\U0001F4F8 ") + Translate("ShortClip"), controlsOverlay);
	shortClipBtn->setAccessibleName(Translate("ShortClip"));
	shortClipBtn->setToolTip(Translate("ShortClipTip"));
	connect(shortClipBtn, &QPushButton::clicked, this, &ShortsDock::OnShortClip);

	longClipBtn = new QPushButton(QString::fromUtf8("\U0001F4F7 ") + Translate("LongClip"), controlsOverlay);
	longClipBtn->setAccessibleName(Translate("LongClip"));
	longClipBtn->setToolTip(Translate("LongClipTip"));
	connect(longClipBtn, &QPushButton::clicked, this, &ShortsDock::OnLongClip);

	settingsBtn = new QPushButton(QString::fromUtf8("\u2699\uFE0F ") + Translate("Settings"), controlsOverlay);
	settingsBtn->setAccessibleName(Translate("Settings"));
	settingsBtn->setToolTip(Translate("SettingsTip"));
	connect(settingsBtn, &QPushButton::clicked, this, &ShortsDock::OnSettings);

	goLiveBtn->setAccessibleName(Translate("GoLive"));
	recordBtn->setAccessibleName(Translate("Record"));

	overlayLayout->addWidget(goLiveBtn);
	overlayLayout->addWidget(recordBtn);
	overlayLayout->addWidget(shortClipBtn);
	overlayLayout->addWidget(longClipBtn);
	overlayLayout->addWidget(settingsBtn);
	controlsOverlay->setStyleSheet(
		QStringLiteral("QWidget#vsControlsOverlay { background: rgba(0,0,0,140); border-radius: 6px; }"
			       "QPushButton { padding: 4px 8px; }"));
	centerLayout->addWidget(controlsOverlay, 0, 0, Qt::AlignBottom | Qt::AlignRight);
	splitter->addWidget(center);

	auto *right = new QWidget(splitter);
	auto *rightLayout = new QVBoxLayout(right);
	rightLayout->setContentsMargins(0, 0, 0, 0);
	rightLayout->setSpacing(4);

	rightLayout->addWidget(new QLabel(Translate("AudioMixer"), right));
	auto *mixerScroll = new QScrollArea(right);
	mixerScroll->setWidgetResizable(true);
	mixerScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	mixerPanel = new AudioMixerPanel(mixerScroll);
	mixerScroll->setWidget(mixerPanel);
	rightLayout->addWidget(mixerScroll, 1);

	bufferStatusLabel = new QLabel(Translate("BufferStopped"), right);
	bufferStatusLabel->setWordWrap(true);
	bufferStatusLabel->setStyleSheet(QStringLiteral("color: #bbb; font-size: 11px;"));
	rightLayout->addWidget(bufferStatusLabel);

	rightLayout->addWidget(new QLabel(Translate("Transitions"), right));
	transitionCombo = new QComboBox(right);
	connect(transitionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&ShortsDock::OnTransitionChanged);
	rightLayout->addWidget(transitionCombo);

	auto *durRow = new QHBoxLayout();
	durRow->addWidget(new QLabel(Translate("TransitionDuration"), right));
	transitionDuration = new QSpinBox(right);
	transitionDuration->setRange(0, 10000);
	transitionDuration->setSingleStep(50);
	transitionDuration->setValue(obs_frontend_get_transition_duration());
	connect(transitionDuration, QOverload<int>::of(&QSpinBox::valueChanged), this,
		&ShortsDock::OnTransitionDurationChanged);
	durRow->addWidget(transitionDuration, 1);
	rightLayout->addLayout(durRow);

	right->setMinimumWidth(180);
	splitter->addWidget(right);

	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	splitter->setStretchFactor(2, 0);
	splitter->setSizes({280, 480, 220});
	root->addWidget(splitter, 1);
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

	obs_source_t *cur = obs_frontend_get_current_scene();
	if (cur) {
		obs_scene_t *mirror = EnsureVerticalMirror(cur);
		SyncMirrorFromMain(mirror, obs_scene_from_source(cur));
		SetActiveScene(mirror, true);
		obs_source_release(cur);
	} else {
		SetActiveScene(nullptr, false);
	}

	RefreshSourcesList();
	RefreshTransformControls();
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
	memset(&ovi, 0, sizeof(ovi));
	if (!obs_get_video_info(&ovi)) {
		blog(LOG_WARNING, "[obs-shorts-vertical] Video not ready yet; using defaults");
		ovi.fps_num = 30;
		ovi.fps_den = 1;
	}

	ovi.base_width = ActiveCanvasWidth();
	ovi.base_height = ActiveCanvasHeight();
	ovi.output_width = ActiveCanvasWidth();
	ovi.output_height = ActiveCanvasHeight();

	video = obs_view_add2(view, &ovi);
	if (!video)
		video = obs_view_add(view);

	if (!video)
		blog(LOG_WARNING, "[obs-shorts-vertical] Could not attach video to vertical view");

	if (scene && view)
		obs_view_set_source(view, 0, obs_scene_get_source(scene));
}

void ShortsDock::DestroyView()
{
	if (view) {
		obs_view_set_source(view, 0, nullptr);
		if (video) {
			obs_view_remove(view);
			video = nullptr;
		}
		obs_view_destroy(view);
		view = nullptr;
	}
}

void ShortsDock::SetCanvasSize(uint32_t width, uint32_t height)
{
	if (width < 160 || height < 160)
		return;

	verticalWidth = width;
	verticalHeight = height;
	settings.customWidth = width;
	settings.customHeight = height;

	CreateView();
	if (outputs)
		outputs->SetVideo(video);
}

void ShortsDock::SetActiveScene(obs_scene_t *newScene, bool isVerticalMirror)
{
	if (scene == newScene && sceneIsMirror == isVerticalMirror)
		return;

	if (scene) {
		obs_source_t *prev = obs_scene_get_source(scene);
		if (prev)
			obs_source_dec_showing(prev);
		obs_scene_release(scene);
		scene = nullptr;
	}

	sceneIsMirror = isVerticalMirror;

	if (newScene) {
		scene = obs_scene_get_ref(newScene);
		if (scene) {
			obs_source_t *cur = obs_scene_get_source(scene);
			if (cur)
				obs_source_inc_showing(cur);
		}
	}

	if (view)
		obs_view_set_source(view, 0, scene ? obs_scene_get_source(scene) : nullptr);

	RefreshSourcesList();
	RefreshTransformControls();
}

obs_scene_t *ShortsDock::EnsureVerticalMirror(obs_source_t *mainSceneSource)
{
	if (!mainSceneSource)
		return nullptr;

	const char *uuid = obs_source_get_uuid(mainSceneSource);
	if (!uuid || !*uuid)
		return nullptr;

	const QString key = QString::fromUtf8(uuid);
	auto it = verticalMirrors.find(key);
	if (it != verticalMirrors.end()) {
		obs_scene_t *existing = it.value();
		if (existing) {
			const QString expected =
				QStringLiteral("VS | %1").arg(QString::fromUtf8(obs_source_get_name(mainSceneSource)));
			obs_source_t *mirrorSrc = obs_scene_get_source(existing);
			if (mirrorSrc && expected != QString::fromUtf8(obs_source_get_name(mirrorSrc)))
				obs_source_set_name(mirrorSrc, expected.toUtf8().constData());
			return existing;
		}
	}

	const QString name = QStringLiteral("VS | %1").arg(QString::fromUtf8(obs_source_get_name(mainSceneSource)));
	obs_scene_t *mirror = obs_scene_create_private(name.toUtf8().constData());
	if (!mirror)
		return nullptr;

	verticalMirrors.insert(key, mirror);
	obs_scene_release(mirror);
	return verticalMirrors.value(key);
}

void ShortsDock::SyncMirrorFromMain(obs_scene_t *mirror, obs_scene_t *mainScene)
{
	if (!mirror || !mainScene)
		return;

	std::vector<obs_source_t *> mainSources;
	CollectSourcesCtx collect{&mainSources};
	obs_scene_enum_items(mainScene, CollectMainSources, &collect);

	std::vector<obs_sceneitem_t *> toRemove;
	RemoveMissingCtx removeCtx{&mainSources, &toRemove};
	obs_scene_enum_items(mirror, CollectMissingMirrorItems, &removeCtx);
	for (obs_sceneitem_t *item : toRemove)
		obs_sceneitem_remove(item);

	obs_scene_enum_items(
		mainScene,
		[](obs_scene_t *, obs_sceneitem_t *mainItem, void *param) -> bool {
			auto *mirrorScene = static_cast<obs_scene_t *>(param);
			obs_source_t *src = obs_sceneitem_get_source(mainItem);
			if (!src || MirrorHasSource(mirrorScene, src))
				return true;

			obs_sceneitem_t *added = obs_scene_add(mirrorScene, src);
			if (!added)
				return true;

			/* Seed new mirror items from main transform once; later edits stay local. */
			vec2 pos, scale;
			obs_sceneitem_get_pos(mainItem, &pos);
			obs_sceneitem_get_scale(mainItem, &scale);
			obs_sceneitem_set_pos(added, &pos);
			obs_sceneitem_set_scale(added, &scale);
			obs_sceneitem_set_rot(added, obs_sceneitem_get_rot(mainItem));
			obs_sceneitem_crop crop;
			obs_sceneitem_get_crop(mainItem, &crop);
			obs_sceneitem_set_crop(added, &crop);
			obs_sceneitem_set_visible(added, obs_sceneitem_visible(mainItem));
			return true;
		},
		mirror);
}

void ShortsDock::OnSceneSelectionChanged()
{
	if (loadingSettings || clearing)
		return;

	QListWidgetItem *item = scenesList->currentItem();
	if (!item)
		return;

	const QString uuid = item->data(Qt::UserRole).toString();
	obs_source_t *mainSrc = FindFrontendSceneByUuid(uuid);
	if (!mainSrc)
		return;

	obs_frontend_set_current_scene(mainSrc);

	obs_scene_t *mirror = EnsureVerticalMirror(mainSrc);
	SyncMirrorFromMain(mirror, obs_scene_from_source(mainSrc));
	SetActiveScene(mirror, true);

	const QString sceneName = QString::fromUtf8(obs_source_get_name(mainSrc));
	if (automation)
		automation->OnSceneChanged(uuid, sceneName);

	obs_source_release(mainSrc);
}

void ShortsDock::OnAddScene()
{
	bool ok = false;
	QString name = QInputDialog::getText(this, Translate("AddScene"), Translate("NewSceneName"), QLineEdit::Normal,
					     Translate("NewSceneName"), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;

	obs_scene_t *created = obs_scene_create(name.trimmed().toUtf8().constData());
	if (!created)
		return;

	obs_source_t *src = obs_scene_get_source(created);
	obs_frontend_set_current_scene(src);
	obs_scene_release(created);
	RefreshScenesList();
}

void ShortsDock::OnRemoveScene()
{
	QListWidgetItem *item = scenesList->currentItem();
	if (!item)
		return;

	if (QMessageBox::question(this, Translate("RemoveScene"), Translate("ConfirmRemoveScene")) != QMessageBox::Yes)
		return;

	obs_source_t *src = FindFrontendSceneByUuid(item->data(Qt::UserRole).toString());
	if (!src)
		return;

	const char *uuid = obs_source_get_uuid(src);
	if (uuid)
		verticalMirrors.remove(QString::fromUtf8(uuid));

	obs_source_remove(src);
	obs_source_release(src);
	RefreshScenesList();
}

void ShortsDock::OnDuplicateScene()
{
	QListWidgetItem *item = scenesList->currentItem();
	if (!item)
		return;

	obs_source_t *src = FindFrontendSceneByUuid(item->data(Qt::UserRole).toString());
	if (!src)
		return;

	obs_scene_t *sceneObj = obs_scene_from_source(src);
	bool ok = false;
	QString name = QInputDialog::getText(this, Translate("DuplicateScene"), Translate("NewSceneName"),
					     QLineEdit::Normal,
					     QString::fromUtf8(obs_source_get_name(src)) + QStringLiteral(" Copy"), &ok);
	if (!ok || name.trimmed().isEmpty()) {
		obs_source_release(src);
		return;
	}

	obs_scene_t *dup = obs_scene_duplicate(sceneObj, name.trimmed().toUtf8().constData(), OBS_SCENE_DUP_REFS);
	if (dup) {
		obs_frontend_set_current_scene(obs_scene_get_source(dup));
		obs_scene_release(dup);
	}
	obs_source_release(src);
	RefreshScenesList();
}

void ShortsDock::OnRenameScene()
{
	QListWidgetItem *item = scenesList->currentItem();
	if (!item)
		return;

	obs_source_t *src = FindFrontendSceneByUuid(item->data(Qt::UserRole).toString());
	if (!src)
		return;

	bool ok = false;
	QString name = QInputDialog::getText(this, Translate("RenameScene"), Translate("NewSceneName"),
					     QLineEdit::Normal, QString::fromUtf8(obs_source_get_name(src)), &ok);
	if (ok && !name.trimmed().isEmpty())
		obs_source_set_name(src, name.trimmed().toUtf8().constData());

	obs_source_release(src);
	RefreshScenesList();
}

void ShortsDock::OnSourceSelectionChanged()
{
	if (!scene || updatingTransform)
		return;

	QListWidgetItem *item = sourcesList->currentItem();
	if (!item)
		return;

	const int64_t id = item->data(Qt::UserRole).toLongLong();
	obs_scene_enum_items(scene, ClearSelection, nullptr);
	obs_sceneitem_t *si = FindItemById(scene, id);
	if (si)
		obs_sceneitem_select(si, true);
	RefreshTransformControls();
}

void ShortsDock::OnAddSource()
{
	obs_source_t *current = obs_frontend_get_current_scene();
	if (!current)
		return;
	obs_scene_t *mainScene = obs_scene_from_source(current);
	if (!mainScene) {
		obs_source_release(current);
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
		QMessageBox::information(this, Translate("AddSource"), Translate("SelectSource"));
		obs_source_release(current);
		return;
	}

	QStringList items;
	for (const auto &n : names)
		items << QString::fromUtf8(n.c_str());

	bool ok = false;
	QString chosen =
		QInputDialog::getItem(this, Translate("AddSource"), Translate("SelectSource"), items, 0, false, &ok);
	if (!ok || chosen.isEmpty()) {
		obs_source_release(current);
		return;
	}

	for (size_t i = 0; i < names.size(); i++) {
		if (chosen != QString::fromUtf8(names[i].c_str()))
			continue;
		obs_scene_add(mainScene, sources[i]);
		break;
	}

	obs_scene_t *mirror = EnsureVerticalMirror(current);
	SyncMirrorFromMain(mirror, mainScene);
	SetActiveScene(mirror, true);

	obs_source_release(current);
}

void ShortsDock::OnRemoveSource()
{
	if (!scene)
		return;

	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;

	if (QMessageBox::question(this, Translate("RemoveSource"), Translate("ConfirmRemoveSource")) !=
	    QMessageBox::Yes)
		return;

	obs_source_t *current = obs_frontend_get_current_scene();
	obs_scene_t *mainScene = current ? obs_scene_from_source(current) : nullptr;

	if (mainScene) {
		for (obs_sceneitem_t *item : selected) {
			obs_source_t *src = obs_sceneitem_get_source(item);
			obs_sceneitem_t *mainItem = FindItemBySource(mainScene, src);
			if (mainItem)
				obs_sceneitem_remove(mainItem);
		}
		obs_scene_t *mirror = EnsureVerticalMirror(current);
		SyncMirrorFromMain(mirror, mainScene);
		SetActiveScene(mirror, true);
	} else {
		for (obs_sceneitem_t *item : selected)
			obs_sceneitem_remove(item);
		RefreshSourcesList();
		RefreshTransformControls();
	}

	if (current)
		obs_source_release(current);
}

void ShortsDock::OnToggleSourceVisible()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected)
		obs_sceneitem_set_visible(item, !obs_sceneitem_visible(item));
	RefreshSourcesList();
}

void ShortsDock::OnToggleSourceLock()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected)
		obs_sceneitem_set_locked(item, !obs_sceneitem_locked(item));
	RefreshSourcesList();
	RefreshTransformControls();
}

void ShortsDock::OnSourceProperties()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_source_t *src = obs_sceneitem_get_source(selected.front());
	if (src)
		obs_frontend_open_source_properties(src);
}

void ShortsDock::OnSourceFilters()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;
	obs_source_t *src = obs_sceneitem_get_source(selected.front());
	if (src)
		obs_frontend_open_source_filters(src);
}

void ShortsDock::OnSourceMoveUp()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected)
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_UP);
	RefreshSourcesList();
}

void ShortsDock::OnSourceMoveDown()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected)
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_DOWN);
	RefreshSourcesList();
}

void ShortsDock::OnFitToScreen()
{
	if (!scene)
		return;
	const float cw = (float)ActiveCanvasWidth();
	const float ch = (float)ActiveCanvasHeight();
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected) {
		if (obs_sceneitem_locked(item))
			continue;
		obs_source_t *source = obs_sceneitem_get_source(item);
		uint32_t srcW = std::max(1u, obs_source_get_width(source));
		uint32_t srcH = std::max(1u, obs_source_get_height(source));
		float scale = std::min(cw / (float)srcW, ch / (float)srcH);
		vec2 s;
		vec2_set(&s, scale, scale);
		obs_sceneitem_set_scale(item, &s);
		vec2 pos;
		vec2_set(&pos, (cw - (float)srcW * scale) * 0.5f, (ch - (float)srcH * scale) * 0.5f);
		obs_sceneitem_set_pos(item, &pos);
		obs_sceneitem_set_rot(item, 0.0f);
	}
	RefreshTransformControls();
}

void ShortsDock::OnStretchToScreen()
{
	if (!scene)
		return;
	const float cw = (float)ActiveCanvasWidth();
	const float ch = (float)ActiveCanvasHeight();
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected) {
		if (obs_sceneitem_locked(item))
			continue;
		obs_source_t *source = obs_sceneitem_get_source(item);
		uint32_t srcW = std::max(1u, obs_source_get_width(source));
		uint32_t srcH = std::max(1u, obs_source_get_height(source));
		vec2 s;
		vec2_set(&s, cw / (float)srcW, ch / (float)srcH);
		obs_sceneitem_set_scale(item, &s);
		vec2 pos;
		vec2_set(&pos, 0.0f, 0.0f);
		obs_sceneitem_set_pos(item, &pos);
		obs_sceneitem_set_rot(item, 0.0f);
	}
	RefreshTransformControls();
}

void ShortsDock::OnCenterToScreen()
{
	if (!scene)
		return;
	const float cw = (float)ActiveCanvasWidth();
	const float ch = (float)ActiveCanvasHeight();
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected) {
		if (obs_sceneitem_locked(item))
			continue;
		ItemTransform info = ReadItemTransform(item);
		obs_source_t *source = obs_sceneitem_get_source(item);
		obs_sceneitem_crop crop;
		obs_sceneitem_get_crop(item, &crop);
		float w = float(obs_source_get_width(source) - crop.left - crop.right) * info.scale.x;
		float h = float(obs_source_get_height(source) - crop.top - crop.bottom) * info.scale.y;
		vec2 pos;
		vec2_set(&pos, (cw - w) * 0.5f, (ch - h) * 0.5f);
		obs_sceneitem_set_pos(item, &pos);
	}
	RefreshTransformControls();
}

void ShortsDock::OnResetTransform()
{
	if (!scene)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected) {
		if (obs_sceneitem_locked(item))
			continue;
		vec2 one, zero;
		vec2_set(&one, 1.0f, 1.0f);
		vec2_set(&zero, 0.0f, 0.0f);
		obs_sceneitem_set_pos(item, &zero);
		obs_sceneitem_set_scale(item, &one);
		obs_sceneitem_set_rot(item, 0.0f);
		obs_sceneitem_crop crop = {0, 0, 0, 0};
		obs_sceneitem_set_crop(item, &crop);
	}
	RefreshTransformControls();
}

void ShortsDock::OnTransformEdited()
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
	vec2_set(&pos, (float)posXSpin->value(), (float)posYSpin->value());
	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_set_rot(item, (float)rotSpin->value());

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
	vec2_set(&scale, (float)sizeWSpin->value() / innerW, (float)sizeHSpin->value() / innerH);
	obs_sceneitem_set_scale(item, &scale);
}

void ShortsDock::OnTransitionChanged(int index)
{
	if (loadingSettings || index < 0)
		return;

	const QString name = transitionCombo->itemData(index).toString();
	if (name.isEmpty())
		return;

	obs_frontend_source_list transitions = {};
	obs_frontend_get_transitions(&transitions);
	for (size_t i = 0; i < transitions.sources.num; i++) {
		obs_source_t *src = transitions.sources.array[i];
		if (name == QString::fromUtf8(obs_source_get_name(src))) {
			obs_frontend_set_current_transition(src);
			break;
		}
	}
	obs_frontend_source_list_free(&transitions);
}

void ShortsDock::OnTransitionDurationChanged(int value)
{
	if (loadingSettings)
		return;
	obs_frontend_set_transition_duration(value);
}

void ShortsDock::OnGoLive()
{
	if (!outputs)
		return;
	if (outputs->IsStreaming()) {
		outputs->StopStreaming();
		return;
	}
	QString err;
	if (!outputs->StartStreaming(&err)) {
		goLiveBtn->setChecked(false);
		QMessageBox::warning(this, Translate("GoLive"), err);
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
			this, title, info.message + QStringLiteral("

") + Translate("SaveAvailablePortion"),
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
	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *src = scenes.sources.array[i];
		names << QString::fromUtf8(obs_source_get_name(src));
		const char *u = obs_source_get_uuid(src);
		uuids << (u ? QString::fromUtf8(u) : QString());
	}
	obs_frontend_source_list_free(&scenes);
}

void ShortsDock::OnSettings()
{
	QStringList names, uuids;
	CollectSceneLists(names, uuids);
	const QString statusText = automation ? automation->StatusText() : QString();
	const auto status = automation ? automation->Status() : vsp::AutomationStatus::Disabled;

	SettingsDialog dlg(settings, outputs.get(), names, uuids, status, statusText, this);
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
	if (!autoIndicator)
		return;
	const bool show = status == vsp::AutomationStatus::Recording || status == vsp::AutomationStatus::Starting ||
			  status == vsp::AutomationStatus::Scheduled;
	autoIndicator->setVisible(show || (status == vsp::AutomationStatus::Waiting && settings.automationEnabled));
	autoIndicator->setText(text);
}

void ShortsDock::OnAutomationNotify(const QString &title, const QString &message)
{
	QMessageBox::information(this, title, message);
}

void ShortsDock::RefreshScenesList()
{
	if (!scenesList)
		return;

	loadingSettings = true;
	const QString previous = scenesList->currentItem() ? scenesList->currentItem()->data(Qt::UserRole).toString()
							   : QString();

	scenesList->clear();

	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);

	obs_source_t *current = obs_frontend_get_current_scene();
	QString currentUuid;
	if (current) {
		const char *uuid = obs_source_get_uuid(current);
		if (uuid)
			currentUuid = QString::fromUtf8(uuid);
	}

	int selectIndex = -1;
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *src = scenes.sources.array[i];
		const char *uuid = obs_source_get_uuid(src);
		auto *row = new QListWidgetItem(QString::fromUtf8(obs_source_get_name(src)));
		row->setData(Qt::UserRole, uuid ? QString::fromUtf8(uuid) : QString());
		scenesList->addItem(row);
		if (uuid && currentUuid == QString::fromUtf8(uuid))
			selectIndex = (int)i;
		else if (selectIndex < 0 && uuid && previous == QString::fromUtf8(uuid))
			selectIndex = (int)i;
	}

	if (selectIndex >= 0)
		scenesList->setCurrentRow(selectIndex);

	obs_frontend_source_list_free(&scenes);
	if (current)
		obs_source_release(current);
	loadingSettings = false;
}

void ShortsDock::RefreshSourcesList()
{
	if (!sourcesList)
		return;

	updatingTransform = true;
	sourcesList->clear();
	if (!scene) {
		updatingTransform = false;
		return;
	}

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
			auto *list = static_cast<QListWidget *>(param);
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
			list->addItem(row);
			return true;
		},
		sourcesList);

	updatingTransform = false;
}

void ShortsDock::RefreshMixer()
{
	if (mixerPanel)
		mixerPanel->Refresh();
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

void ShortsDock::OnBufferStatus(BufferStatus, const QString &text)
{
	if (bufferStatusLabel)
		bufferStatusLabel->setText(text);
}

void ShortsDock::RefreshTransitions()
{
	if (!transitionCombo)
		return;

	loadingSettings = true;
	transitionCombo->clear();

	obs_frontend_source_list transitions = {};
	obs_frontend_get_transitions(&transitions);
	obs_source_t *current = obs_frontend_get_current_transition();
	QString currentName;
	if (current)
		currentName = QString::fromUtf8(obs_source_get_name(current));

	int select = -1;
	for (size_t i = 0; i < transitions.sources.num; i++) {
		obs_source_t *src = transitions.sources.array[i];
		const QString name = QString::fromUtf8(obs_source_get_name(src));
		transitionCombo->addItem(name, name);
		if (name == currentName)
			select = (int)i;
	}
	if (select >= 0)
		transitionCombo->setCurrentIndex(select);

	if (transitionDuration)
		transitionDuration->setValue(obs_frontend_get_transition_duration());

	obs_frontend_source_list_free(&transitions);
	if (current)
		obs_source_release(current);
	loadingSettings = false;
}

void ShortsDock::RefreshTransformControls()
{
	if (!posXSpin)
		return;

	updatingTransform = true;
	std::vector<obs_sceneitem_t *> selected;
	if (scene)
		obs_scene_enum_items(scene, CollectSelected, &selected);

	bool enable = !selected.empty() && !obs_sceneitem_locked(selected.front());
	posXSpin->setEnabled(enable);
	posYSpin->setEnabled(enable);
	sizeWSpin->setEnabled(enable);
	sizeHSpin->setEnabled(enable);
	rotSpin->setEnabled(enable);

	if (!selected.empty()) {
		obs_sceneitem_t *item = selected.front();
		ItemTransform info = ReadItemTransform(item);
		obs_source_t *source = obs_sceneitem_get_source(item);
		obs_sceneitem_crop crop;
		obs_sceneitem_get_crop(item, &crop);
		float w = float(obs_source_get_width(source) - crop.left - crop.right) * info.scale.x;
		float h = float(obs_source_get_height(source) - crop.top - crop.bottom) * info.scale.y;
		posXSpin->setValue(info.pos.x);
		posYSpin->setValue(info.pos.y);
		sizeWSpin->setValue(w);
		sizeHSpin->setValue(h);
		rotSpin->setValue(info.rot);
	}
	updatingTransform = false;
}

void ShortsDock::SaveSettings(obs_data_t *data)
{
	vsp::SaveSettingsToData(data, settings, verticalWidth, verticalHeight);
	SaveHotkeys(data);

	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (auto it = verticalMirrors.begin(); it != verticalMirrors.end(); ++it) {
		obs_scene_t *mirror = it.value();
		if (!mirror)
			continue;
		OBSDataAutoRelease obj = obs_data_create();
		obs_data_set_string(obj, "main_uuid", it.key().toUtf8().constData());
		OBSDataAutoRelease sceneData = obs_save_source(obs_scene_get_source(mirror));
		obs_data_set_obj(obj, "source", sceneData);
		obs_data_array_push_back(arr, obj);
	}
	obs_data_set_array(data, "scenes", arr);
	obs_data_set_array(data, "vertical_mirrors", arr);
}

void ShortsDock::LoadSettings(obs_data_t *data)
{
	loadingSettings = true;

	settings = vsp::LoadSettingsFromData(data, verticalWidth, verticalHeight);
	LoadHotkeys(data);
	ApplyCanvasFromSettings();

	verticalMirrors.clear();

	obs_data_array_t *arr = obs_data_get_array(data, "vertical_mirrors");
	if (!arr)
		arr = obs_data_get_array(data, "scenes");

	if (arr) {
		const size_t count = obs_data_array_count(arr);
		for (size_t i = 0; i < count; i++) {
			OBSDataAutoRelease obj = obs_data_array_item(arr, i);
			const char *uuid = obs_data_get_string(obj, "main_uuid");
			obs_data_t *sourceData = obs_data_get_obj(obj, "source");
			if (!uuid || !*uuid || !sourceData) {
				if (sourceData)
					obs_data_release(sourceData);
				continue;
			}
			obs_source_t *src = obs_load_source(sourceData);
			obs_data_release(sourceData);
			if (!src)
				continue;
			obs_scene_t *loaded = obs_scene_from_source(src);
			if (loaded)
				verticalMirrors.insert(QString::fromUtf8(uuid), loaded);
			obs_source_release(src);
		}
		obs_data_array_release(arr);
	}

	if (outputs)
		outputs->ApplySettings(settings);

	loadingSettings = false;
	RefreshVerticalWorkspace(true);
	if (automation)
		automation->ApplySettings(settings);
	RefreshScenesList();
	RefreshTransitions();
	RefreshMixer();
}


void ShortsDock::SyncActiveSceneFromFrontend()
{
	RefreshScenesList();

	obs_source_t *cur = obs_frontend_get_current_scene();
	if (!cur)
		return;

	obs_scene_t *mirror = EnsureVerticalMirror(cur);
	SyncMirrorFromMain(mirror, obs_scene_from_source(cur));
	SetActiveScene(mirror, true);

	if (automation) {
		const char *uuid = obs_source_get_uuid(cur);
		automation->OnSceneChanged(uuid ? QString::fromUtf8(uuid) : QString(),
					   QString::fromUtf8(obs_source_get_name(cur)));
	}

	obs_source_release(cur);
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
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		QMetaObject::invokeMethod(dock, "RefreshScenesList", Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		QMetaObject::invokeMethod(dock, "SyncActiveSceneFromFrontend", Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_TRANSITION_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_TRANSITION_CHANGED:
	case OBS_FRONTEND_EVENT_TRANSITION_DURATION_CHANGED:
		QMetaObject::invokeMethod(dock, "RefreshTransitions", Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		QMetaObject::invokeMethod(dock, "RefreshMixer", Qt::QueuedConnection);
		QMetaObject::invokeMethod(dock, "RefreshScenesList", Qt::QueuedConnection);
		QMetaObject::invokeMethod(dock, "SyncActiveSceneFromFrontend", Qt::QueuedConnection);
		break;
	default:
		break;
	}
}

void ShortsDock::UpdatePreviewScale(int cx, int cy)
{
	GetScaleAndCenterPos((int)ActiveCanvasWidth(), (int)ActiveCanvasHeight(), cx, cy, previewX, previewY,
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

	const uint32_t canvasW = ActiveCanvasWidth();
	const uint32_t canvasH = ActiveCanvasHeight();
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

		RefreshSourcesList();
		RefreshTransformControls();
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
		RefreshSourcesList();
		RefreshTransformControls();
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

		RefreshTransformControls();
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
	menu.addAction(Translate("FitToScreen"), this, &ShortsDock::OnFitToScreen);
	menu.addAction(Translate("StretchToScreen"), this, &ShortsDock::OnStretchToScreen);
	menu.addAction(Translate("CenterToScreen"), this, &ShortsDock::OnCenterToScreen);
	menu.addAction(Translate("ResetTransform"), this, &ShortsDock::OnResetTransform);
	menu.addSeparator();
	menu.addAction(Translate("SourceProperties"), this, &ShortsDock::OnSourceProperties);
	menu.addAction(Translate("SourceFilters"), this, &ShortsDock::OnSourceFilters);
	menu.addSeparator();

	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	const bool has = !selected.empty();
	const bool isLocked = has && obs_sceneitem_locked(selected.front());
	const bool isVisible = has && obs_sceneitem_visible(selected.front());

	QAction *lockAction = menu.addAction(Translate(isLocked ? "Unlock" : "Lock"), this, &ShortsDock::OnToggleSourceLock);
	lockAction->setEnabled(has);
	QAction *visAction =
		menu.addAction(Translate(isVisible ? "Hidden" : "Visible"), this, &ShortsDock::OnToggleSourceVisible);
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
