#include "vertical-sources-dock.hpp"
#include "shorts-dock.hpp"

#include <obs-module.h>

#include <QAbstractItemView>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
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
		connect(workspace, &ShortsDock::verticalTransformChanged, this, &VerticalSourcesDock::RefreshTransform);
		RefreshSources();
		RefreshTransform();
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
	auto *addSrcBtn = MakeToolButton(this, QStringLiteral("+"), Translate("AddSource"));
	auto *removeSrcBtn = MakeToolButton(this, QStringLiteral("\u2212"), Translate("RemoveSource"));
	auto *visBtn = MakeToolButton(this, QStringLiteral("Vis"), Translate("ToggleVisible"));
	auto *lockBtn = MakeToolButton(this, QStringLiteral("Lock"), Translate("ToggleLock"));
	auto *propsBtn = MakeToolButton(this, QStringLiteral("Prop"), Translate("SourceProperties"));
	auto *filtersBtn = MakeToolButton(this, QStringLiteral("Filt"), Translate("SourceFilters"));
	auto *upBtn = MakeToolButton(this, QStringLiteral("Up"), Translate("MoveSourceUp"));
	auto *downBtn = MakeToolButton(this, QStringLiteral("Dn"), Translate("MoveSourceDown"));
	connect(addSrcBtn, &QToolButton::clicked, this, &VerticalSourcesDock::OnAdd);
	connect(removeSrcBtn, &QToolButton::clicked, this, &VerticalSourcesDock::OnRemove);
	connect(visBtn, &QToolButton::clicked, this, &VerticalSourcesDock::OnToggleVisible);
	connect(lockBtn, &QToolButton::clicked, this, &VerticalSourcesDock::OnToggleLock);
	connect(propsBtn, &QToolButton::clicked, this, &VerticalSourcesDock::OnProperties);
	connect(filtersBtn, &QToolButton::clicked, this, &VerticalSourcesDock::OnFilters);
	connect(upBtn, &QToolButton::clicked, this, &VerticalSourcesDock::OnMoveUp);
	connect(downBtn, &QToolButton::clicked, this, &VerticalSourcesDock::OnMoveDown);
	for (auto *b : {addSrcBtn, removeSrcBtn, visBtn, lockBtn, propsBtn, filtersBtn, upBtn, downBtn})
		sourceBtns->addWidget(b);
	sourceBtns->addStretch(1);
	root->addLayout(sourceBtns);

	auto *form = new QGridLayout();
	posXSpin = new QDoubleSpinBox(this);
	posYSpin = new QDoubleSpinBox(this);
	sizeWSpin = new QDoubleSpinBox(this);
	sizeHSpin = new QDoubleSpinBox(this);
	rotSpin = new QDoubleSpinBox(this);
	for (auto *spin : {posXSpin, posYSpin, sizeWSpin, sizeHSpin}) {
		spin->setRange(-100000.0, 100000.0);
		spin->setDecimals(1);
		spin->setSingleStep(1.0);
		connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
			&VerticalSourcesDock::OnTransformEdited);
	}
	rotSpin->setRange(-360.0, 360.0);
	rotSpin->setDecimals(1);
	rotSpin->setSingleStep(1.0);
	connect(rotSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
		&VerticalSourcesDock::OnTransformEdited);

	form->addWidget(new QLabel(Translate("PositionX"), this), 0, 0);
	form->addWidget(posXSpin, 0, 1);
	form->addWidget(new QLabel(Translate("PositionY"), this), 0, 2);
	form->addWidget(posYSpin, 0, 3);
	form->addWidget(new QLabel(Translate("SizeW"), this), 1, 0);
	form->addWidget(sizeWSpin, 1, 1);
	form->addWidget(new QLabel(Translate("SizeH"), this), 1, 2);
	form->addWidget(sizeHSpin, 1, 3);
	form->addWidget(new QLabel(Translate("Rotation"), this), 2, 0);
	form->addWidget(rotSpin, 2, 1);
	root->addLayout(form);

	auto *transformButtons = new QHBoxLayout();
	auto *fitBtn = new QPushButton(Translate("FitToScreen"), this);
	auto *stretchBtn = new QPushButton(Translate("StretchToScreen"), this);
	auto *centerBtn = new QPushButton(Translate("CenterToScreen"), this);
	auto *resetBtn = new QPushButton(Translate("ResetTransform"), this);
	connect(fitBtn, &QPushButton::clicked, this, &VerticalSourcesDock::OnFitToScreen);
	connect(stretchBtn, &QPushButton::clicked, this, &VerticalSourcesDock::OnStretchToScreen);
	connect(centerBtn, &QPushButton::clicked, this, &VerticalSourcesDock::OnCenterToScreen);
	connect(resetBtn, &QPushButton::clicked, this, &VerticalSourcesDock::OnResetTransform);
	transformButtons->addWidget(fitBtn);
	transformButtons->addWidget(stretchBtn);
	transformButtons->addWidget(centerBtn);
	transformButtons->addWidget(resetBtn);
	root->addLayout(transformButtons);
}

void VerticalSourcesDock::RefreshSources()
{
	if (!workspace || !sourcesList)
		return;

	refreshing = true;
	workspace->PopulateSourcesList(sourcesList);
	refreshing = false;
}

void VerticalSourcesDock::RefreshTransform()
{
	if (!workspace)
		return;

	updatingTransform = true;
	workspace->PopulateTransformControls(posXSpin, posYSpin, sizeWSpin, sizeHSpin, rotSpin);
	updatingTransform = false;
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

void VerticalSourcesDock::OnFitToScreen()
{
	if (workspace)
		workspace->RequestFitToScreen();
}

void VerticalSourcesDock::OnStretchToScreen()
{
	if (workspace)
		workspace->RequestStretchToScreen();
}

void VerticalSourcesDock::OnCenterToScreen()
{
	if (workspace)
		workspace->RequestCenterToScreen();
}

void VerticalSourcesDock::OnResetTransform()
{
	if (workspace)
		workspace->RequestResetTransform();
}

void VerticalSourcesDock::OnTransformEdited()
{
	if (updatingTransform || !workspace)
		return;

	workspace->RequestTransformEdited(posXSpin->value(), posYSpin->value(), sizeWSpin->value(),
					  sizeHSpin->value(), rotSpin->value());
}
