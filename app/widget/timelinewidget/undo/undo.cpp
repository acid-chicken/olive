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

#include "undo.h"

#include "node/block/transition/transition.h"
#include "node/graph.h"

Node *TakeNodeFromParentGraph(Node *n, QObject *new_parent = nullptr) {
  static_cast<NodeGraph *>(n->parent())->TakeNode(n, new_parent);

  return n;
}

BlockResizeCommand::BlockResizeCommand(Block *block, rational new_length, QUndoCommand *parent)
    : QUndoCommand(parent), block_(block), old_length_(block->length()), new_length_(new_length) {}

void BlockResizeCommand::redo() { block_->set_length_and_media_out(new_length_); }

void BlockResizeCommand::undo() { block_->set_length_and_media_out(old_length_); }

BlockResizeWithMediaInCommand::BlockResizeWithMediaInCommand(Block *block, rational new_length, QUndoCommand *parent)
    : QUndoCommand(parent), block_(block), old_length_(block->length()), new_length_(new_length) {}

void BlockResizeWithMediaInCommand::redo() { block_->set_length_and_media_in(new_length_); }

void BlockResizeWithMediaInCommand::undo() { block_->set_length_and_media_in(old_length_); }

BlockSetMediaInCommand::BlockSetMediaInCommand(Block *block, rational new_media_in, QUndoCommand *parent)
    : QUndoCommand(parent), block_(block), old_media_in_(block->media_in()), new_media_in_(new_media_in) {}

void BlockSetMediaInCommand::redo() { block_->set_media_in(new_media_in_); }

void BlockSetMediaInCommand::undo() { block_->set_media_in(old_media_in_); }

TrackRippleRemoveBlockCommand::TrackRippleRemoveBlockCommand(TrackOutput *track, Block *block, QUndoCommand *parent)
    : QUndoCommand(parent), track_(track), block_(block) {}

void TrackRippleRemoveBlockCommand::redo() {
  before_ = block_->previous();
  track_->RippleRemoveBlock(block_);
}

void TrackRippleRemoveBlockCommand::undo() {
  if (before_) {
    track_->InsertBlockAfter(block_, before_);
  } else {
    track_->PrependBlock(block_);
  }
}

TrackInsertBlockAfterCommand::TrackInsertBlockAfterCommand(TrackOutput *track, Block *block, Block *before,
                                                           QUndoCommand *parent)
    : QUndoCommand(parent), track_(track), block_(block), before_(before) {}

void TrackInsertBlockAfterCommand::redo() { track_->InsertBlockAfter(block_, before_); }

void TrackInsertBlockAfterCommand::undo() { track_->RippleRemoveBlock(block_); }

TrackRippleRemoveAreaCommand::TrackRippleRemoveAreaCommand(TrackOutput *track, rational in, rational out,
                                                           QUndoCommand *parent)
    : QUndoCommand(parent),
      track_(track),
      in_(in),
      out_(out),
      splice_(nullptr),
      trim_out_(nullptr),
      trim_in_(nullptr),
      insert_(nullptr) {}

void TrackRippleRemoveAreaCommand::SetInsert(Block *insert) { insert_ = insert; }

void TrackRippleRemoveAreaCommand::redo() {
  // Iterate through blocks determining which need trimming/removing/splitting
  foreach (Block *block, track_->Blocks()) {
    if (block->in() < in_ && block->out() > out_) {
      // The area entirely within this Block
      splice_ = block;

      // We don't need to do anything else here
      break;
    } else if (block->in() >= in_ && block->out() <= out_) {
      // This Block's is entirely within the area
      removed_blocks_.append(block);
    } else if (block->in() < in_ && block->out() >= in_) {
      // This Block's out point exceeds `in`
      trim_out_ = block;
    } else if (block->in() <= out_ && block->out() > out_) {
      // This Block's in point exceeds `out`
      trim_in_ = block;
    }
  }

  track_->BlockInvalidateCache();

  // If we picked up a block to splice
  if (splice_ != nullptr) {
    // Split the block here
    Block *copy = static_cast<Block *>(splice_->copy());

    splice_original_length_ = splice_->length();
    splice_->set_length_and_media_out(out_ - splice_->in());

    static_cast<NodeGraph *>(track_->parent())->AddNode(copy);
    Node::CopyInputs(splice_, copy);
    copy->set_length_and_media_in(splice_original_length_ - (out_ - splice_->in()));

    track_->InsertBlockAfter(copy, splice_);

    // Perform all further actions as if we were just trimming these clips
    trim_out_ = splice_;
    trim_in_ = copy;
  }

  // If we picked up a block to trim the in point of
  if (trim_in_ != nullptr) {
    trim_in_old_length_ = trim_in_->length();
    trim_in_new_length_ = trim_in_->out() - out_;
  }

  // If we picked up a block to trim the out point of
  if (trim_out_ != nullptr) {
    trim_out_old_length_ = trim_out_->length();
    trim_out_new_length_ = in_ - trim_out_->in();
  }

  // If we picked up a block to trim the in point of
  if (trim_in_old_length_ != trim_in_new_length_) {
    trim_in_->set_length_and_media_in(trim_in_new_length_);
  }

  // Remove all blocks that are flagged for removal
  foreach (Block *remove_block, removed_blocks_) {
    track_->RippleRemoveBlock(remove_block);

    // FIXME: Delete blocks from graph and restore them in undo
  }

  // If we picked up a block to trim the out point of
  if (trim_out_old_length_ != trim_out_new_length_) {
    trim_out_->set_length_and_media_out(trim_out_new_length_);
  }

  // If we were given a block to insert, insert it here
  if (insert_) {
    if (!trim_out_) {
      // This is the start of the Sequence
      track_->PrependBlock(insert_);
    } else if (!trim_in_) {
      // This is the end of the Sequence
      track_->AppendBlock(insert_);
    } else {
      // This is somewhere in the middle of the Sequence
      track_->InsertBlockAfter(insert_, trim_out_);
    }
  }

  track_->UnblockInvalidateCache();

  track_->InvalidateCache(in_, out_);
}

void TrackRippleRemoveAreaCommand::undo() {
  track_->BlockInvalidateCache();

  // If we were given a block to insert, insert it here
  if (insert_ != nullptr) {
    track_->RippleRemoveBlock(insert_);
  }

  // If we picked up a block to trim the out point of
  if (trim_out_old_length_ != trim_out_new_length_) {
    trim_out_->set_length_and_media_out(trim_out_old_length_);
  }

  // Remove all blocks that are flagged for removal
  foreach (Block *remove_block, removed_blocks_) {
    if (trim_in_ == nullptr) {
      track_->AppendBlock(remove_block);
    } else {
      track_->InsertBlockBefore(remove_block, trim_in_);
    }
  }
  removed_blocks_.clear();

  // If we picked up a block to trim the in point of
  if (trim_in_old_length_ != trim_in_new_length_) {
    trim_in_->set_length_and_media_in(trim_in_old_length_);
  }

  // If we're splicing, trim_in_ is a copy
  if (splice_ != nullptr) {
    track_->RippleRemoveBlock(trim_in_);

    // Remove node
    TakeNodeFromParentGraph(trim_in_, &memory_manager_);

    splice_->set_length_and_media_out(splice_original_length_);
  }

  track_->UnblockInvalidateCache();

  track_->InvalidateCache(in_, out_);
}

TrackPlaceBlockCommand::TrackPlaceBlockCommand(TrackList *timeline, int track, Block *block, rational in,
                                               QUndoCommand *parent)
    : TrackRippleRemoveAreaCommand(nullptr, in, 0, parent),  // Out gets set correctly in redo()
      timeline_(timeline),
      track_index_(track),
      gap_(nullptr) {
  insert_ = block;
}

void TrackPlaceBlockCommand::redo() {
  added_track_count_ = 0;

  // Get track (or make it if necessary)
  while (track_index_ >= timeline_->Tracks().size()) {
    timeline_->AddTrack();

    added_track_count_++;
  }

  track_ = timeline_->TrackAt(track_index_);

  append_ = (in_ >= track_->track_length());

  // Check if the placement location is past the end of the timeline
  if (append_) {
    if (in_ > track_->track_length()) {
      // If so, insert a gap here
      gap_ = new GapBlock();
      gap_->set_length_and_media_out(in_ - track_->track_length());
      static_cast<NodeGraph *>(track_->parent())->AddNode(gap_);
      track_->AppendBlock(gap_);
    }

    track_->AppendBlock(insert_);
  } else {
    out_ = in_ + insert_->length();

    // Place the Block at this point
    TrackRippleRemoveAreaCommand::redo();
  }
}

void TrackPlaceBlockCommand::undo() {
  if (append_) {
    track_->RippleRemoveBlock(insert_);

    if (gap_ != nullptr) {
      track_->RippleRemoveBlock(gap_);
      delete TakeNodeFromParentGraph(gap_);
    }
  } else {
    TrackRippleRemoveAreaCommand::undo();
  }

  for (; added_track_count_ > 0; added_track_count_--) {
    timeline_->RemoveTrack();
  }
}

BlockSplitCommand::BlockSplitCommand(TrackOutput *track, Block *block, rational point, QUndoCommand *parent)
    : QUndoCommand(parent),
      track_(track),
      block_(block),
      new_length_(point - block->in()),
      old_length_(block->length()),
      point_(point) {
  Q_ASSERT(point > block_->in() && point < block_->out() && block_->type() == Block::kClip);

  // Ensures that this block is deleted if this action is undone
  new_block_ = static_cast<Block *>(block_->copy());
  new_block_->setParent(&memory_manager_);

  // Determine if the block outputs to an "out" transition
  foreach (NodeEdgePtr edge, block_->output()->edges()) {
    if (edge->input()->parentNode()->IsBlock() &&
        static_cast<Block *>(edge->input()->parentNode())->type() == Block::kTransition &&
        edge->input() == static_cast<TransitionBlock *>(edge->input()->parentNode())->out_block_input()) {
      transitions_to_move_.append(edge->input());
    }
  }
}

void BlockSplitCommand::redo() {
  track_->BlockInvalidateCache();

  static_cast<NodeGraph *>(block_->parent())->AddNode(new_block_);
  Node::CopyInputs(block_, new_block_);

  rational new_part_length = block_->length() - (point_ - block_->in());

  block_->set_length_and_media_out(new_length_);

  new_block_->set_length_and_media_in(new_part_length);

  track_->InsertBlockAfter(new_block_, block_);

  foreach (NodeInput *transition, transitions_to_move_) {
    NodeParam::DisconnectEdge(block_->output(), transition);
    NodeParam::ConnectEdge(new_block_->output(), transition);
  }

  track_->UnblockInvalidateCache();
}

void BlockSplitCommand::undo() {
  track_->BlockInvalidateCache();

  block_->set_length_and_media_out(old_length_);
  track_->RippleRemoveBlock(new_block_);

  TakeNodeFromParentGraph(new_block_, &memory_manager_);

  foreach (NodeInput *transition, transitions_to_move_) {
    NodeParam::DisconnectEdge(new_block_->output(), transition);
    NodeParam::ConnectEdge(block_->output(), transition);
  }

  track_->UnblockInvalidateCache();
}

Block *BlockSplitCommand::new_block() { return new_block_; }

TrackSplitAtTimeCommand::TrackSplitAtTimeCommand(TrackOutput *track, rational point, QUndoCommand *parent)
    : QUndoCommand(parent) {
  // Find Block that contains this time
  foreach (Block *b, track->Blocks()) {
    if (b->out() == point) {
      // This time is between blocks, no split needs to occur
      return;
    } else if (b->in() < point && b->out() > point) {
      // We found the Block, split it
      new BlockSplitCommand(track, b, point, this);
      return;
    }
  }
}

TrackReplaceBlockCommand::TrackReplaceBlockCommand(TrackOutput *track, Block *old, Block *replace, QUndoCommand *parent)
    : QUndoCommand(parent), track_(track), old_(old), replace_(replace) {}

void TrackReplaceBlockCommand::redo() { track_->ReplaceBlock(old_, replace_); }

void TrackReplaceBlockCommand::undo() { track_->ReplaceBlock(replace_, old_); }

TrackPrependBlockCommand::TrackPrependBlockCommand(TrackOutput *track, Block *block, QUndoCommand *parent)
    : QUndoCommand(parent), track_(track), block_(block) {}

void TrackPrependBlockCommand::redo() { track_->PrependBlock(block_); }

void TrackPrependBlockCommand::undo() { track_->RippleRemoveBlock(block_); }

BlockSplitPreservingLinksCommand::BlockSplitPreservingLinksCommand(const QVector<Block *> &blocks,
                                                                   const QList<rational> &times, QUndoCommand *parent)
    : QUndoCommand(parent), blocks_(blocks), times_(times) {
  QVector<QVector<Block *> > split_blocks(times.size());

  for (int i = 0; i < times.size(); i++) {
    const rational &time = times.at(i);

    QVector<Block *> splits(blocks.size());

    for (int j = 0; j < blocks.size(); j++) {
      Block *b = blocks.at(j);

      if (b->in() < time && b->out() > time) {
        TrackOutput *track = TrackOutput::TrackFromBlock(b);

        Q_ASSERT(track);

        BlockSplitCommand *split_command = new BlockSplitCommand(track, b, time, this);
        splits.replace(j, split_command->new_block());
      } else {
        splits.replace(j, nullptr);
      }
    }

    split_blocks.replace(i, splits);
  }

  // Now that we've determined all the splits, we can relink everything
  for (int i = 0; i < blocks_.size(); i++) {
    Block *a = blocks.at(i);

    for (int j = 0; j < blocks.size(); j++) {
      if (i == j) {
        continue;
      }

      Block *b = blocks.at(j);

      if (Block::AreLinked(a, b)) {
        // These blocks are linked, ensure all the splits are linked too

        foreach (const QVector<Block *> &split_list, split_blocks) { Block::Link(split_list.at(i), split_list.at(j)); }
      }
    }
  }
}

TrackCleanGapsCommand::TrackCleanGapsCommand(TrackList *track_list, int index, QUndoCommand *parent)
    : QUndoCommand(parent), track_list_(track_list), track_index_(index) {}

void TrackCleanGapsCommand::redo() {
  GapBlock *on_gap = nullptr;
  QList<GapBlock *> consecutive_gaps;

  TrackOutput *track = track_list_->TrackAt(track_index_);

  foreach (Block *b, track->Blocks()) {
    if (b) {
      if (b->type() == Block::kGap) {
        if (on_gap) {
          consecutive_gaps.append(static_cast<GapBlock *>(b));
        } else {
          on_gap = static_cast<GapBlock *>(b);
        }
      } else if (on_gap) {
        merged_gaps_.append({on_gap, on_gap->length(), consecutive_gaps});

        // Remove each gap and add to the length of the merged
        // We can block the IC signal because merging gaps won't actually change anything
        track->BlockInvalidateCache();
        rational new_gap_length = on_gap->length();
        foreach (GapBlock *gap, consecutive_gaps) {
          track->RippleRemoveBlock(gap);
          static_cast<NodeGraph *>(gap->parent())->TakeNode(gap, &memory_manager_);

          new_gap_length += gap->length();
        }
        on_gap->set_length_and_media_out(new_gap_length);
        track->UnblockInvalidateCache();

        // Reset state
        on_gap = nullptr;
        consecutive_gaps.clear();
      }
    }
  }

  if (on_gap) {
    // If we're here, we found at least one or several
    removed_end_gaps_.append(on_gap);
    removed_end_gaps_.append(consecutive_gaps);

    foreach (GapBlock *gap, removed_end_gaps_) {
      track->RippleRemoveBlock(gap);
      static_cast<NodeGraph *>(gap->parent())->TakeNode(gap, &memory_manager_);
    }
  }
}

void TrackCleanGapsCommand::undo() {
  TrackOutput *track = track_list_->TrackAt(track_index_);

  // Restored removed end gaps
  foreach (GapBlock *gap, removed_end_gaps_) {
    static_cast<NodeGraph *>(gap->parent())->AddNode(gap);
    track->AppendBlock(gap);
  }
  removed_end_gaps_.clear();

  track->BlockInvalidateCache();

  for (int i = merged_gaps_.size() - 1; i >= 0; i--) {
    const MergedGap &merge_info = merged_gaps_.at(i);

    merge_info.merged->set_length_and_media_out(merge_info.original_length);

    GapBlock *last_gap_added = merge_info.merged;

    foreach (GapBlock *gap, merge_info.removed) {
      static_cast<NodeGraph *>(gap->parent())->AddNode(gap);
      track->InsertBlockAfter(gap, last_gap_added);
      last_gap_added = gap;
    }
  }

  track->UnblockInvalidateCache();

  merged_gaps_.clear();
}

BlockSetSpeedCommand::BlockSetSpeedCommand(Block *block, const rational &new_speed, QUndoCommand *parent)
    : QUndoCommand(parent), block_(block), old_speed_(block->speed()), new_speed_(new_speed) {}

void BlockSetSpeedCommand::redo() { block_->set_speed(new_speed_); }

void BlockSetSpeedCommand::undo() { block_->set_speed(old_speed_); }
