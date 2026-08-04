#pragma once

#include "plugin-settings.hpp"
#include "vertical-outputs.hpp"

#include <QDialog>

class QComboBox;
class QSpinBox;
class QLineEdit;
class QLabel;
class QCheckBox;

class SettingsDialog : public QDialog {
	Q_OBJECT
public:
	SettingsDialog(vsp::PluginSettings settings, VerticalOutputs *outputs, QWidget *parent = nullptr);

	vsp::PluginSettings result() const { return settings; }

private slots:
	void OnPresetChanged(int index);
	void OnClipPresetChanged(int index);
	void OnBrowsePath();
	void OnResetDefaults();
	void OnAccepted();

private:
	void SyncFieldsFromSettings();
	bool ValidateAndCommit(QString *error);

	vsp::PluginSettings settings;
	VerticalOutputs *outputs = nullptr;

	QComboBox *presetCombo = nullptr;
	QSpinBox *widthSpin = nullptr;
	QSpinBox *heightSpin = nullptr;
	QLabel *aspectHint = nullptr;
	QComboBox *clipCombo = nullptr;
	QSpinBox *clipCustomSpin = nullptr;
	QLineEdit *pathEdit = nullptr;
	QLabel *encoderLabel = nullptr;
	QLabel *bitrateLabel = nullptr;
	QLabel *formatLabel = nullptr;
};
