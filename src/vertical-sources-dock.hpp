#pragma once

#include <QFrame>

class QDoubleSpinBox;
class QListWidget;
class ShortsDock;

class VerticalSourcesDock : public QFrame {
	Q_OBJECT

public:
	explicit VerticalSourcesDock(ShortsDock *workspace, QWidget *parent = nullptr);

private slots:
	void RefreshSources();
	void RefreshTransform();
	void OnSelectionChanged();
	void OnAdd();
	void OnRemove();
	void OnToggleVisible();
	void OnToggleLock();
	void OnProperties();
	void OnFilters();
	void OnMoveUp();
	void OnMoveDown();
	void OnFitToScreen();
	void OnStretchToScreen();
	void OnCenterToScreen();
	void OnResetTransform();
	void OnTransformEdited();

private:
	void BuildUI();

	ShortsDock *workspace = nullptr;
	QListWidget *sourcesList = nullptr;
	QDoubleSpinBox *posXSpin = nullptr;
	QDoubleSpinBox *posYSpin = nullptr;
	QDoubleSpinBox *sizeWSpin = nullptr;
	QDoubleSpinBox *sizeHSpin = nullptr;
	QDoubleSpinBox *rotSpin = nullptr;
	bool refreshing = false;
	bool updatingTransform = false;
};
