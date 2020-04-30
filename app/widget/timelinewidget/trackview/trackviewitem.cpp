/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2019 Olive Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "trackviewitem.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

OLIVE_NAMESPACE_ENTER

TrackViewItem::TrackViewItem(TrackOutput* track, QWidget *parent) :
    QWidget(parent),
    track_(track)
{
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setSpacing(0);
    layout->setMargin(0);

    stack_ = new QStackedWidget();
    layout->addWidget(stack_);

    label_ = new ClickableLabel(track_->GetTrackName());
    connect(label_, SIGNAL(MouseDoubleClicked()), this, SLOT(LabelClicked()));
    stack_->addWidget(label_);

    line_edit_ = new FocusableLineEdit();
    connect(line_edit_, SIGNAL(Confirmed()), this, SLOT(LineEditConfirmed()));
    connect(line_edit_, SIGNAL(Cancelled()), this, SLOT(LineEditCancelled()));
    stack_->addWidget(line_edit_);

    mute_button_ = CreateMSLButton(tr("M"), Qt::red);
    connect(mute_button_, SIGNAL(toggled(bool)), track_, SLOT(SetMuted(bool)));
    layout->addWidget(mute_button_);

    /*solo_button_ = CreateMSLButton(tr("S"), Qt::yellow);
    layout->addWidget(solo_button_);*/

    lock_button_ = CreateMSLButton(tr("L"), Qt::gray);
    connect(lock_button_, SIGNAL(toggled(bool)), track_, SLOT(SetLocked(bool)));
    layout->addWidget(lock_button_);

    setMinimumHeight(mute_button_->height());

    connect(track, &TrackOutput::MutedChanged, mute_button_, &QPushButton::setChecked);
}

QPushButton *TrackViewItem::CreateMSLButton(const QString& text, const QColor& checked_color) const
{
    QPushButton* button = new QPushButton(text);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    button->setCheckable(true);
    button->setStyleSheet(QStringLiteral("QPushButton::checked { background: %1; }").arg(checked_color.name()));

    int size = button->sizeHint().height();
    size = qRound(size * 0.75);
    button->setFixedSize(size, size);

    return button;
}

void TrackViewItem::LabelClicked()
{
    stack_->setCurrentWidget(line_edit_);
    line_edit_->setFocus();
    line_edit_->selectAll();
}

void TrackViewItem::LineEditConfirmed()
{
    line_edit_->blockSignals(true);

    QString line_edit_str = line_edit_->text();
    if (!line_edit_str.isEmpty()) {
        label_->setText(line_edit_str);
        track_->SetTrackName(line_edit_str);
    }

    stack_->setCurrentWidget(label_);

    line_edit_->blockSignals(false);
}

void TrackViewItem::LineEditCancelled()
{
    line_edit_->blockSignals(true);

    stack_->setCurrentWidget(label_);

    line_edit_->blockSignals(false);
}

OLIVE_NAMESPACE_EXIT
