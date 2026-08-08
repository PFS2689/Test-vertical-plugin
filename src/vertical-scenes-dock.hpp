#pragma once

#include <QFrame>

class QListWidget;
class ShortsDock;

/* Lists main OBS scenes (not a parallel vertical scene collection). */
class VerticalScenesDock : public QFrame {
	Q_OBJECT

public:
	explicit VerticalScenesDock(ShortsDock *workspace, QWidget *parent = nullptr);

private slots:
	void RefreshList();
	void OnSelectionChanged();

private:
	void BuildUI();

	ShortsDock *workspace = nullptr;
	QListWidget *scenesList = nullptr;
	bool refreshing = false;
};
