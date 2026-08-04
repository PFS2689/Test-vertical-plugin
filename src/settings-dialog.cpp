#include "settings-dialog.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(vsp::PluginSettings s, VerticalOutputs *outs, QWidget *parent)
	: QDialog(parent),
	  settings(std::move(s)),
	  outputs(outs)
{
	setWindowTitle(QString::fromUtf8(obs_module_text("Settings")));
	setModal(true);
	resize(460, 420);

	auto *root = new QVBoxLayout(this);
	auto *form = new QFormLayout();

	presetCombo = new QComboBox(this);
	presetCombo->addItem(QString::fromUtf8(obs_module_text("PresetYouTube")),
			     (int)vsp::CanvasPreset::YouTubeVertical);
	presetCombo->addItem(QString::fromUtf8(obs_module_text("PresetTikTok")), (int)vsp::CanvasPreset::TikTokVertical);
	presetCombo->addItem(QString::fromUtf8(obs_module_text("PresetTwitch")), (int)vsp::CanvasPreset::TwitchVertical);
	presetCombo->addItem(QString::fromUtf8(obs_module_text("PresetInstagram")),
			     (int)vsp::CanvasPreset::InstagramVertical);
	presetCombo->addItem(QString::fromUtf8(obs_module_text("PresetCustom")), (int)vsp::CanvasPreset::Custom);
	connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsDialog::OnPresetChanged);

	widthSpin = new QSpinBox(this);
	widthSpin->setRange(160, 7680);
	heightSpin = new QSpinBox(this);
	heightSpin->setRange(160, 7680);
	aspectHint = new QLabel(this);
	aspectHint->setWordWrap(true);

	clipCombo = new QComboBox(this);
	clipCombo->addItem(QString::fromUtf8(obs_module_text("Clip10")), (int)vsp::ClipLengthPreset::Sec10);
	clipCombo->addItem(QString::fromUtf8(obs_module_text("Clip20")), (int)vsp::ClipLengthPreset::Sec20);
	clipCombo->addItem(QString::fromUtf8(obs_module_text("Clip30")), (int)vsp::ClipLengthPreset::Sec30);
	clipCombo->addItem(QString::fromUtf8(obs_module_text("Clip60")), (int)vsp::ClipLengthPreset::Sec60);
	clipCombo->addItem(QString::fromUtf8(obs_module_text("ClipCustom")), (int)vsp::ClipLengthPreset::Custom);
	connect(clipCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&SettingsDialog::OnClipPresetChanged);

	clipCustomSpin = new QSpinBox(this);
	clipCustomSpin->setRange(1, 600);
	clipCustomSpin->setSuffix(QStringLiteral(" s"));

	pathEdit = new QLineEdit(this);
	auto *browse = new QPushButton(QString::fromUtf8(obs_module_text("Browse")), this);
	connect(browse, &QPushButton::clicked, this, &SettingsDialog::OnBrowsePath);
	auto *pathRow = new QHBoxLayout();
	pathRow->addWidget(pathEdit, 1);
	pathRow->addWidget(browse);

	encoderLabel = new QLabel(this);
	bitrateLabel = new QLabel(this);
	formatLabel = new QLabel(QString::fromUtf8(obs_module_text("FormatInherited")), this);
	formatLabel->setWordWrap(true);

	form->addRow(QString::fromUtf8(obs_module_text("CanvasPreset")), presetCombo);
	form->addRow(QString::fromUtf8(obs_module_text("Width")), widthSpin);
	form->addRow(QString::fromUtf8(obs_module_text("Height")), heightSpin);
	form->addRow(QString(), aspectHint);
	form->addRow(QString::fromUtf8(obs_module_text("ClipLength")), clipCombo);
	form->addRow(QString::fromUtf8(obs_module_text("CustomClipLength")), clipCustomSpin);
	form->addRow(QString::fromUtf8(obs_module_text("RecordingPath")), pathRow);
	form->addRow(QString::fromUtf8(obs_module_text("EncoderInherited")), encoderLabel);
	form->addRow(QString::fromUtf8(obs_module_text("BitrateInherited")), bitrateLabel);
	form->addRow(QString::fromUtf8(obs_module_text("FileFormat")), formatLabel);
	root->addLayout(form);

	auto *resetBtn = new QPushButton(QString::fromUtf8(obs_module_text("ResetDefaults")), this);
	connect(resetBtn, &QPushButton::clicked, this, &SettingsDialog::OnResetDefaults);
	root->addWidget(resetBtn);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::OnAccepted);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	root->addWidget(buttons);

	SyncFieldsFromSettings();
}

void SettingsDialog::SyncFieldsFromSettings()
{
	for (int i = 0; i < presetCombo->count(); ++i) {
		if (presetCombo->itemData(i).toInt() == (int)settings.canvasPreset) {
			presetCombo->setCurrentIndex(i);
			break;
		}
	}
	widthSpin->setValue((int)settings.customWidth);
	heightSpin->setValue((int)settings.customHeight);
	const bool custom = settings.canvasPreset == vsp::CanvasPreset::Custom;
	widthSpin->setEnabled(custom);
	heightSpin->setEnabled(custom);

	for (int i = 0; i < clipCombo->count(); ++i) {
		if (clipCombo->itemData(i).toInt() == (int)settings.clipPreset) {
			clipCombo->setCurrentIndex(i);
			break;
		}
	}
	clipCustomSpin->setValue(settings.customClipSeconds);
	clipCustomSpin->setEnabled(settings.clipPreset == vsp::ClipLengthPreset::Custom);

	pathEdit->setText(settings.recordingPath);

	if (outputs) {
		encoderLabel->setText(outputs->ActiveEncoderSummary());
		bitrateLabel->setText(outputs->ActiveBitrateSummary());
	}

	OnPresetChanged(presetCombo->currentIndex());
}

void SettingsDialog::OnPresetChanged(int)
{
	const auto preset = static_cast<vsp::CanvasPreset>(presetCombo->currentData().toInt());
	const bool custom = preset == vsp::CanvasPreset::Custom;
	widthSpin->setEnabled(custom);
	heightSpin->setEnabled(custom);
	uint32_t w = 0, h = 0;
	vsp::CanvasSizeForPreset(preset, (uint32_t)widthSpin->value(), (uint32_t)heightSpin->value(), w, h);
	if (!custom) {
		widthSpin->setValue((int)w);
		heightSpin->setValue((int)h);
	}
	if (vsp::IsPortrait((uint32_t)widthSpin->value(), (uint32_t)heightSpin->value())) {
		aspectHint->setText(QString::fromUtf8(obs_module_text("AspectVerticalOk")));
	} else {
		aspectHint->setText(QString::fromUtf8(obs_module_text("AspectNotVertical")));
	}
}

void SettingsDialog::OnClipPresetChanged(int)
{
	const auto preset = static_cast<vsp::ClipLengthPreset>(clipCombo->currentData().toInt());
	clipCustomSpin->setEnabled(preset == vsp::ClipLengthPreset::Custom);
}

void SettingsDialog::OnBrowsePath()
{
	const QString dir = QFileDialog::getExistingDirectory(this, QString::fromUtf8(obs_module_text("RecordingPath")),
							      pathEdit->text());
	if (!dir.isEmpty())
		pathEdit->setText(dir);
}

void SettingsDialog::OnResetDefaults()
{
	settings = vsp::PluginSettings{};
	settings.recordingPath = vsp::DefaultRecordingPath();
	SyncFieldsFromSettings();
}

bool SettingsDialog::ValidateAndCommit(QString *error)
{
	settings.canvasPreset = static_cast<vsp::CanvasPreset>(presetCombo->currentData().toInt());
	settings.customWidth = (uint32_t)widthSpin->value();
	settings.customHeight = (uint32_t)heightSpin->value();
	settings.clipPreset = static_cast<vsp::ClipLengthPreset>(clipCombo->currentData().toInt());
	settings.customClipSeconds = clipCustomSpin->value();
	settings.recordingPath = pathEdit->text().trimmed();

	uint32_t w = 0, h = 0;
	vsp::CanvasSizeForPreset(settings.canvasPreset, settings.customWidth, settings.customHeight, w, h);
	if (!vsp::ValidateCanvasSize((int)w, (int)h, error))
		return false;
	if (!vsp::ValidateClipSeconds(vsp::EffectiveClipSeconds(settings), error))
		return false;

	if (settings.recordingPath.isEmpty()) {
		if (error)
			*error = QString::fromUtf8(obs_module_text("RecordingPathRequired"));
		return false;
	}
	QDir dir(settings.recordingPath);
	if (!dir.exists() && !QDir().mkpath(settings.recordingPath)) {
		if (error)
			*error = QString::fromUtf8(obs_module_text("RecordingPathCreateFailed"));
		return false;
	}
	return true;
}

void SettingsDialog::OnAccepted()
{
	QString error;
	if (!ValidateAndCommit(&error)) {
		QMessageBox::warning(this, windowTitle(), error);
		return;
	}
	accept();
}
