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

#ifndef EXTERNALNODE_H
#define EXTERNALNODE_H

#include <QXmlStreamReader>

#include "metareader.h"
#include "node.h"

OLIVE_NAMESPACE_ENTER

/**
 * @brief A node generated from an external XML metadata file
 */
class ExternalNode : public Node {
 public:
  ExternalNode(const QString& xml_meta_filename);

  virtual Node* copy() const override;

  virtual QString Name() const override;
  virtual QString ShortName() const override;
  virtual QString id() const override;
  virtual QString Category() const override;
  virtual QString Description() const override;

  virtual void Retranslate() override;

  virtual Capabilities GetCapabilities(const NodeValueDatabase&) const override;
  virtual QString ShaderVertexCode(const NodeValueDatabase&) const override;
  virtual QString ShaderFragmentCode(const NodeValueDatabase&) const override;
  virtual int ShaderIterations() const override;
  virtual NodeInput* ShaderIterativeInput() const override;

 private:
  NodeMetaReader meta_;
};

OLIVE_NAMESPACE_EXIT

#endif  // EXTERNALNODE_H
