#include "settings-dialog.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimeEdit>
#include <QTimeZone>
#include <QVBoxLayout>

namespace {

QWidget *WrapScroll(QWidget *inner)
{
	auto *scroll = new QScrollArea();
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidget(inner);
	return scroll;
}

} // namespace

SettingsDialog::SettingsDialog(vsp::PluginSettings s, VerticalOutputs *outs, const QStringList &names,
			       const QStringList &uuids, vsp::AutomationStatus autoStatus, const QString &autoText,
			       QWidget *parent)
	: QDialog(parent),
	  settings(std::move(s)),
	  outputs(outs),
	  sceneNames(names),
	  sceneUuids(uuids),
	  automationStatus(autoStatus),
	  automationStatusText(autoText)
{
	setWindowTitle(QString::fromUtf8(obs_module_text("Settings")));
	setModal(true);
	resize(560, 560);

	auto *root = new QVBoxLayout(this);
	auto *tabs = new QTabWidget(this);

	auto *canvas = new QWidget();
	BuildCanvasTab(canvas);
	tabs->addTab(WrapScroll(canvas), QString::fromUtf8(obs_module_text("TabVerticalCanvas")));

	auto *recording = new QWidget();
	BuildRecordingTab(recording);
	tabs->addTab(WrapScroll(recording), QString::fromUtf8(obs_module_text("TabVerticalRecording")));

	auto *clips = new QWidget();
	BuildClipsTab(clips);
	tabs->addTab(WrapScroll(clips), QString::fromUtf8(obs_module_text("TabVerticalClips")));

	auto *automation = new QWidget();
	BuildAutomationTab(automation);
	tabs->addTab(WrapScroll(automation), QString::fromUtf8(obs_module_text("TabVerticalRecordingAutomation")));

	auto *streaming = new QWidget();
	BuildStreamingTab(streaming);
	tabs->addTab(WrapScroll(streaming), QString::fromUtf8(obs_module_text("TabVerticalStreaming")));

	auto *advanced = new QWidget();
	BuildAdvancedTab(advanced);
	tabs->addTab(WrapScroll(advanced), QString::fromUtf8(obs_module_text("TabAdvanced")));

	root->addWidget(tabs);

	auto *btnRow = new QHBoxLayout();
	auto *resetBtn = new QPushButton(QString::fromUtf8(obs_module_text("ResetDefaults")), this);
	connect(resetBtn, &QPushButton::clicked, this, &SettingsDialog::OnResetDefaults);
	btnRow->addWidget(resetBtn);
	btnRow->addStretch(1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::OnAccepted);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	btnRow->addWidget(buttons);
	root->addLayout(btnRow);

	SyncFieldsFromSettings();
}

void SettingsDialog::BuildCanvasTab(QWidget *tab)
{
	auto *form = new QFormLayout(tab);
	presetCombo = new QComboBox(tab);
	presetCombo->addItem(QString::fromUtf8(obs_module_text("PresetYouTube")),
			     (int)vsp::CanvasPreset::YouTubeVertical);
	presetCombo->addItem(QString::fromUtf8(obs_module_text("PresetTikTok")), (int)vsp::CanvasPreset::TikTokVertical);
	presetCombo->addItem(QString::fromUtf8(obs_module_text("PresetTwitch")), (int)vsp::CanvasPreset::TwitchVertical);
	presetCombo->addItem(QString::fromUtf8(obs_module_text("PresetInstagram")),
			     (int)vsp::CanvasPreset::InstagramVertical);
	presetCombo->addItem(QString::fromUtf8(obs_module_text("PresetCustom")), (int)vsp::CanvasPreset::Custom);
	connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&SettingsDialog::OnCanvasPresetChanged);

	widthSpin = new QSpinBox(tab);
	widthSpin->setRange(160, 7680);
	heightSpin = new QSpinBox(tab);
	heightSpin->setRange(160, 7680);
	aspectHint = new QLabel(tab);
	aspectHint->setWordWrap(true);

	auto *help = new QLabel(QString::fromUtf8(obs_module_text("CanvasHelp")), tab);
	help->setWordWrap(true);

	form->addRow(help);
	form->addRow(QString::fromUtf8(obs_module_text("CanvasPreset")), presetCombo);
	form->addRow(QString::fromUtf8(obs_module_text("Width")), widthSpin);
	form->addRow(QString::fromUtf8(obs_module_text("Height")), heightSpin);
	form->addRow(QString(), aspectHint);
}

void SettingsDialog::BuildRecordingTab(QWidget *tab)
{
	auto *form = new QFormLayout(tab);
	pathEdit = new QLineEdit(tab);
	auto *browse = new QPushButton(QString::fromUtf8(obs_module_text("Browse")), tab);
	connect(browse, &QPushButton::clicked, this, &SettingsDialog::OnBrowsePath);
	auto *pathRow = new QHBoxLayout();
	pathRow->addWidget(pathEdit, 1);
	pathRow->addWidget(browse);

	encoderLabel = new QLabel(tab);
	bitrateLabel = new QLabel(tab);
	formatLabel = new QLabel(QString::fromUtf8(obs_module_text("FormatInherited")), tab);
	formatLabel->setWordWrap(true);
	recordStatusLabel = new QLabel(tab);
	recordStatusLabel->setWordWrap(true);

	form->addRow(QString::fromUtf8(obs_module_text("RecordingPath")), pathRow);
	form->addRow(QString::fromUtf8(obs_module_text("EncoderInherited")), encoderLabel);
	form->addRow(QString::fromUtf8(obs_module_text("BitrateInherited")), bitrateLabel);
	form->addRow(QString::fromUtf8(obs_module_text("FileFormat")), formatLabel);
	form->addRow(QString::fromUtf8(obs_module_text("RecordingStatus")), recordStatusLabel);
}

void SettingsDialog::BuildClipsTab(QWidget *tab)
{
	auto *root = new QVBoxLayout(tab);

	auto *shortBox = new QGroupBox(QString::fromUtf8(obs_module_text("ShortClip")), tab);
	auto *shortForm = new QFormLayout(shortBox);
	shortClipCombo = new QComboBox(shortBox);
	shortClipCombo->addItem(QString::fromUtf8(obs_module_text("Clip10")), (int)vsp::ShortClipPreset::Sec10);
	shortClipCombo->addItem(QString::fromUtf8(obs_module_text("Clip20")), (int)vsp::ShortClipPreset::Sec20);
	shortClipCombo->addItem(QString::fromUtf8(obs_module_text("Clip30")), (int)vsp::ShortClipPreset::Sec30);
	shortClipCombo->addItem(QString::fromUtf8(obs_module_text("Clip60")), (int)vsp::ShortClipPreset::Sec60);
	shortClipCombo->addItem(QString::fromUtf8(obs_module_text("ClipCustom")), (int)vsp::ShortClipPreset::Custom);
	connect(shortClipCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&SettingsDialog::OnShortClipPresetChanged);
	shortCustomSpin = new QSpinBox(shortBox);
	shortCustomSpin->setRange(1, vsp::kMaxClipBufferSeconds);
	shortCustomSpin->setSuffix(QStringLiteral(" s"));
	shortForm->addRow(QString::fromUtf8(obs_module_text("ShortClipLength")), shortClipCombo);
	shortForm->addRow(QString::fromUtf8(obs_module_text("CustomShortClipLength")), shortCustomSpin);
	root->addWidget(shortBox);

	auto *longBox = new QGroupBox(QString::fromUtf8(obs_module_text("LongClip")), tab);
	auto *longForm = new QFormLayout(longBox);
	longClipCombo = new QComboBox(longBox);
	longClipCombo->addItem(QString::fromUtf8(obs_module_text("LongClip2Min")), (int)vsp::LongClipPreset::Min2);
	longClipCombo->addItem(QString::fromUtf8(obs_module_text("LongClip3Min")), (int)vsp::LongClipPreset::Min3);
	longClipCombo->addItem(QString::fromUtf8(obs_module_text("LongClip4Min")), (int)vsp::LongClipPreset::Min4);
	longClipCombo->addItem(QString::fromUtf8(obs_module_text("LongClip5Min")), (int)vsp::LongClipPreset::Min5);
	longClipCombo->addItem(QString::fromUtf8(obs_module_text("LongClipCustom")), (int)vsp::LongClipPreset::Custom);
	connect(longClipCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&SettingsDialog::OnLongClipPresetChanged);

	auto *customRow = new QHBoxLayout();
	longCustomMin = new QSpinBox(longBox);
	longCustomMin->setRange(0, vsp::kMaxClipBufferSeconds / 60);
	longCustomMin->setSuffix(QStringLiteral(" min"));
	longCustomSec = new QSpinBox(longBox);
	longCustomSec->setRange(0, 59);
	longCustomSec->setSuffix(QStringLiteral(" s"));
	longCustomEdit = new QLineEdit(longBox);
	longCustomEdit->setPlaceholderText(QStringLiteral("MM:SS"));
	longCustomEdit->setMaximumWidth(80);
	customRow->addWidget(longCustomMin);
	customRow->addWidget(longCustomSec);
	customRow->addWidget(new QLabel(QStringLiteral("or"), longBox));
	customRow->addWidget(longCustomEdit);
	customRow->addStretch(1);

	longWarnLabel = new QLabel(longBox);
	longWarnLabel->setWordWrap(true);
	longWarnLabel->setStyleSheet(QStringLiteral("color: #a06000;"));

	longForm->addRow(QString::fromUtf8(obs_module_text("LongClipLength")), longClipCombo);
	longForm->addRow(QString::fromUtf8(obs_module_text("CustomLongClipLength")), customRow);
	longForm->addRow(QString(), longWarnLabel);
	root->addWidget(longBox);

	clipBufferCheck = new QCheckBox(QString::fromUtf8(obs_module_text("ClipBufferEnabled")), tab);
	root->addWidget(clipBufferCheck);

	auto *note = new QLabel(QString::fromUtf8(obs_module_text("ClipsHelp")), tab);
	note->setWordWrap(true);
	root->addWidget(note);
	root->addStretch(1);
}

void SettingsDialog::BuildAutomationTab(QWidget *tab)
{
	auto *root = new QVBoxLayout(tab);

	autoMaster = new QCheckBox(QString::fromUtf8(obs_module_text("AutomationMaster")), tab);
	root->addWidget(autoMaster);

	auto *help = new QLabel(QString::fromUtf8(obs_module_text("AutomationHelp")), tab);
	help->setWordWrap(true);
	root->addWidget(help);

	autoStatusLabel = new QLabel(tab);
	autoStatusLabel->setWordWrap(true);
	root->addWidget(autoStatusLabel);

	tzLabel = new QLabel(QStringLiteral("Time zone: %1").arg(QString::fromUtf8(QTimeZone::systemTimeZoneId())),
			     tab);
	root->addWidget(tzLabel);

	auto *startBox = new QGroupBox(QString::fromUtf8(obs_module_text("AutoStartTriggers")), tab);
	auto *startLay = new QVBoxLayout(startBox);
	startMainStream = new QCheckBox(QString::fromUtf8(obs_module_text("StartOnMainStream")), startBox);
	startScene = new QCheckBox(QString::fromUtf8(obs_module_text("StartOnScene")), startBox);
	startObs = new QCheckBox(QString::fromUtf8(obs_module_text("StartOnObsStart")), startBox);
	startSchedule = new QCheckBox(QString::fromUtf8(obs_module_text("StartOnSchedule")), startBox);
	startCountdown = new QCheckBox(QString::fromUtf8(obs_module_text("StartOnCountdown")), startBox);
	startVerticalLive = new QCheckBox(QString::fromUtf8(obs_module_text("StartOnVerticalLive")), startBox);
	startLay->addWidget(startMainStream);
	startLay->addWidget(startScene);
	startLay->addWidget(startObs);
	startLay->addWidget(startSchedule);
	startLay->addWidget(startCountdown);
	startLay->addWidget(startVerticalLive);
	root->addWidget(startBox);

	auto *stopBox = new QGroupBox(QString::fromUtf8(obs_module_text("AutoStopTriggers")), tab);
	auto *stopLay = new QVBoxLayout(stopBox);
	stopMainStream = new QCheckBox(QString::fromUtf8(obs_module_text("StopOnMainStream")), stopBox);
	stopScene = new QCheckBox(QString::fromUtf8(obs_module_text("StopOnSceneInactive")), stopBox);
	stopDuration = new QCheckBox(QString::fromUtf8(obs_module_text("StopOnDuration")), stopBox);
	stopScheduleEnd = new QCheckBox(QString::fromUtf8(obs_module_text("StopOnScheduleEnd")), stopBox);
	stopVerticalLive = new QCheckBox(QString::fromUtf8(obs_module_text("StopOnVerticalLive")), stopBox);
	stopObsShutdown = new QCheckBox(QString::fromUtf8(obs_module_text("StopOnObsShutdown")), stopBox);
	stopLay->addWidget(stopMainStream);
	stopLay->addWidget(stopScene);
	stopLay->addWidget(stopDuration);
	stopLay->addWidget(stopScheduleEnd);
	stopLay->addWidget(stopVerticalLive);
	stopLay->addWidget(stopObsShutdown);
	root->addWidget(stopBox);

	auto *sceneForm = new QFormLayout();
	sceneCombo = new QComboBox(tab);
	sceneCombo->addItem(QString::fromUtf8(obs_module_text("NoSceneSelected")), QString());
	for (int i = 0; i < sceneNames.size(); ++i) {
		const QString uuid = i < sceneUuids.size() ? sceneUuids[i] : QString();
		sceneCombo->addItem(sceneNames[i], uuid);
	}
	sceneForm->addRow(QString::fromUtf8(obs_module_text("TriggerScene")), sceneCombo);
	root->addLayout(sceneForm);

	auto *durRow = new QHBoxLayout();
	durationH = new QSpinBox(tab);
	durationH->setRange(0, 24);
	durationH->setSuffix(QStringLiteral(" h"));
	durationM = new QSpinBox(tab);
	durationM->setRange(0, 59);
	durationM->setSuffix(QStringLiteral(" m"));
	durationS = new QSpinBox(tab);
	durationS->setRange(0, 59);
	durationS->setSuffix(QStringLiteral(" s"));
	durRow->addWidget(durationH);
	durRow->addWidget(durationM);
	durRow->addWidget(durationS);
	durRow->addStretch(1);
	expectedStopLabel = new QLabel(tab);
	auto *durForm = new QFormLayout();
	durForm->addRow(QString::fromUtf8(obs_module_text("AutoRecordDuration")), durRow);
	durForm->addRow(QString(), expectedStopLabel);
	countdownSpin = new QSpinBox(tab);
	countdownSpin->setRange(1, 3600);
	countdownSpin->setSuffix(QStringLiteral(" s"));
	durForm->addRow(QString::fromUtf8(obs_module_text("CountdownSeconds")), countdownSpin);
	root->addLayout(durForm);

	auto *schedBox = new QGroupBox(QString::fromUtf8(obs_module_text("Schedule")), tab);
	auto *schedForm = new QFormLayout(schedBox);
	schedStartDate = new QDateEdit(tab);
	schedStartDate->setCalendarPopup(true);
	schedStartDate->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
	schedStartTime = new QTimeEdit(tab);
	schedStartTime->setDisplayFormat(QStringLiteral("HH:mm"));
	schedEndDate = new QDateEdit(tab);
	schedEndDate->setCalendarPopup(true);
	schedEndDate->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
	schedEndTime = new QTimeEdit(tab);
	schedEndTime->setDisplayFormat(QStringLiteral("HH:mm"));
	schedRepeat = new QComboBox(tab);
	schedRepeat->addItem(QString::fromUtf8(obs_module_text("RepeatOnce")), (int)vsp::ScheduleRepeat::Once);
	schedRepeat->addItem(QString::fromUtf8(obs_module_text("RepeatDaily")), (int)vsp::ScheduleRepeat::Daily);
	schedRepeat->addItem(QString::fromUtf8(obs_module_text("RepeatWeekly")), (int)vsp::ScheduleRepeat::Weekly);
	schedRepeat->addItem(QString::fromUtf8(obs_module_text("RepeatWeekdays")), (int)vsp::ScheduleRepeat::Weekdays);
	schedForm->addRow(QString::fromUtf8(obs_module_text("ScheduleStartDate")), schedStartDate);
	schedForm->addRow(QString::fromUtf8(obs_module_text("ScheduleStartTime")), schedStartTime);
	schedForm->addRow(QString::fromUtf8(obs_module_text("ScheduleEndDate")), schedEndDate);
	schedForm->addRow(QString::fromUtf8(obs_module_text("ScheduleEndTime")), schedEndTime);
	schedForm->addRow(QString::fromUtf8(obs_module_text("ScheduleRepeat")), schedRepeat);
	auto *schedNote = new QLabel(QString::fromUtf8(obs_module_text("ScheduleNote")), schedBox);
	schedNote->setWordWrap(true);
	schedForm->addRow(schedNote);
	root->addWidget(schedBox);

	confirmManualStop = new QCheckBox(QString::fromUtf8(obs_module_text("ConfirmManualStop")), tab);
	root->addWidget(confirmManualStop);

	auto *resetAuto = new QPushButton(QString::fromUtf8(obs_module_text("ResetAutomation")), tab);
	connect(resetAuto, &QPushButton::clicked, this, &SettingsDialog::OnResetAutomation);
	root->addWidget(resetAuto);
	root->addStretch(1);
}

void SettingsDialog::BuildStreamingTab(QWidget *tab)
{
	auto *lay = new QVBoxLayout(tab);
	streamHelp = new QLabel(QString::fromUtf8(obs_module_text("StreamingHelp")), tab);
	streamHelp->setWordWrap(true);
	lay->addWidget(streamHelp);
	lay->addStretch(1);
}

void SettingsDialog::BuildAdvancedTab(QWidget *tab)
{
	auto *lay = new QVBoxLayout(tab);
	auto *label = new QLabel(QString::fromUtf8(obs_module_text("AdvancedHelp")), tab);
	label->setWordWrap(true);
	lay->addWidget(label);
	lay->addStretch(1);
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
	OnCanvasPresetChanged(presetCombo->currentIndex());

	for (int i = 0; i < shortClipCombo->count(); ++i) {
		if (shortClipCombo->itemData(i).toInt() == (int)settings.shortClipPreset) {
			shortClipCombo->setCurrentIndex(i);
			break;
		}
	}
	shortCustomSpin->setValue(settings.customShortClipSeconds);
	OnShortClipPresetChanged(shortClipCombo->currentIndex());

	for (int i = 0; i < longClipCombo->count(); ++i) {
		if (longClipCombo->itemData(i).toInt() == (int)settings.longClipPreset) {
			longClipCombo->setCurrentIndex(i);
			break;
		}
	}
	longCustomMin->setValue(settings.customLongClipSeconds / 60);
	longCustomSec->setValue(settings.customLongClipSeconds % 60);
	longCustomEdit->setText(QStringLiteral("%1:%2")
					.arg(settings.customLongClipSeconds / 60, 2, 10, QLatin1Char('0'))
					.arg(settings.customLongClipSeconds % 60, 2, 10, QLatin1Char('0')));
	OnLongClipPresetChanged(longClipCombo->currentIndex());

	pathEdit->setText(settings.recordingPath);
	clipBufferCheck->setChecked(settings.clipBufferEnabled);

	if (outputs) {
		encoderLabel->setText(outputs->ActiveEncoderSummary());
		bitrateLabel->setText(outputs->ActiveBitrateSummary());
		recordStatusLabel->setText(outputs->RecordingStatusSummary());
	}

	autoMaster->setChecked(settings.automationEnabled);
	startMainStream->setChecked(settings.autoStartOnMainStream);
	startScene->setChecked(settings.autoStartOnScene);
	startObs->setChecked(settings.autoStartOnObsStart);
	startSchedule->setChecked(settings.autoStartOnSchedule);
	startCountdown->setChecked(settings.autoStartOnCountdown);
	startVerticalLive->setChecked(settings.autoStartOnVerticalLive);
	stopMainStream->setChecked(settings.autoStopOnMainStreamStop);
	stopScene->setChecked(settings.autoStopOnSceneInactive);
	stopDuration->setChecked(settings.autoStopOnDuration);
	stopScheduleEnd->setChecked(settings.autoStopOnScheduleEnd);
	stopVerticalLive->setChecked(settings.autoStopOnVerticalLiveStop);
	stopObsShutdown->setChecked(settings.autoStopOnObsShutdown);
	confirmManualStop->setChecked(settings.confirmManualStopDuringAutomation);

	int sceneIdx = 0;
	for (int i = 0; i < sceneCombo->count(); ++i) {
		if (sceneCombo->itemData(i).toString() == settings.triggerSceneUuid ||
		    sceneCombo->itemText(i) == settings.triggerSceneName) {
			sceneIdx = i;
			break;
		}
	}
	sceneCombo->setCurrentIndex(sceneIdx);

	durationH->setValue(settings.autoRecordDurationSeconds / 3600);
	durationM->setValue((settings.autoRecordDurationSeconds % 3600) / 60);
	durationS->setValue(settings.autoRecordDurationSeconds % 60);
	countdownSpin->setValue(settings.countdownSeconds);

	const QDate sd = QDate::fromString(settings.scheduleStartDate, QStringLiteral("yyyy-MM-dd"));
	schedStartDate->setDate(sd.isValid() ? sd : QDate::currentDate());
	const QTime st = QTime::fromString(settings.scheduleStartTime, QStringLiteral("HH:mm"));
	schedStartTime->setTime(st.isValid() ? st : QTime::currentTime());
	const QDate ed = QDate::fromString(settings.scheduleEndDate, QStringLiteral("yyyy-MM-dd"));
	schedEndDate->setDate(ed.isValid() ? ed : QDate::currentDate());
	const QTime et = QTime::fromString(settings.scheduleEndTime, QStringLiteral("HH:mm"));
	schedEndTime->setTime(et.isValid() ? et : QTime::currentTime().addSecs(3600));

	for (int i = 0; i < schedRepeat->count(); ++i) {
		if (schedRepeat->itemData(i).toInt() == (int)settings.scheduleRepeat) {
			schedRepeat->setCurrentIndex(i);
			break;
		}
	}

	autoStatusLabel->setText(QStringLiteral("Status: %1").arg(
		automationStatusText.isEmpty() ? vsp::AutomationStatusLabel(automationStatus) : automationStatusText));
}

void SettingsDialog::OnCanvasPresetChanged(int)
{
	const auto preset = static_cast<vsp::CanvasPreset>(presetCombo->currentData().toInt());
	const bool custom = preset == vsp::CanvasPreset::Custom;
	widthSpin->setEnabled(custom);
	heightSpin->setEnabled(custom);
	if (!custom) {
		uint32_t w = 0, h = 0;
		vsp::CanvasSizeForPreset(preset, settings.customWidth, settings.customHeight, w, h);
		widthSpin->setValue((int)w);
		heightSpin->setValue((int)h);
	}
	aspectHint->setText(vsp::IsPortrait((uint32_t)widthSpin->value(), (uint32_t)heightSpin->value())
				    ? QString::fromUtf8(obs_module_text("PortraitHint"))
				    : QString::fromUtf8(obs_module_text("LandscapeHint")));
}

void SettingsDialog::OnShortClipPresetChanged(int)
{
	const auto preset = static_cast<vsp::ShortClipPreset>(shortClipCombo->currentData().toInt());
	shortCustomSpin->setEnabled(preset == vsp::ShortClipPreset::Custom);
}

void SettingsDialog::OnLongClipPresetChanged(int)
{
	const auto preset = static_cast<vsp::LongClipPreset>(longClipCombo->currentData().toInt());
	const bool custom = preset == vsp::LongClipPreset::Custom;
	longCustomMin->setEnabled(custom);
	longCustomSec->setEnabled(custom);
	longCustomEdit->setEnabled(custom);

	int secs = custom ? (longCustomMin->value() * 60 + longCustomSec->value()) : (int)preset;
	QString warn;
	vsp::ValidateLongClipSeconds(secs > 0 ? secs : 1, nullptr, &warn);
	longWarnLabel->setText(warn);
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

void SettingsDialog::OnResetAutomation()
{
	resetAutomation = true;
	settings.automationEnabled = false;
	settings.autoStartOnMainStream = false;
	settings.autoStartOnScene = false;
	settings.autoStartOnObsStart = false;
	settings.autoStartOnSchedule = false;
	settings.autoStartOnCountdown = false;
	settings.autoStartOnVerticalLive = false;
	settings.autoStopOnMainStreamStop = false;
	settings.autoStopOnSceneInactive = false;
	settings.autoStopOnDuration = false;
	settings.autoStopOnScheduleEnd = false;
	settings.autoStopOnVerticalLiveStop = false;
	settings.autoStopOnObsShutdown = true;
	settings.triggerSceneUuid.clear();
	settings.triggerSceneName.clear();
	settings.autoRecordDurationSeconds = 3600;
	settings.countdownSeconds = 60;
	settings.scheduleStartDate.clear();
	settings.scheduleStartTime.clear();
	settings.scheduleEndDate.clear();
	settings.scheduleEndTime.clear();
	settings.scheduleRepeat = vsp::ScheduleRepeat::Once;
	settings.scheduleWeekdaysMask = 0;
	SyncFieldsFromSettings();
}

bool SettingsDialog::ValidateAndCommit(QString *error, QString *warning)
{
	settings.canvasPreset = static_cast<vsp::CanvasPreset>(presetCombo->currentData().toInt());
	settings.customWidth = (uint32_t)widthSpin->value();
	settings.customHeight = (uint32_t)heightSpin->value();
	if (!vsp::ValidateCanvasSize(widthSpin->value(), heightSpin->value(), error))
		return false;

	settings.shortClipPreset = static_cast<vsp::ShortClipPreset>(shortClipCombo->currentData().toInt());
	settings.customShortClipSeconds = shortCustomSpin->value();
	if (settings.shortClipPreset == vsp::ShortClipPreset::Custom &&
	    !vsp::ValidateShortClipSeconds(settings.customShortClipSeconds, error))
		return false;

	settings.longClipPreset = static_cast<vsp::LongClipPreset>(longClipCombo->currentData().toInt());
	if (settings.longClipPreset == vsp::LongClipPreset::Custom) {
		int secs = -1;
		const QString mmss = longCustomEdit->text().trimmed();
		if (!mmss.isEmpty() && mmss.contains(QLatin1Char(':'))) {
			secs = vsp::ParseMmSs(mmss, error);
			if (secs < 0)
				return false;
		} else {
			secs = longCustomMin->value() * 60 + longCustomSec->value();
		}
		QString warn;
		if (!vsp::ValidateLongClipSeconds(secs, error, &warn))
			return false;
		settings.customLongClipSeconds = secs;
		if (warning && !warn.isEmpty())
			*warning = warn;
	}

	settings.recordingPath = pathEdit->text().trimmed();
	settings.clipBufferEnabled = clipBufferCheck->isChecked();

	settings.automationEnabled = autoMaster->isChecked();
	settings.autoStartOnMainStream = startMainStream->isChecked();
	settings.autoStartOnScene = startScene->isChecked();
	settings.autoStartOnObsStart = startObs->isChecked();
	settings.autoStartOnSchedule = startSchedule->isChecked();
	settings.autoStartOnCountdown = startCountdown->isChecked();
	settings.autoStartOnVerticalLive = startVerticalLive->isChecked();
	settings.autoStopOnMainStreamStop = stopMainStream->isChecked();
	settings.autoStopOnSceneInactive = stopScene->isChecked();
	settings.autoStopOnDuration = stopDuration->isChecked();
	settings.autoStopOnScheduleEnd = stopScheduleEnd->isChecked();
	settings.autoStopOnVerticalLiveStop = stopVerticalLive->isChecked();
	settings.autoStopOnObsShutdown = stopObsShutdown->isChecked();
	settings.confirmManualStopDuringAutomation = confirmManualStop->isChecked();

	settings.triggerSceneUuid = sceneCombo->currentData().toString();
	settings.triggerSceneName = sceneCombo->currentIndex() > 0 ? sceneCombo->currentText() : QString();

	settings.autoRecordDurationSeconds = durationH->value() * 3600 + durationM->value() * 60 + durationS->value();
	if (settings.autoStopOnDuration && !vsp::ValidateAutoDurationSeconds(settings.autoRecordDurationSeconds, error))
		return false;

	settings.countdownSeconds = countdownSpin->value();
	settings.scheduleStartDate = schedStartDate->date().toString(QStringLiteral("yyyy-MM-dd"));
	settings.scheduleStartTime = schedStartTime->time().toString(QStringLiteral("HH:mm"));
	settings.scheduleEndDate = schedEndDate->date().toString(QStringLiteral("yyyy-MM-dd"));
	settings.scheduleEndTime = schedEndTime->time().toString(QStringLiteral("HH:mm"));
	settings.scheduleRepeat = static_cast<vsp::ScheduleRepeat>(schedRepeat->currentData().toInt());

	return true;
}

void SettingsDialog::OnAccepted()
{
	QString error;
	QString warning;
	if (!ValidateAndCommit(&error, &warning)) {
		QMessageBox::warning(this, QString::fromUtf8(obs_module_text("Settings")), error);
		return;
	}
	if (!warning.isEmpty()) {
		QMessageBox::information(this, QString::fromUtf8(obs_module_text("Settings")), warning);
	}
	accept();
}
