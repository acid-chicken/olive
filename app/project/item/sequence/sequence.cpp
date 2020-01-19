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

#include "sequence.h"

#include <QCoreApplication>

#include "config/config.h"
#include "common/channellayout.h"
#include "common/timecodefunctions.h"
#include "common/xmlreadloop.h"
#include "node/factory.h"
#include "panel/panelmanager.h"
#include "panel/node/node.h"
#include "panel/curve/curve.h"
#include "panel/param/param.h"
#include "panel/timeline/timeline.h"
#include "panel/viewer/viewer.h"
#include "ui/icons/icons.h"

Sequence::Sequence()
{
    viewer_output_ = new ViewerOutput();
    viewer_output_->SetCanBeDeleted(false);
    AddNode(viewer_output_);
}

void Sequence::Load(QXmlStreamReader *reader, QHash<quintptr, StreamPtr> &, QList<NodeInput::FootageConnection>& footage_connections)
{
    XMLAttributeLoop(reader, attr) {
        if (attr.name() == "name") {
            set_name(attr.value().toString());

            // Currently the only thing we care about
        }
    }

    QHash<quintptr, NodeOutput*> output_ptrs;
    QList<NodeParam::SerializedConnection> desired_connections;

    XMLReadLoop(reader, "sequence") {
        if (reader->isStartElement()) {
            if (reader->name() == "video") {
                int video_width, video_height;
                rational video_timebase;

                XMLReadLoop(reader, "video") {
                    if (reader->isStartElement()) {
                        if (reader->name() == "width") {
                            reader->readNext();
                            video_width = reader->text().toInt();
                        } else if (reader->name() == "height") {
                            reader->readNext();
                            video_height = reader->text().toInt();
                        } else if (reader->name() == "timebase") {
                            reader->readNext();
                            video_timebase = rational::fromString(reader->text().toString());
                        }
                    }
                }

                set_video_params(VideoParams(video_width, video_height, video_timebase));
            } else if (reader->name() == "audio") {
                int rate;
                uint64_t layout;

                XMLReadLoop(reader, "audio") {
                    if (reader->isStartElement()) {
                        if (reader->name() == "rate") {
                            reader->readNext();
                            rate = reader->text().toInt();
                        } else if (reader->name() == "layout") {
                            reader->readNext();
                            layout = reader->text().toULongLong();
                        }
                    }
                }

                set_audio_params(AudioParams(rate, layout));
            } else if (reader->name() == "node" || reader->name() == "viewer") {
                Node* node;

                if (reader->name() == "node") {
                    QString node_id;

                    XMLAttributeLoop(reader, attr) {
                        if (attr.name() == "id") {
                            node_id = attr.value().toString();

                            // Currently the only thing we need
                            break;
                        }
                    }

                    if (node_id.isEmpty()) {
                        qDebug() << "Found node with no ID";
                        continue;
                    }

                    node = NodeFactory::CreateFromID(node_id);

                    if (!node) {
                        qDebug() << "Failed to load" << node_id << "- no node with that ID is installed";
                        continue;
                    }
                } else {
                    node = viewer_output_;
                }

                if (node) {
                    node->Load(reader, output_ptrs, desired_connections, footage_connections, reader->name().toString());

                    AddNode(node);
                }
            }
        }
    }

    // Make connections
    foreach (const NodeParam::SerializedConnection& con, desired_connections) {
        NodeParam::ConnectEdge(output_ptrs.value(con.output),
                               con.input);
    }

    // Ensure this and all children are in the main thread
    // (FIXME: Weird place for this? This should probably be in ProjectLoadManager somehow)
    if (thread() != qApp->thread()) {
        moveToThread(qApp->thread());
    }
}

void Sequence::Save(QXmlStreamWriter *writer) const
{
    writer->writeStartElement("sequence");

    writer->writeAttribute("name", name());

    writer->writeStartElement("video");

    writer->writeTextElement("width", QString::number(video_params().width()));
    writer->writeTextElement("height", QString::number(video_params().height()));
    writer->writeTextElement("timebase", video_params().time_base().toString());

    writer->writeEndElement(); // video

    writer->writeStartElement("audio");

    writer->writeTextElement("rate", QString::number(audio_params().sample_rate()));
    writer->writeTextElement("layout", QString::number(audio_params().channel_layout()));

    writer->writeEndElement(); // audio

    foreach (Node* node, nodes()) {
        if (node != viewer_output_) {
            node->Save(writer);
        }
    }

    viewer_output_->Save(writer, "viewer");

    writer->writeEndElement(); // sequence
}

void Sequence::Open(Sequence* sequence)
{
    // FIXME: This is fairly "hardcoded" behavior and doesn't support infinite panels

    ViewerPanel* viewer_panel = PanelManager::instance()->MostRecentlyFocused<ViewerPanel>();
    TimelinePanel* timeline_panel = PanelManager::instance()->MostRecentlyFocused<TimelinePanel>();
    NodePanel* node_panel = PanelManager::instance()->MostRecentlyFocused<NodePanel>();

    viewer_panel->ConnectViewerNode(sequence->viewer_output_);
    timeline_panel->ConnectTimelineNode(sequence->viewer_output_);
    node_panel->SetGraph(sequence);
}

void Sequence::add_default_nodes()
{
    // Create tracks and connect them to the viewer
    Node* video_track_output = viewer_output_->track_list(Timeline::kTrackTypeVideo)->AddTrack();
    Node* audio_track_output = viewer_output_->track_list(Timeline::kTrackTypeAudio)->AddTrack();
    NodeParam::ConnectEdge(video_track_output->output(), viewer_output_->texture_input());
    NodeParam::ConnectEdge(audio_track_output->output(), viewer_output_->samples_input());
}

Item::Type Sequence::type() const
{
    return kSequence;
}

QIcon Sequence::icon()
{
    return icon::Sequence;
}

QString Sequence::duration()
{
    rational timeline_length = viewer_output_->Length();

    int64_t timestamp = Timecode::time_to_timestamp(timeline_length, video_params().time_base());

    return Timecode::timestamp_to_timecode(timestamp, video_params().time_base(), Timecode::CurrentDisplay());
}

QString Sequence::rate()
{
    return QCoreApplication::translate("Sequence", "%1 FPS").arg(video_params().time_base().flipped().toDouble());
}

const VideoParams &Sequence::video_params() const
{
    return viewer_output_->video_params();
}

void Sequence::set_video_params(const VideoParams &vparam)
{
    viewer_output_->set_video_params(vparam);
}

const AudioParams &Sequence::audio_params() const
{
    return viewer_output_->audio_params();
}

void Sequence::set_audio_params(const AudioParams &params)
{
    viewer_output_->set_audio_params(params);
}

void Sequence::set_default_parameters()
{
    set_video_params(VideoParams(Config::Current()["DefaultSequenceWidth"].toInt(),
                                 Config::Current()["DefaultSequenceHeight"].toInt(),
                                 Config::Current()["DefaultSequenceFrameRate"].value<rational>()));
    set_audio_params(AudioParams(Config::Current()["DefaultSequenceAudioFrequency"].toInt(),
                                 Config::Current()["DefaultSequenceAudioLayout"].toULongLong()));
}

void Sequence::NameChangedEvent(const QString &name)
{
    viewer_output_->set_media_name(name);
}
