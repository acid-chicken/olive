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

#ifndef TRANSITIONBLOCK_H
#define TRANSITIONBLOCK_H

#include "node/block/block.h"

OLIVE_NAMESPACE_ENTER

class TransitionBlock : public Block {
 public:
  TransitionBlock();

  virtual Type type() const override;

  NodeInput* out_block_input() const;
  NodeInput* in_block_input() const;

  virtual void Retranslate() override;

  rational in_offset() const;
  rational out_offset() const;

  Block* connected_out_block() const;
  Block* connected_in_block() const;

  double GetTotalProgress(const rational& time) const;
  double GetOutProgress(const rational& time) const;
  double GetInProgress(const rational& time) const;

  virtual void Hash(QCryptographicHash& hash, const rational& time) const override;

 private:
  double GetInternalTransitionTime(const rational& time) const;

  NodeInput* out_block_input_;

  NodeInput* in_block_input_;

  Block* connected_out_block_;

  Block* connected_in_block_;

 private slots:
  void BlockConnected(NodeEdgePtr edge);

  void BlockDisconnected(NodeEdgePtr edge);
};

OLIVE_NAMESPACE_EXIT

#endif  // TRANSITIONBLOCK_H
