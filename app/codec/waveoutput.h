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

#ifndef WAVEAUDIO_H
#define WAVEAUDIO_H

#include <QByteArray>
#include <QFile>

#include "audio/sampleformat.h"
#include "render/audioparams.h"

OLIVE_NAMESPACE_ENTER

class WaveOutput {
 public:
  WaveOutput(const QString& f, const AudioRenderingParams& params);

  ~WaveOutput();

  DISABLE_COPY_MOVE(WaveOutput)

  bool open();

  void write(const QByteArray& bytes);
  void write(const char* bytes, int length);

  void close();

  const int& data_length() const;

  const AudioRenderingParams& params() const;

 private:
  template <typename T>
  void write_int(QFile* file, T integer);

  void switch_endianness(QByteArray& array);

  QFile file_;

  AudioRenderingParams params_;

  int data_length_;
};

OLIVE_NAMESPACE_EXIT

#endif  // WAVEAUDIO_H
