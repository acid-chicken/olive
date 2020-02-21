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

#include "core.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QStyleFactory>

#include "audio/audiomanager.h"
#include "config/config.h"
#include "dialog/about/about.h"
#include "dialog/export/export.h"
#include "dialog/preferences/preferences.h"
#include "dialog/projectproperties/projectproperties.h"
#include "dialog/sequence/sequence.h"
#include "dialog/task/task.h"
#include "node/factory.h"
#include "panel/panelmanager.h"
#include "panel/project/project.h"
#include "panel/viewer/viewer.h"
#include "project/item/footage/footage.h"
#include "project/item/sequence/sequence.h"
#include "project/projectimportmanager.h"
#include "project/projectloadmanager.h"
#include "project/projectsavemanager.h"
#include "render/backend/indexmanager.h"
#include "render/backend/opengl/opengltexturecache.h"
#include "render/colormanager.h"
#include "render/diskmanager.h"
#include "render/memorymanager.h"
#include "render/pixelservice.h"
#include "task/taskmanager.h"
#include "ui/style/style.h"
#include "undo/undostack.h"
#include "widget/menu/menushared.h"
#include "widget/taskview/taskviewitem.h"
#include "window/mainwindow/mainwindow.h"

Core Core::instance_;

Core::Core() : main_window_(nullptr), tool_(Tool::kPointer), snapping_(true), queue_autorecovery_(false) {}

Core* Core::instance() { return &instance_; }

void Core::Start() {
  //
  // Parse command line arguments
  //

  QCoreApplication* app = QCoreApplication::instance();

  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();

  // Project from command line option
  // FIXME: What's the correct way to make a visually "optional" positional argument, or is manually adding square
  // brackets like this correct?
  parser.addPositionalArgument("[project]", tr("Project to open on startup"));

  // Create fullscreen option
  QCommandLineOption fullscreen_option({"f", "fullscreen"}, tr("Start in full screen mode"));
  parser.addOption(fullscreen_option);

  // Parse options
  parser.process(*app);

  QStringList args = parser.positionalArguments();

  // Detect project to load on startup
  if (!args.isEmpty()) {
    startup_project_ = args.first();
  }

  // Declare custom types for Qt signal/slot syste
  DeclareTypesForQt();

  // Set up node factory/library
  NodeFactory::Initialize();

  // Set up the index manager for renderers
  IndexManager::CreateInstance();

  // Set up memory manager
  MemoryManager::CreateInstance();

  // Load application config
  Config::Load();

  //
  // Start GUI (FIXME CLI mode)
  //

  StartGUI(parser.isSet(fullscreen_option));

  // Load startup project
  if (startup_project_.isEmpty()) {
    // If no load project is set, create a new one on open
    AddOpenProject(std::make_shared<Project>());
  } else {
  }
}

void Core::Stop() {
  // Save Config
  // Config::Save();

  MenuShared::DestroyInstance();

  TaskManager::DestroyInstance();

  PanelManager::DestroyInstance();

  AudioManager::DestroyInstance();

  DiskManager::DestroyInstance();

  PixelService::DestroyInstance();

  NodeFactory::Destroy();

  IndexManager::DestroyInstance();

  MemoryManager::DestroyInstance();

  delete main_window_;
}

MainWindow* Core::main_window() { return main_window_; }

UndoStack* Core::undo_stack() { return &undo_stack_; }

void Core::ImportFiles(const QStringList& urls, ProjectViewModel* model, Folder* parent) {
  if (urls.isEmpty()) {
    QMessageBox::critical(main_window_, tr("Import error"), tr("Nothing to import"));
    return;
  }

  ProjectImportManager* pim = new ProjectImportManager(model, parent, urls);

  if (!pim->GetFileCount()) {
    // No files to import
    delete pim;
    return;
  }

  connect(pim, &ProjectImportManager::ImportComplete, this, &Core::ImportTaskComplete, Qt::BlockingQueuedConnection);

  TaskDialog* task_dialog = new TaskDialog(pim, tr("Importing..."), main_window());
  task_dialog->open();
}

const Tool::Item& Core::tool() { return tool_; }

const bool& Core::snapping() { return snapping_; }

void Core::SetTool(const Tool::Item& tool) {
  tool_ = tool;

  emit ToolChanged(tool_);
}

void Core::SetSnapping(const bool& b) {
  snapping_ = b;

  emit SnappingChanged(snapping_);
}

void Core::DialogAboutShow() {
  AboutDialog a(main_window_);
  a.exec();
}

void Core::DialogImportShow() {
  // Open dialog for user to select files
  QStringList files = QFileDialog::getOpenFileNames(main_window_, tr("Import footage..."));

  // Check if the user actually selected files to import
  if (!files.isEmpty()) {
    // Locate the most recently focused Project panel (assume that's the panel the user wants to import into)
    ProjectPanel* active_project_panel = PanelManager::instance()->MostRecentlyFocused<ProjectPanel>();
    Project* active_project;

    if (active_project_panel == nullptr                                      // Check that we found a Project panel
        || (active_project = active_project_panel->project()) == nullptr) {  // and that we could find an active Project
      QMessageBox::critical(main_window_, tr("Failed to import footage"), tr("Failed to find active Project panel"));
      return;
    }

    // Get the selected folder in this panel
    Folder* folder = active_project_panel->GetSelectedFolder();

    ImportFiles(files, active_project_panel->model(), folder);
  }
}

void Core::DialogPreferencesShow() {
  PreferencesDialog pd(main_window_, main_window_->menuBar());
  pd.exec();
}

void Core::DialogProjectPropertiesShow() {
  ProjectPropertiesDialog ppd(main_window_);
  ppd.exec();
}

void Core::DialogExportShow() {
  ViewerPanel* latest_viewer = PanelManager::instance()->MostRecentlyFocused<ViewerPanel>();

  if (latest_viewer && latest_viewer->GetConnectedViewer()) {
    if (latest_viewer->GetConnectedViewer()->Length() == 0) {
      QMessageBox::critical(main_window_, tr("Error"), tr("This Sequence is empty. There is nothing to export."),
                            QMessageBox::Ok);
    } else {
      ExportDialog ed(latest_viewer->GetConnectedViewer(), main_window_);
      ed.exec();
    }
  }
}

void Core::CreateNewFolder() {
  // Locate the most recently focused Project panel (assume that's the panel the user wants to import into)
  ProjectPanel* active_project_panel = PanelManager::instance()->MostRecentlyFocused<ProjectPanel>();
  Project* active_project;

  if (active_project_panel == nullptr                                      // Check that we found a Project panel
      || (active_project = active_project_panel->project()) == nullptr) {  // and that we could find an active Project
    QMessageBox::critical(main_window_, tr("Failed to create new folder"), tr("Failed to find active Project panel"));
    return;
  }

  // Get the selected folder in this panel
  Folder* folder = active_project_panel->GetSelectedFolder();

  // Create new folder
  ItemPtr new_folder = std::make_shared<Folder>();

  // Set a default name
  new_folder->set_name(tr("New Folder"));

  // Create an undoable command
  ProjectViewModel::AddItemCommand* aic =
      new ProjectViewModel::AddItemCommand(active_project_panel->model(), folder, new_folder);

  Core::instance()->undo_stack()->push(aic);

  // Trigger an automatic rename so users can enter the folder name
  active_project_panel->Edit(new_folder.get());
}

void Core::CreateNewSequence() {
  // Locate the most recently focused Project panel (assume that's the panel the user wants to import into)
  ProjectPanel* active_project_panel = PanelManager::instance()->MostRecentlyFocused<ProjectPanel>();
  Project* active_project;

  if (active_project_panel == nullptr                                      // Check that we found a Project panel
      || (active_project = active_project_panel->project()) == nullptr) {  // and that we could find an active Project
    QMessageBox::critical(main_window_, tr("Failed to create new sequence"), tr("Failed to find active Project panel"));
    return;
  }

  // Get the selected folder in this panel
  Folder* folder = active_project_panel->GetSelectedFolder();

  // Create new sequence
  SequencePtr new_sequence = std::make_shared<Sequence>();

  // Set all defaults for the sequence
  new_sequence->set_default_parameters();

  // Get default name for this sequence (in the format "Sequence N", the first that doesn't exist)
  int sequence_number = 1;
  QString sequence_name;
  do {
    sequence_name = tr("Sequence %1").arg(sequence_number);
    sequence_number++;
  } while (active_project->root()->ChildExistsWithName(sequence_name));
  new_sequence->set_name(sequence_name);

  SequenceDialog sd(new_sequence.get(), SequenceDialog::kNew, main_window_);

  // Make sure SequenceDialog doesn't make an undo command for editing the sequence, since we make an undo command for
  // adding it later on
  sd.SetUndoable(false);

  if (sd.exec() == QDialog::Accepted) {
    // Create an undoable command
    ProjectViewModel::AddItemCommand* aic =
        new ProjectViewModel::AddItemCommand(active_project_panel->model(), folder, new_sequence);

    new_sequence->add_default_nodes();

    Core::instance()->undo_stack()->push(aic);

    Sequence::Open(new_sequence.get());
  }
}

void Core::AddOpenProject(ProjectPtr p) {
  open_projects_.append(p);

  emit ProjectOpened(p.get());
}

void Core::ImportTaskComplete(QUndoCommand* command) { undo_stack_.pushIfHasChildren(command); }

void Core::DeclareTypesForQt() {
  qRegisterMetaType<NodeDependency>();
  qRegisterMetaType<rational>();
  qRegisterMetaType<OpenGLTexturePtr>();
  qRegisterMetaType<OpenGLTextureCache::ReferencePtr>();
  qRegisterMetaType<NodeValueTable>();
  qRegisterMetaType<FramePtr>();
  qRegisterMetaType<AudioRenderingParams>();
  qRegisterMetaType<NodeKeyframe::Type>();
  qRegisterMetaType<Decoder::RetrieveState>();
  qRegisterMetaType<TimeRange>();
}

void Core::StartGUI(bool full_screen) {
  // Set UI style
  qApp->setStyle(QStyleFactory::create("Fusion"));
  StyleManager::SetStyle(StyleManager::DefaultStyle());

  // Set up shared menus
  MenuShared::CreateInstance();

  // Since we're starting GUI mode, create a PanelFocusManager (auto-deletes with QObject)
  PanelManager::CreateInstance();

  // Initialize audio service
  AudioManager::CreateInstance();

  // Initialize disk service
  DiskManager::CreateInstance();

  // Initialize task manager
  TaskManager::CreateInstance();

  // Initialize pixel service
  PixelService::CreateInstance();

  // Connect the PanelFocusManager to the application's focus change signal
  connect(qApp, &QApplication::focusChanged, PanelManager::instance(), &PanelManager::FocusChanged);

  // Create main window and open it
  main_window_ = new MainWindow();
  if (full_screen) {
    main_window_->showFullScreen();
  } else {
    main_window_->showMaximized();
  }

  // When a new project is opened, update the mainwindow
  connect(this, &Core::ProjectOpened, main_window_, &MainWindow::ProjectOpen);

  // Start autorecovery timer using the config value as its interval
  SetAutorecoveryInterval(Config::Current()["AutorecoveryInterval"].toInt());
  autorecovery_timer_.start();
}

void Core::SaveProjectInternal(Project* project) {
  // Create save manager
  ProjectSaveManager* psm = new ProjectSaveManager(project);

  TaskDialog* task_dialog = new TaskDialog(psm, tr("Save Project"), main_window());
  task_dialog->open();
}

void Core::SaveAutorecovery() {
  if (queue_autorecovery_) {
    // FIXME: Save autorecovery of projects open

    queue_autorecovery_ = false;
  }
}

Project* Core::GetActiveProject() {
  // Locate the most recently focused Project panel (assume that's the panel the user wants to import into)
  ProjectPanel* active_project_panel = PanelManager::instance()->MostRecentlyFocused<ProjectPanel>();

  // If we couldn't find one, return nullptr
  if (active_project_panel == nullptr) {
    return nullptr;
  }

  // Otherwise, return the project panel's project (which may be nullptr but in most cases shouldn't be)
  return active_project_panel->project();
}

void Core::SetProjectModified() {
  main_window()->setWindowModified(true);
  queue_autorecovery_ = true;
}

void Core::SetAutorecoveryInterval(int minutes) {
  // Convert minutes to milliseconds
  autorecovery_timer_.setInterval(minutes * 60000);
}

void Core::SaveActiveProject() {
  Project* active_project = GetActiveProject();

  if (!active_project) {
    return;
  }

  if (active_project->filename().isEmpty()) {
    SaveActiveProjectAs();
  } else {
    SaveProjectInternal(active_project);
  }
}

void Core::SaveActiveProjectAs() {
  Project* active_project = GetActiveProject();

  if (!active_project) {
    return;
  }

  QString fn = QFileDialog::getSaveFileName(main_window_, tr("Save Project As"), QString(), GetProjectFilter());

  if (!fn.isEmpty()) {
    active_project->set_filename(fn);

    SaveProjectInternal(active_project);
  }
}

QList<rational> Core::SupportedFrameRates() {
  QList<rational> frame_rates;

  frame_rates.append(rational(10, 1));        // 10 FPS
  frame_rates.append(rational(15, 1));        // 15 FPS
  frame_rates.append(rational(24000, 1001));  // 23.976 FPS
  frame_rates.append(rational(24, 1));        // 24 FPS
  frame_rates.append(rational(25, 1));        // 25 FPS
  frame_rates.append(rational(30000, 1001));  // 29.97 FPS
  frame_rates.append(rational(30, 1));        // 30 FPS
  frame_rates.append(rational(48000, 1001));  // 47.952 FPS
  frame_rates.append(rational(48, 1));        // 48 FPS
  frame_rates.append(rational(50, 1));        // 50 FPS
  frame_rates.append(rational(60000, 1001));  // 59.94 FPS
  frame_rates.append(rational(60, 1));        // 60 FPS

  return frame_rates;
}

QList<int> Core::SupportedSampleRates() {
  QList<int> sample_rates;

  sample_rates.append(8000);   // 8000 Hz
  sample_rates.append(11025);  // 11025 Hz
  sample_rates.append(16000);  // 16000 Hz
  sample_rates.append(22050);  // 22050 Hz
  sample_rates.append(24000);  // 24000 Hz
  sample_rates.append(32000);  // 32000 Hz
  sample_rates.append(44100);  // 44100 Hz
  sample_rates.append(48000);  // 48000 Hz
  sample_rates.append(88200);  // 88200 Hz
  sample_rates.append(96000);  // 96000 Hz

  return sample_rates;
}

QList<uint64_t> Core::SupportedChannelLayouts() {
  QList<uint64_t> channel_layouts;

  channel_layouts.append(AV_CH_LAYOUT_MONO);
  channel_layouts.append(AV_CH_LAYOUT_STEREO);
  channel_layouts.append(AV_CH_LAYOUT_5POINT1);
  channel_layouts.append(AV_CH_LAYOUT_7POINT1);

  return channel_layouts;
}

QString Core::FrameRateToString(const rational& frame_rate) { return tr("%1 FPS").arg(frame_rate.toDouble()); }

QString Core::SampleRateToString(const int& sample_rate) { return tr("%1 Hz").arg(sample_rate); }

QString Core::ChannelLayoutToString(const uint64_t& layout) {
  switch (layout) {
    case AV_CH_LAYOUT_MONO:
      return tr("Mono");
    case AV_CH_LAYOUT_STEREO:
      return tr("Stereo");
    case AV_CH_LAYOUT_5POINT1:
      return tr("5.1");
    case AV_CH_LAYOUT_7POINT1:
      return tr("7.1");
    default:
      return tr("Unknown (0x%1)").arg(layout, 1, 16);
  }
}

QString Core::GetProjectFilter() { return QStringLiteral("%1 (*.ove)").arg(tr("Olive Project")); }

void Core::OpenProjectInternal(const QString& filename) {
  ProjectLoadManager* plm = new ProjectLoadManager(filename);

  // We use a blocking queued connection here because we want to ensure we have this project instance before the
  // ProjectLoadManager is destroyed
  connect(plm, &ProjectLoadManager::ProjectLoaded, this, &Core::AddOpenProject, Qt::BlockingQueuedConnection);

  TaskDialog* task_dialog = new TaskDialog(plm, tr("Load Project"), main_window());
  task_dialog->open();
}

int Core::CountFilesInFileList(const QFileInfoList& filenames) {
  int file_count = 0;

  foreach (const QFileInfo& f, filenames) {
    // For some reason QDir::NoDotAndDotDot	doesn't work with entryInfoList, so we have to check manually
    if (f.fileName() == "." || f.fileName() == "..") {
      continue;
    } else if (f.isDir()) {
      QFileInfoList info_list = QDir(f.absoluteFilePath()).entryInfoList();

      file_count += CountFilesInFileList(info_list);
    } else {
      file_count++;
    }
  }

  return file_count;
}

QString GetRenderModePreferencePrefix(RenderMode::Mode mode, const QString& preference) {
  QString key;

  key.append((mode == RenderMode::kOffline) ? QStringLiteral("Offline") : QStringLiteral("Online"));
  key.append(preference);

  return key;
}

QVariant Core::GetPreferenceForRenderMode(RenderMode::Mode mode, const QString& preference) {
  return Config::Current()[GetRenderModePreferencePrefix(mode, preference)];
}

void Core::SetPreferenceForRenderMode(RenderMode::Mode mode, const QString& preference, const QVariant& value) {
  Config::Current()[GetRenderModePreferencePrefix(mode, preference)] = value;
}

void Core::OpenProject() {
  QString file = QFileDialog::getOpenFileName(main_window_, tr("Open Project"), QString(), GetProjectFilter());

  if (!file.isEmpty()) {
    OpenProjectInternal(file);
  }
}
