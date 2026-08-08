#pragma once

#include <QFrame>

class QListWidget;
class ShortsDock;

class VerticalSourcesDock : public QFrame {
	Q_OBJECT

public:
	explicit VerticalSourcesDock(ShortsDock *workspace, QWidget *parent = nullptr);

private slots:
	void RefreshSources();
	void OnSelectionChanged();
	void OnAdd();
	void OnRemove();
	void OnToggleVisible();
	void OnToggleLock();
	void OnProperties();
	void OnFilters();
	void OnMoveUp();
	void OnMoveDown();

private:
	void BuildUI();

	ShortsDock *workspace = nullptr;
	QListWidget *sourcesList = nullptr;
	bool refreshing = false;
};
