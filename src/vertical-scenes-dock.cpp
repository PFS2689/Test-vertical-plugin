#include "vertical-scenes-dock.hpp"
#include "shorts-dock.hpp"

#include <obs-module.h>

#include <QAbstractItemView>
#include <QLabel>
#include <QVBoxLayout>

namespace {

QString Translate(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

} // namespace

VerticalScenesDock::VerticalScenesDock(ShortsDock *workspace_, QWidget *parent) : QFrame(parent), workspace(workspace_)
{
	setObjectName(QStringLiteral("VerticalScenesDock"));
	BuildUI();

	if (workspace) {
		connect(workspace, &ShortsDock::verticalScenesChanged, this, &VerticalScenesDock::RefreshList);
		RefreshList();
	}
}

void VerticalScenesDock::BuildUI()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);
	root->setSpacing(4);

	auto *hint = new QLabel(Translate("MainScenesHint"), this);
	hint->setWordWrap(true);
	hint->setStyleSheet(QStringLiteral("color: #b0b0b0; font-size: 11px;"));
	root->addWidget(hint);

	scenesList = new QListWidget(this);
	scenesList->setSelectionMode(QAbstractItemView::SingleSelection);
	connect(scenesList, &QListWidget::itemSelectionChanged, this, &VerticalScenesDock::OnSelectionChanged);
	root->addWidget(scenesList, 1);
}

void VerticalScenesDock::RefreshList()
{
	if (!workspace || !scenesList)
		return;

	refreshing = true;
	workspace->PopulateScenesList(scenesList);
	refreshing = false;
}

void VerticalScenesDock::OnSelectionChanged()
{
	if (refreshing || !workspace || !scenesList)
		return;

	QListWidgetItem *item = scenesList->currentItem();
	if (!item)
		return;

	const QString uuid = item->data(Qt::UserRole).toString();
	if (uuid.isEmpty())
		return;

	workspace->RequestSelectScene(uuid);
}
