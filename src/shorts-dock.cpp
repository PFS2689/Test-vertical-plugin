#include "shorts-dock.hpp"
#include "display-helpers.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QMetaObject>
#include <QPainterPath>
#include <QScreen>
#include <QScrollArea>
#include <QSplitter>
#include <QToolButton>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

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
#define HELPER_COLOR 0xFFD0D0D0

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

} // namespace

ShortsDock::ShortsDock(QWidget *parent) : QFrame(parent)
{
	setObjectName("ShortsDock");
	setMinimumWidth(280);
	setMinimumHeight(400);

	BuildUI();

	/* View/scene setup can fail if video is not ready yet — keep the dock usable. */
	CreateView();

	obs_frontend_add_event_callback(FrontendEvent, this);

	obs_scene_t *defaultScene = CreateShortScene(Translate("NewSceneName"));
	if (defaultScene) {
		SetCurrentScene(defaultScene);
		UpdateScenesCombo();
	} else {
		blog(LOG_WARNING, "[obs-shorts-vertical] Could not create default short scene");
	}
}

ShortsDock::~ShortsDock()
{
	obs_frontend_remove_event_callback(FrontendEvent, this);

	if (recordOutput) {
		if (obs_output_active(recordOutput))
			obs_output_stop(recordOutput);
		obs_output_release(recordOutput);
		recordOutput = nullptr;
	}

	clearing = true;

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

	for (OBSScene &s : scenes)
		s = nullptr;
	scenes.clear();

	obs_enter_graphics();
	gs_vertexbuffer_destroy(box);
	gs_vertexbuffer_destroy(rectFill);
	box = nullptr;
	rectFill = nullptr;
	obs_leave_graphics();
}

void ShortsDock::BuildUI()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(6, 6, 6, 6);
	root->setSpacing(6);

	/* Toolbar: canvas size + record */
	auto *toolbar = new QHBoxLayout();
	canvasPreset = new QComboBox(this);
	canvasPreset->addItem(Translate("Preset1080x1920"), QVariant::fromValue(QSize(1080, 1920)));
	canvasPreset->addItem(Translate("Preset720x1280"), QVariant::fromValue(QSize(720, 1280)));
	canvasPreset->addItem(Translate("Preset1080x1350"), QVariant::fromValue(QSize(1080, 1350)));
	canvasPreset->addItem(Translate("CustomSize"), QVariant());
	connect(canvasPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&ShortsDock::OnCanvasPresetChanged);

	widthSpin = new QSpinBox(this);
	widthSpin->setRange(240, 4320);
	widthSpin->setValue((int)canvasWidth);
	heightSpin = new QSpinBox(this);
	heightSpin->setRange(240, 7680);
	heightSpin->setValue((int)canvasHeight);

	auto *applySize = new QPushButton(Translate("Apply"), this);
	connect(applySize, &QPushButton::clicked, this, &ShortsDock::OnApplyCustomSize);

	recordButton = new QPushButton(Translate("Record"), this);
	recordButton->setCheckable(true);
	connect(recordButton, &QPushButton::clicked, this, &ShortsDock::OnToggleRecord);

	lockCheck = new QCheckBox(Translate("LockPreview"), this);
	connect(lockCheck, &QCheckBox::toggled, this, &ShortsDock::OnLockToggled);

	toolbar->addWidget(new QLabel(Translate("CanvasSize"), this));
	toolbar->addWidget(canvasPreset, 1);
	toolbar->addWidget(widthSpin);
	toolbar->addWidget(new QLabel("×", this));
	toolbar->addWidget(heightSpin);
	toolbar->addWidget(applySize);
	toolbar->addWidget(lockCheck);
	toolbar->addWidget(recordButton);
	root->addLayout(toolbar);

	auto *splitter = new QSplitter(Qt::Horizontal, this);

	/* Left: scenes + sources */
	auto *left = new QWidget(splitter);
	auto *leftLayout = new QVBoxLayout(left);
	leftLayout->setContentsMargins(0, 0, 0, 0);

	auto *sceneRow = new QHBoxLayout();
	scenesCombo = new QComboBox(left);
	connect(scenesCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ShortsDock::OnSceneChanged);
	auto *addSceneBtn = new QToolButton(left);
	addSceneBtn->setText("+");
	addSceneBtn->setToolTip(Translate("AddScene"));
	connect(addSceneBtn, &QToolButton::clicked, this, &ShortsDock::OnAddScene);
	auto *removeSceneBtn = new QToolButton(left);
	removeSceneBtn->setText("−");
	removeSceneBtn->setToolTip(Translate("RemoveScene"));
	connect(removeSceneBtn, &QToolButton::clicked, this, &ShortsDock::OnRemoveScene);
	sceneRow->addWidget(scenesCombo, 1);
	sceneRow->addWidget(addSceneBtn);
	sceneRow->addWidget(removeSceneBtn);
	leftLayout->addWidget(new QLabel(Translate("ShortsScenes"), left));
	leftLayout->addLayout(sceneRow);

	leftLayout->addWidget(new QLabel(Translate("ShortsSources"), left));
	sourcesList = new QListWidget(left);
	connect(sourcesList, &QListWidget::itemSelectionChanged, this, &ShortsDock::OnSourceSelectionChanged);
	leftLayout->addWidget(sourcesList, 1);

	auto *sourceButtons = new QHBoxLayout();
	auto *addCam = new QPushButton(Translate("AddCamera"), left);
	connect(addCam, &QPushButton::clicked, this, &ShortsDock::OnAddCamera);
	auto *addSrc = new QPushButton(Translate("AddSource"), left);
	connect(addSrc, &QPushButton::clicked, this, &ShortsDock::OnAddExistingSource);
	auto *removeSrc = new QPushButton(Translate("RemoveSource"), left);
	connect(removeSrc, &QPushButton::clicked, this, &ShortsDock::OnRemoveSource);
	sourceButtons->addWidget(addCam);
	sourceButtons->addWidget(addSrc);
	sourceButtons->addWidget(removeSrc);
	leftLayout->addLayout(sourceButtons);

	/* Transform panel */
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

	left->setMinimumWidth(260);
	splitter->addWidget(left);

	/* Right: preview */
	auto *right = new QWidget(splitter);
	auto *rightLayout = new QVBoxLayout(right);
	rightLayout->setContentsMargins(0, 0, 0, 0);

	preview = new OBSQTDisplay(right);
	preview->setMinimumSize(180, 320);
	preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	previewEventFilter = BuildEventFilter();
	preview->installEventFilter(previewEventFilter.get());

	auto addDrawCallback = [this]() {
		obs_display_add_draw_callback(preview->GetDisplay(), DrawCallback, this);
	};
	connect(preview, &OBSQTDisplay::DisplayCreated, addDrawCallback);

	helpLabel = new QLabel(Translate("PreviewHelp"), right);
	helpLabel->setWordWrap(true);
	helpLabel->setStyleSheet("color: #888; font-size: 11px;");

	rightLayout->addWidget(preview, 1);
	rightLayout->addWidget(helpLabel);
	splitter->addWidget(right);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);

	root->addWidget(splitter, 1);
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
		blog(LOG_WARNING, "[obs-shorts-vertical] Video not ready yet; retrying view later");
		/* Keep the view object; Add2 can be retried when video resets. */
		ovi.fps_num = 30;
		ovi.fps_den = 1;
	}
	ovi.base_width = canvasWidth;
	ovi.base_height = canvasHeight;
	ovi.output_width = canvasWidth;
	ovi.output_height = canvasHeight;

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
	if (width < 240 || height < 240)
		return;

	canvasWidth = width;
	canvasHeight = height;
	widthSpin->setValue((int)width);
	heightSpin->setValue((int)height);

	/* Rebuild view at new resolution */
	obs_source_t *current = scene ? obs_scene_get_source(scene) : nullptr;
	CreateView();
	if (current && view)
		obs_view_set_source(view, 0, current);
}

obs_scene_t *ShortsDock::CreateShortScene(const char *name)
{
	obs_scene_t *s = obs_scene_create_private(name);
	if (!s)
		return nullptr;
	/* OBSScene addrefs via obs_scene_get_ref; release the create ref. */
	scenes.emplace_back(s);
	obs_scene_release(s);
	return scenes.back();
}

void ShortsDock::SetCurrentScene(obs_scene_t *newScene)
{
	if (scene == newScene)
		return;

	if (scene) {
		obs_source_t *prev = obs_scene_get_source(scene);
		if (prev)
			obs_source_dec_showing(prev);
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

	if (view)
		obs_view_set_source(view, 0, scene ? obs_scene_get_source(scene) : nullptr);

	RefreshSourcesList();
	RefreshTransformControls();
}

void ShortsDock::UpdateScenesCombo()
{
	scenesCombo->blockSignals(true);
	scenesCombo->clear();
	int current = -1;
	for (size_t i = 0; i < scenes.size(); i++) {
		obs_source_t *src = obs_scene_get_source(scenes[i]);
		scenesCombo->addItem(QString::fromUtf8(obs_source_get_name(src)));
		if (scenes[i] == scene)
			current = (int)i;
	}
	if (current >= 0)
		scenesCombo->setCurrentIndex(current);
	scenesCombo->blockSignals(false);
}

void ShortsDock::OnAddScene()
{
	bool ok = false;
	QString name = QInputDialog::getText(this, Translate("AddScene"), Translate("NewSceneName"),
					     QLineEdit::Normal, Translate("NewSceneName"), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;

	obs_scene_t *s = CreateShortScene(name.trimmed().toUtf8().constData());
	SetCurrentScene(s);
	UpdateScenesCombo();
}

void ShortsDock::OnRemoveScene()
{
	if (scenes.size() <= 1 || !scene)
		return;

	if (QMessageBox::question(this, Translate("RemoveScene"), Translate("ConfirmRemoveScene")) !=
	    QMessageBox::Yes)
		return;

	obs_scene_t *toRemove = scene;
	size_t idx = 0;
	for (; idx < scenes.size(); idx++) {
		if (scenes[idx] == toRemove)
			break;
	}

	size_t next = idx > 0 ? idx - 1 : 0;
	if (idx == 0 && scenes.size() > 1)
		next = 1;

	obs_scene_t *nextScene = scenes[next];
	SetCurrentScene(nextScene);

	/* Erasing releases the vector's reference; SetCurrentScene already dropped ours. */
	scenes.erase(scenes.begin() + (ptrdiff_t)idx);
	UpdateScenesCombo();
}

void ShortsDock::OnSceneChanged(int index)
{
	if (index < 0 || index >= (int)scenes.size())
		return;
	SetCurrentScene(scenes[(size_t)index]);
}

void ShortsDock::OnAddCamera()
{
	if (!scene)
		return;

	const char *id =
#ifdef _WIN32
		"dshow_input";
#elif defined(__APPLE__)
		"av_capture_input";
#else
		"v4l2_input";
#endif

	obs_source_t *cam = obs_source_create(id, Translate("Camera"), nullptr, nullptr);
	if (!cam) {
		/* Fallback: let user pick any input source via properties later */
		cam = obs_source_create("image_source", Translate("Camera"), nullptr, nullptr);
	}
	if (!cam)
		return;

	obs_sceneitem_t *item = obs_scene_add(scene, cam);
	obs_source_release(cam);

	if (item) {
		obs_sceneitem_set_visible(item, true);
		/* Fit camera nicely in vertical frame */
		obs_source_t *src = obs_sceneitem_get_source(item);
		uint32_t srcW = std::max(1u, obs_source_get_width(src));
		uint32_t srcH = std::max(1u, obs_source_get_height(src));

		float scale = std::min((float)canvasWidth / (float)srcW, (float)canvasHeight / (float)srcH);
		vec2 s;
		vec2_set(&s, scale, scale);
		obs_sceneitem_set_scale(item, &s);

		vec2 pos;
		vec2_set(&pos, ((float)canvasWidth - (float)srcW * scale) * 0.5f,
			 ((float)canvasHeight - (float)srcH * scale) * 0.5f);
		obs_sceneitem_set_pos(item, &pos);

		obs_scene_enum_items(scene, ClearSelection, nullptr);
		obs_sceneitem_select(item, true);
	}

	RefreshSourcesList();
	RefreshTransformControls();
}

void ShortsDock::OnAddExistingSource()
{
	if (!scene)
		return;

	std::vector<std::string> names;
	std::vector<OBSSource> sources;

	struct EnumData {
		std::vector<std::string> *names;
		std::vector<OBSSource> *sources;
	} data{&names, &sources};

	obs_enum_sources(
		[](void *param, obs_source_t *source) -> bool {
			auto *d = static_cast<EnumData *>(param);
			uint32_t caps = obs_source_get_output_flags(source);
			if ((caps & OBS_SOURCE_VIDEO) == 0)
				return true;
			d->names->emplace_back(obs_source_get_name(source));
			d->sources->emplace_back(source);
			return true;
		},
		&data);

	if (names.empty()) {
		QMessageBox::information(this, Translate("AddSource"), Translate("SelectCamera"));
		return;
	}

	QStringList items;
	for (const auto &n : names)
		items << QString::fromUtf8(n.c_str());

	bool ok = false;
	QString chosen = QInputDialog::getItem(this, Translate("AddSource"), Translate("SelectCamera"), items, 0,
					       false, &ok);
	if (!ok || chosen.isEmpty())
		return;

	for (size_t i = 0; i < names.size(); i++) {
		if (chosen == QString::fromUtf8(names[i].c_str())) {
			obs_sceneitem_t *item = obs_scene_add(scene, sources[i]);
			if (item) {
				obs_scene_enum_items(scene, ClearSelection, nullptr);
				obs_sceneitem_select(item, true);
			}
			break;
		}
	}

	RefreshSourcesList();
	RefreshTransformControls();
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

	for (obs_sceneitem_t *item : selected)
		obs_sceneitem_remove(item);

	RefreshSourcesList();
	RefreshTransformControls();
}

void ShortsDock::OnSourceSelectionChanged()
{
	if (!scene || updatingTransform)
		return;

	QListWidgetItem *item = sourcesList->currentItem();
	if (!item)
		return;

	int64_t id = item->data(Qt::UserRole).toLongLong();
	obs_scene_enum_items(scene, ClearSelection, nullptr);

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *si, void *param) -> bool {
			int64_t *want = static_cast<int64_t *>(param);
			if (obs_sceneitem_get_id(si) == *want) {
				obs_sceneitem_select(si, true);
				return false;
			}
			return true;
		},
		&id);

	RefreshTransformControls();
}

void ShortsDock::OnCanvasPresetChanged(int index)
{
	QVariant v = canvasPreset->itemData(index);
	if (!v.isValid())
		return;
	QSize size = v.toSize();
	SetCanvasSize((uint32_t)size.width(), (uint32_t)size.height());
}

void ShortsDock::OnApplyCustomSize()
{
	SetCanvasSize((uint32_t)widthSpin->value(), (uint32_t)heightSpin->value());
	canvasPreset->setCurrentIndex(canvasPreset->count() - 1);
}

void ShortsDock::OnToggleRecord()
{
	if (recordOutput && obs_output_active(recordOutput)) {
		obs_output_stop(recordOutput);
		recordButton->setText(Translate("Record"));
		recordButton->setChecked(false);
		return;
	}

	if (!video) {
		recordButton->setChecked(false);
		return;
	}

	QString path = QFileDialog::getSaveFileName(this, Translate("Record"), QString(), "MP4 (*.mp4);;MKV (*.mkv)");
	if (path.isEmpty()) {
		recordButton->setChecked(false);
		return;
	}

	if (!recordOutput) {
		recordOutput = obs_output_create("ffmpeg_output", "shorts_record", nullptr, nullptr);
		if (!recordOutput)
			recordOutput = obs_output_create("ffmpeg_muxer", "shorts_record", nullptr, nullptr);
	}

	if (!recordOutput) {
		QMessageBox::warning(this, Translate("Record"), "Could not create recording output.");
		recordButton->setChecked(false);
		return;
	}

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "path", path.toUtf8().constData());
	obs_data_set_string(settings, "muxer_settings", "");
	obs_data_set_string(settings, "format_name", "mp4");
	obs_data_set_string(settings, "video_path", path.toUtf8().constData());
	obs_output_update(recordOutput, settings);

	obs_output_set_media(recordOutput, video, obs_get_audio());

	/* Attach simple encoders if needed */
	OBSEncoderAutoRelease venc = obs_video_encoder_create("obs_x264", "shorts_venc", nullptr, nullptr);
	OBSEncoderAutoRelease aenc = obs_audio_encoder_create("ffmpeg_aac", "shorts_aenc", nullptr, 0, nullptr);
	if (venc) {
		OBSDataAutoRelease vs = obs_data_create();
		obs_data_set_int(vs, "bitrate", 6000);
		obs_data_set_string(vs, "rate_control", "CBR");
		obs_data_set_string(vs, "preset", "veryfast");
		obs_encoder_update(venc, vs);
		obs_encoder_set_video(venc, video);
		obs_output_set_video_encoder(recordOutput, venc);
	}
	if (aenc) {
		OBSDataAutoRelease as = obs_data_create();
		obs_data_set_int(as, "bitrate", 160);
		obs_encoder_update(aenc, as);
		obs_encoder_set_audio(aenc, obs_get_audio());
		obs_output_set_audio_encoder(recordOutput, aenc, 0);
	}

	if (!obs_output_start(recordOutput)) {
		QMessageBox::warning(this, Translate("Record"),
				     QString("Failed to start recording: %1").arg(obs_output_get_last_error(recordOutput)));
		recordButton->setChecked(false);
		return;
	}

	recordButton->setText(Translate("StopRecord"));
	recordButton->setChecked(true);
}

void ShortsDock::OnTransformEdited()
{
	if (updatingTransform || !scene || locked)
		return;

	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	if (selected.empty())
		return;

	obs_sceneitem_t *item = selected.front();
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

void ShortsDock::OnFitToScreen()
{
	if (!scene || locked)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected) {
		obs_source_t *source = obs_sceneitem_get_source(item);
		uint32_t srcW = std::max(1u, obs_source_get_width(source));
		uint32_t srcH = std::max(1u, obs_source_get_height(source));
		float scale = std::min((float)canvasWidth / (float)srcW, (float)canvasHeight / (float)srcH);
		vec2 s;
		vec2_set(&s, scale, scale);
		obs_sceneitem_set_scale(item, &s);
		vec2 pos;
		vec2_set(&pos, ((float)canvasWidth - (float)srcW * scale) * 0.5f,
			 ((float)canvasHeight - (float)srcH * scale) * 0.5f);
		obs_sceneitem_set_pos(item, &pos);
		obs_sceneitem_set_rot(item, 0.0f);
	}
	RefreshTransformControls();
}

void ShortsDock::OnStretchToScreen()
{
	if (!scene || locked)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected) {
		obs_source_t *source = obs_sceneitem_get_source(item);
		uint32_t srcW = std::max(1u, obs_source_get_width(source));
		uint32_t srcH = std::max(1u, obs_source_get_height(source));
		vec2 s;
		vec2_set(&s, (float)canvasWidth / (float)srcW, (float)canvasHeight / (float)srcH);
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
	if (!scene || locked)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected) {
		ItemTransform info = ReadItemTransform(item);
		obs_source_t *source = obs_sceneitem_get_source(item);
		obs_sceneitem_crop crop;
		obs_sceneitem_get_crop(item, &crop);
		float w = float(obs_source_get_width(source) - crop.left - crop.right) * info.scale.x;
		float h = float(obs_source_get_height(source) - crop.top - crop.bottom) * info.scale.y;
		vec2 pos;
		vec2_set(&pos, ((float)canvasWidth - w) * 0.5f, ((float)canvasHeight - h) * 0.5f);
		obs_sceneitem_set_pos(item, &pos);
	}
	RefreshTransformControls();
}

void ShortsDock::OnResetTransform()
{
	if (!scene || locked)
		return;
	std::vector<obs_sceneitem_t *> selected;
	obs_scene_enum_items(scene, CollectSelected, &selected);
	for (obs_sceneitem_t *item : selected) {
		vec2 one;
		vec2_set(&one, 1.0f, 1.0f);
		vec2 zero;
		vec2_set(&zero, 0.0f, 0.0f);
		obs_sceneitem_set_pos(item, &zero);
		obs_sceneitem_set_scale(item, &one);
		obs_sceneitem_set_rot(item, 0.0f);
		obs_sceneitem_crop crop = {0, 0, 0, 0};
		obs_sceneitem_set_crop(item, &crop);
	}
	RefreshTransformControls();
}

void ShortsDock::OnLockToggled(bool locked_)
{
	locked = locked_;
}

void ShortsDock::RefreshSourcesList()
{
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
			auto *row = new QListWidgetItem(QString::fromUtf8(obs_source_get_name(source)));
			row->setData(Qt::UserRole, QVariant::fromValue((qint64)obs_sceneitem_get_id(item)));
			if (obs_sceneitem_selected(item))
				row->setSelected(true);
			list->addItem(row);
			return true;
		},
		sourcesList);

	updatingTransform = false;
}

void ShortsDock::RefreshTransformControls()
{
	updatingTransform = true;
	std::vector<obs_sceneitem_t *> selected;
	if (scene)
		obs_scene_enum_items(scene, CollectSelected, &selected);

	bool enable = !selected.empty();
	posXSpin->setEnabled(enable && !locked);
	posYSpin->setEnabled(enable && !locked);
	sizeWSpin->setEnabled(enable && !locked);
	sizeHSpin->setEnabled(enable && !locked);
	rotSpin->setEnabled(enable && !locked);

	if (enable) {
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

void ShortsDock::UpdatePreviewScale(int cx, int cy)
{
	GetScaleAndCenterPos((int)canvasWidth, (int)canvasHeight, cx, cy, previewX, previewY, previewScale);
}

void ShortsDock::DrawCallback(void *data, uint32_t cx, uint32_t cy)
{
	static_cast<ShortsDock *>(data)->DrawPreview(cx, cy);
}

void ShortsDock::DrawPreview(uint32_t cx, uint32_t cy)
{
	if (!scene)
		return;

	UpdatePreviewScale((int)cx, (int)cy);

	gs_viewport_push();
	gs_projection_push();

	gs_ortho(0.0f, (float)canvasWidth, 0.0f, (float)canvasHeight, -100.0f, 100.0f);
	gs_set_viewport(previewX, previewY, (int)(previewScale * canvasWidth), (int)(previewScale * canvasHeight));

	obs_source_t *source = obs_scene_get_source(scene);
	if (source)
		obs_source_video_render(source);

	DrawSceneEditing();

	gs_projection_pop();
	gs_viewport_pop();
}

void ShortsDock::DrawSceneEditing()
{
	if (locked)
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
	matrix4 invBoxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);
	matrix4_inv(&invBoxTransform, &boxTransform);

	vec3 size;
	vec3_set(&size, gs_get_width() ? 0.0f : 0.0f, 0.0f, 0.0f);
	UNUSED_PARAMETER(size);

	ItemTransform info = ReadItemTransform(item);

	gs_matrix_push();
	gs_matrix_mul(&boxTransform);

	obs_source_t *source = obs_sceneitem_get_source(item);
	UNUSED_PARAMETER(source);
	UNUSED_PARAMETER(info);

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
	return std::make_unique<OBSEventFilter>([this](QObject *obj, QEvent *event) {
		return HandlePreviewEvent(obj, event);
	});
}

bool ShortsDock::HandlePreviewEvent(QObject *, QEvent *event)
{
	switch (event->type()) {
	case QEvent::MouseButtonPress: {
		auto *mouse = static_cast<QMouseEvent *>(event);
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
	case QEvent::Wheel: {
		/* reserved for future zoom */
		return false;
	}
	default:
		return false;
	}
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
		vec2 itemPos;
		obs_sceneitem_get_pos(item, &itemPos);
		vec2_add(&itemPos, &itemPos, &offset);
		obs_sceneitem_set_pos(item, &itemPos);
	}

	vec2_sub(&lastMoveOffset, &pos, &startPos);
}

void ShortsDock::StretchItem(const vec2 &pos)
{
	if (!stretchItem)
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

void ShortsDock::SaveSettings(obs_data_t *data)
{
	obs_data_set_int(data, "width", canvasWidth);
	obs_data_set_int(data, "height", canvasHeight);

	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (OBSScene &s : scenes) {
		OBSDataAutoRelease obj = obs_data_create();
		obs_source_t *src = obs_scene_get_source(s);
		obs_data_set_string(obj, "name", obs_source_get_name(src));
		OBSDataAutoRelease sceneData = obs_save_source(src);
		obs_data_set_obj(obj, "source", sceneData);
		obs_data_array_push_back(arr, obj);
	}
	obs_data_set_array(data, "scenes", arr);

	if (scene)
		obs_data_set_string(data, "current_scene", obs_source_get_name(obs_scene_get_source(scene)));
}

void ShortsDock::LoadSettings(obs_data_t *data)
{
	uint32_t w = (uint32_t)obs_data_get_int(data, "width");
	uint32_t h = (uint32_t)obs_data_get_int(data, "height");
	if (w >= 240 && h >= 240)
		SetCanvasSize(w, h);

	obs_data_array_t *arr = obs_data_get_array(data, "scenes");
	if (!arr)
		return;

	/* Clear existing */
	SetCurrentScene(nullptr);
	for (OBSScene &s : scenes)
		s = nullptr;
	scenes.clear();

	size_t count = obs_data_array_count(arr);
	for (size_t i = 0; i < count; i++) {
		OBSDataAutoRelease obj = obs_data_array_item(arr, i);
		obs_data_t *sourceData = obs_data_get_obj(obj, "source");
		if (!sourceData)
			continue;
		obs_source_t *src = obs_load_source(sourceData);
		if (!src)
			continue;
		obs_scene_t *loaded = obs_scene_from_source(src);
		if (loaded) {
			scenes.emplace_back(loaded);
		}
		obs_source_release(src);
	}
	obs_data_array_release(arr);

	if (scenes.empty()) {
		obs_scene_t *fallback = CreateShortScene(Translate("NewSceneName"));
		SetCurrentScene(fallback);
	} else {
		const char *current = obs_data_get_string(data, "current_scene");
		obs_scene_t *found = scenes[0];
		for (OBSScene &s : scenes) {
			if (strcmp(obs_source_get_name(obs_scene_get_source(s)), current) == 0) {
				found = s;
				break;
			}
		}
		SetCurrentScene(found);
	}
	UpdateScenesCombo();
}

void ShortsDock::FrontendEvent(enum obs_frontend_event event, void *private_data)
{
	auto *dock = static_cast<ShortsDock *>(private_data);
	if (event == OBS_FRONTEND_EVENT_EXIT || event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN) {
		if (dock->recordOutput && obs_output_active(dock->recordOutput))
			obs_output_stop(dock->recordOutput);
	}
}
