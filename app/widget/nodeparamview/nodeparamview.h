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

#ifndef NODEPARAMVIEW_H
#define NODEPARAMVIEW_H

#include <QMainWindow>
#include <QVBoxLayout>
#include <QWidget>

#include "node/node.h"
#include "nodeparamviewitem.h"
#include "widget/keyframeview/keyframeview.h"
#include "widget/timebased/timebased.h"

OLIVE_NAMESPACE_ENTER

class NodeParamViewParamContainer : public QWidget
{
  Q_OBJECT
public:
  NodeParamViewParamContainer(QWidget* parent = nullptr) :
    QWidget(parent)
  {
  }

protected:
  virtual void resizeEvent(QResizeEvent *event) override
  {
    QWidget::resizeEvent(event);

    emit Resized(event->size().height());
  }

signals:
  void Resized(int new_height);

};

class NodeParamView : public TimeBasedWidget
{
  Q_OBJECT
public:
  NodeParamView(QWidget* parent = nullptr);

  void SelectNodes(const QList<Node*>& nodes);
  void DeselectNodes(const QList<Node*>& nodes);

  const QMap<Node*, NodeParamViewItem*>& GetItemMap() const
  {
    return items_;
  }

  Node* GetTimeTarget() const;

  void DeleteSelected();

signals:
  void InputDoubleClicked(NodeInput* input);

  void RequestSelectNode(const QList<Node*>& target);

protected:
  virtual void resizeEvent(QResizeEvent *event) override;

  virtual void ScaleChangedEvent(const double &) override;
  virtual void TimebaseChangedEvent(const rational&) override;
  virtual void TimeChangedEvent(const int64_t &) override;

  virtual void ConnectedNodeChanged(ViewerOutput* n) override;

private:
  void UpdateItemTime(const int64_t &timestamp);

  void QueueKeyframePositionUpdate();

  KeyframeView* keyframe_view_;

  QMap<Node*, NodeParamViewItem*> items_;

  QScrollBar* vertical_scrollbar_;

  int last_scroll_val_;

  NodeParamViewParamContainer* param_widget_container_;

  // This may look weird, but QMainWindow is just a QWidget with a fancy layout that allows
  // docking windows
  QMainWindow* param_widget_area_;

private slots:
  void ItemRequestedTimeChanged(const rational& time);

  void UpdateGlobalScrollBar();

  void PlaceKeyframesOnView();

};

OLIVE_NAMESPACE_EXIT

#endif // NODEPARAMVIEW_H
