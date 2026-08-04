#pragma once

#include "plugin-settings.hpp"
#include "vertical-outputs.hpp"

#include <QDialog>
#include <QStringList>

class QComboBox;
class QSpinBox;
class QLineEdit;
class QLabel;
class QCheckBox;
class QTabWidget;
class QDateEdit;
class QTimeEdit;

class SettingsDialog : public QDialog {
	Q_OBJECT
public:
	SettingsDialog(vsp::PluginSettings settings, VerticalOutputs *outputs, const QStringList &sceneNames,
		       const QStringList &sceneUuids, vsp::AutomationStatus automationStatus,
		       const QString &automationStatusText, QWidget *parent = nullptr);

	vsp::PluginSettings result() const { return settings; }
	bool WantsAutomationReset() const { return resetAutomation; }

private slots:
	void OnCanvasPresetChanged(int index);
	void OnShortClipPresetChanged(int index);
	void OnLongClipPresetChanged(int index);
	void OnBrowsePath();
	void OnResetDefaults();
	void OnResetAutomation();
	void OnStreamDestModeChanged(int index);
	void OnTestDestination();
	void OnAccepted();

private:
	void BuildCanvasTab(QWidget *tab);
	void BuildRecordingTab(QWidget *tab);
	void BuildClipsTab(QWidget *tab);
	void BuildAutomationTab(QWidget *tab);
	void BuildStreamingTab(QWidget *tab);
	void BuildAdvancedTab(QWidget *tab);
	void SyncFieldsFromSettings();
	bool ValidateAndCommit(QString *error, QString *warning);

	vsp::PluginSettings settings;
	VerticalOutputs *outputs = nullptr;
	QStringList sceneNames;
	QStringList sceneUuids;
	vsp::AutomationStatus automationStatus = vsp::AutomationStatus::Disabled;
	QString automationStatusText;
	bool resetAutomation = false;

	/* Canvas */
	QComboBox *presetCombo = nullptr;
	QSpinBox *widthSpin = nullptr;
	QSpinBox *heightSpin = nullptr;
	QLabel *aspectHint = nullptr;

	/* Recording */
	QLineEdit *pathEdit = nullptr;
	QLabel *encoderLabel = nullptr;
	QLabel *bitrateLabel = nullptr;
	QLabel *formatLabel = nullptr;
	QLabel *recordStatusLabel = nullptr;

	/* Clips */
	QComboBox *shortClipCombo = nullptr;
	QSpinBox *shortCustomSpin = nullptr;
	QComboBox *longClipCombo = nullptr;
	QLineEdit *longCustomEdit = nullptr; /* MM:SS */
	QSpinBox *longCustomMin = nullptr;
	QSpinBox *longCustomSec = nullptr;
	QLabel *longWarnLabel = nullptr;
	QCheckBox *clipBufferCheck = nullptr;
	QCheckBox *autoStartBufferCheck = nullptr;
	QCheckBox *stopIdleCheck = nullptr;
	QSpinBox *idleTimeoutSpin = nullptr;
	QCheckBox *saveAvailableCheck = nullptr;
	QCheckBox *bufferOnLiveCheck = nullptr;
	QCheckBox *bufferOnRecordCheck = nullptr;
	QLabel *bufferStatusInSettings = nullptr;

	/* Streaming destination */
	QComboBox *streamDestMode = nullptr;
	QLineEdit *verticalServerEdit = nullptr;
	QLineEdit *verticalKeyEdit = nullptr;
	QLabel *streamDestSummary = nullptr;
	QPushButton *testDestBtn = nullptr;

	/* Automation */
	QCheckBox *autoMaster = nullptr;
	QCheckBox *startMainStream = nullptr;
	QCheckBox *startScene = nullptr;
	QCheckBox *startObs = nullptr;
	QCheckBox *startSchedule = nullptr;
	QCheckBox *startCountdown = nullptr;
	QCheckBox *startVerticalLive = nullptr;
	QCheckBox *stopMainStream = nullptr;
	QCheckBox *stopScene = nullptr;
	QCheckBox *stopDuration = nullptr;
	QCheckBox *stopScheduleEnd = nullptr;
	QCheckBox *stopVerticalLive = nullptr;
	QCheckBox *stopObsShutdown = nullptr;
	QComboBox *sceneCombo = nullptr;
	QSpinBox *durationH = nullptr;
	QSpinBox *durationM = nullptr;
	QSpinBox *durationS = nullptr;
	QLabel *expectedStopLabel = nullptr;
	QSpinBox *countdownSpin = nullptr;
	QDateEdit *schedStartDate = nullptr;
	QTimeEdit *schedStartTime = nullptr;
	QDateEdit *schedEndDate = nullptr;
	QTimeEdit *schedEndTime = nullptr;
	QComboBox *schedRepeat = nullptr;
	QLabel *tzLabel = nullptr;
	QLabel *autoStatusLabel = nullptr;
	QCheckBox *confirmManualStop = nullptr;

	/* Streaming / Advanced */
	QLabel *streamHelp = nullptr;
};
