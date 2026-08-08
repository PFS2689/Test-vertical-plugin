#include "vertical-sources-dock.hpp"
#include "shorts-dock.hpp"

#include <obs-module.h>

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QListWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QString Translate(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QToolButton *MakeToolButton(QWidget *parent, const QString &text, const QString &tip)
{
	auto *btn = new QToolButton(parent);
	btn->setText(text);
	btn->setToolTip(tip);
	btn->setAutoRaise(true);
	return btn;
}

} // namespace

VerticalSourcesDock::VerticalSourcesDock(ShortsDock *workspace_, QWidget *parent)
	: QFrame(parent),
	  workspace(workspace_)
{
	setObjectName(QStringLiteral("VerticalSourcesDock"));
	BuildUI();

	if (workspace) {
		connect(workspace, &ShortsDock::verticalSourcesChanged, this, &VerticalSourcesDock::RefreshSources);
		RefreshSources();
	}
}

void VerticalSourcesDock::BuildUI()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);
	root->setSpacing(4);

	sourcesList = new QListWidget(this);
	sourcesList->setSelectionMode(QAbstractItemView::ExtendedSelection);
	connect(sourcesList, &QListWidget::itemSelectionChanged, this, &VerticalSourcesDock::OnSelectionChanged);
	root->addWidget(sourcesList, 1);

	auto *sourceBtns = new QHBoxLayout();
	auto *refreshBtn = MakeToolButton(this, QStringLiteral("↻"), Translate("AddSource"));
	auto *visBtn = MakeToolButton(this, QStringLiteral("Vis"), Translate("ToggleVisible"));
	auto *lockBtn = MakeToolButton(this, QStringLiteral("Lock"), Translate("ToggleLock"));
	auto *propsBtn = MakeToolButton(this, QStringLiteral("Prop"), Translate("SourceProperties"));
	auto *filtersBtn = MakeToolButton(this, QStringLiteral("Filt"), Translate("SourceFilters"));
	connect(refreshBtn, &QToolButton::clicked, this, &VerticalSourcesDock::OnAdd);
	connect(visBtn, &QToolButton::clicked, this, &VerticalSourcesDock::OnToggleVisible);
	connect(lockBtn, &QToolButton::clicked, this, &VerticalSourcesDock::OnToggleLock);
	connect(propsBtn, &QToolButton::clicked, this, &VerticalSourcesDock::OnProperties);
	connect(filtersBtn, &QToolButton::clicked, this, &VerticalSourcesDock::OnFilters);
	for (auto *b : {refreshBtn, visBtn, lockBtn, propsBtn, filtersBtn})
		sourceBtns->addWidget(b);
	sourceBtns->addStretch(1);
	root->addLayout(sourceBtns);
}

void VerticalSourcesDock::RefreshSources()
{
	if (!workspace || !sourcesList)
		return;

	refreshing = true;
	workspace->PopulateSourcesList(sourcesList);
	refreshing = false;
}

void VerticalSourcesDock::OnSelectionChanged()
{
	if (refreshing || !workspace || !sourcesList)
		return;

	QListWidgetItem *item = sourcesList->currentItem();
	if (!item)
		return;

	const qint64 itemId = item->data(Qt::UserRole).toLongLong();
	workspace->RequestSelectSource(itemId);
}

void VerticalSourcesDock::OnAdd()
{
	if (workspace)
		workspace->RequestAddSource();
}

void VerticalSourcesDock::OnRemove()
{
	if (workspace)
		workspace->RequestRemoveSource();
}

void VerticalSourcesDock::OnToggleVisible()
{
	if (workspace)
		workspace->RequestToggleSourceVisible();
}

void VerticalSourcesDock::OnToggleLock()
{
	if (workspace)
		workspace->RequestToggleSourceLock();
}

void VerticalSourcesDock::OnProperties()
{
	if (workspace)
		workspace->RequestSourceProperties();
}

void VerticalSourcesDock::OnFilters()
{
	if (workspace)
		workspace->RequestSourceFilters();
}

void VerticalSourcesDock::OnMoveUp()
{
	if (workspace)
		workspace->RequestSourceMoveUp();
}

void VerticalSourcesDock::OnMoveDown()
{
	if (workspace)
		workspace->RequestSourceMoveDown();
}
