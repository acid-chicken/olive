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
#include <QClipboard>
#include <QCommandLineParser>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QStyleFactory>

#include "audio/audiomanager.h"
#include "cli/clitask/clitaskdialog.h"
#include "common/filefunctions.h"
#include "common/xmlutils.h"
#include "config/config.h"
#include "dialog/about/about.h"
#include "dialog/export/export.h"
#include "dialog/sequence/sequence.h"
#include "dialog/task/task.h"
#include "dialog/preferences/preferences.h"
#include "dialog/projectproperties/projectproperties.h"
#include "node/factory.h"
#include "panel/panelmanager.h"
#include "panel/project/project.h"
#include "panel/viewer/viewer.h"
#include "project/projectimportmanager.h"
#include "project/projectloadmanager.h"
#include "project/projectsavemanager.h"
#include "render/backend/opengl/opengltexturecache.h"
#include "render/colormanager.h"
#include "render/diskmanager.h"
#include "render/pixelformat.h"
#include "task/taskmanager.h"
#include "ui/style/style.h"
#include "undo/undostack.h"
#include "widget/menu/menushared.h"
#include "widget/taskview/taskviewitem.h"
#include "window/mainwindow/mainwindow.h"

OLIVE_NAMESPACE_ENTER

Core Core::instance_;

Core::Core() :
    main_window_(nullptr),
    tool_(Tool::kPointer),
    addable_object_(Tool::kAddableEmpty),
    snapping_(true),
    gui_active_(false)
{
}

Core *Core::instance()
{
    return &instance_;
}

bool Core::Start()
{
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

    // Create headless export option
    QCommandLineOption headless_export_option({"x", "export"}, tr("Export project from command line"));
    parser.addOption(headless_export_option);

    // Parse options
    parser.process(*app);

    QStringList args = parser.positionalArguments();

    // Detect project to load on startup
    if (!args.isEmpty()) {
        startup_project_ = args.first();
    }

    // Declare custom types for Qt signal/slot system
    DeclareTypesForQt();

    // Set up node factory/library
    NodeFactory::Initialize();

    // Set up color manager's default config
    ColorManager::SetUpDefaultConfig();

    // Initialize task manager
    TaskManager::CreateInstance();

    // Reset config (Config sets to default on construction already, but we do it again here as a workaround that fixes
    //               the fact that some of the config paths set by default rely on the app name having been set (in main())
    Config::Current().SetDefaults();

    // Load application config
    Config::Load();


    //
    // Start application
    //

    qInfo() << "Using Qt version:" << qVersion();

    gui_active_ = !parser.isSet(headless_export_option);

    if (gui_active_) {

        // Start GUI
        StartGUI(parser.isSet(fullscreen_option));

        // Load startup project
        if (!startup_project_.isEmpty() && !QFileInfo::exists(startup_project_)) {
            QMessageBox::warning(main_window(),
                                 tr("Failed to open startup file"),
                                 tr("The project \"%1\" doesn't exist. A new project will be started instead.").arg(startup_project_),
                                 QMessageBox::Ok);

            startup_project_.clear();
        }

        if (startup_project_.isEmpty()) {
            // If no load project is set, create a new one on open
            CreateNewProject();
        } else {
            OpenProjectInternal(startup_project_);
        }

        return true;

    } else {

        if (parser.isSet(headless_export_option)) {

            if (startup_project_.isEmpty()) {
                qCritical().noquote() << tr("You must specify a project file to export");
            } else {
                OpenProjectInternal(startup_project_);

                qDebug() << "Ready for exporting!";

                return true;
            }

        }

        // Error fallback
        return false;

    }
}

void Core::Stop()
{
    // Save Config
    //Config::Save();

    // Save recently opened projects
    {
        QFile recent_projects_file(GetRecentProjectsFilePath());
        if (recent_projects_file.open(QFile::WriteOnly | QFile::Text)) {
            QTextStream ts(&recent_projects_file);

            foreach (const QString& s, recent_projects_) {
                ts << s << "\n";
            }

            recent_projects_file.close();
        }
    }

    MenuShared::DestroyInstance();

    TaskManager::DestroyInstance();

    PanelManager::DestroyInstance();

    AudioManager::DestroyInstance();

    DiskManager::DestroyInstance();

    PixelFormat::DestroyInstance();

    NodeFactory::Destroy();

    delete main_window_;
}

MainWindow *Core::main_window()
{
    return main_window_;
}

UndoStack *Core::undo_stack()
{
    return &undo_stack_;
}

void Core::ImportFiles(const QStringList &urls, ProjectViewModel* model, Folder* parent)
{
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

const Tool::Item &Core::tool() const
{
    return tool_;
}

const Tool::AddableObject &Core::selected_addable_object() const
{
    return addable_object_;
}

void Core::SetSelectedAddableObject(const Tool::AddableObject &obj)
{
    addable_object_ = obj;
}

void Core::ClearOpenRecentList()
{
    recent_projects_.clear();
}

void Core::CreateNewProject()
{
    // If we already have an empty/new project, switch to it
    foreach (ProjectPtr already_open, open_projects_) {
        if (already_open->is_new()) {
            AddOpenProject(already_open);
            return;
        }
    }

    AddOpenProject(std::make_shared<Project>());
}

const bool &Core::snapping() const
{
    return snapping_;
}

const QStringList &Core::GetRecentProjects() const
{
    return recent_projects_;
}

ProjectPtr Core::GetSharedPtrFromProject(Project *project) const
{
    foreach (ProjectPtr p, open_projects_) {
        if (p.get() == project) {
            return p;
        }
    }

    return nullptr;
}

void Core::SetTool(const Tool::Item &tool)
{
    tool_ = tool;

    emit ToolChanged(tool_);
}

void Core::SetSnapping(const bool &b)
{
    snapping_ = b;

    emit SnappingChanged(snapping_);
}

void Core::DialogAboutShow()
{
    AboutDialog a(main_window_);
    a.exec();
}

void Core::DialogImportShow()
{
    // Open dialog for user to select files
    QStringList files = QFileDialog::getOpenFileNames(main_window_,
                        tr("Import footage..."));

    // Check if the user actually selected files to import
    if (!files.isEmpty()) {

        // Locate the most recently focused Project panel (assume that's the panel the user wants to import into)
        ProjectPanel* active_project_panel = PanelManager::instance()->MostRecentlyFocused<ProjectPanel>();
        Project* active_project;

        if (active_project_panel == nullptr // Check that we found a Project panel
                || (active_project = active_project_panel->project()) == nullptr) { // and that we could find an active Project
            QMessageBox::critical(main_window_, tr("Failed to import footage"), tr("Failed to find active Project panel"));
            return;
        }

        // Get the selected folder in this panel
        Folder* folder = active_project_panel->GetSelectedFolder();

        ImportFiles(files, active_project_panel->model(), folder);
    }
}

void Core::DialogPreferencesShow()
{
    PreferencesDialog pd(main_window_, main_window_->menuBar());
    pd.exec();
}

void Core::DialogProjectPropertiesShow()
{
    ProjectPtr proj = GetActiveProject();

    if (proj) {
        ProjectPropertiesDialog ppd(proj.get(), main_window_);
        ppd.exec();
    } else {
        QMessageBox::critical(main_window_,
                              tr("No Active Project"),
                              tr("No project is currently open to set the properties for"),
                              QMessageBox::Ok);
    }
}

void Core::DialogExportShow()
{
    TimeBasedPanel* latest_time_based = PanelManager::instance()->MostRecentlyFocused<TimeBasedPanel>();

    if (latest_time_based && latest_time_based->GetConnectedViewer()) {
        if (latest_time_based->GetConnectedViewer()->Length() == 0) {
            QMessageBox::critical(main_window_,
                                  tr("Error"),
                                  tr("This Sequence is empty. There is nothing to export."),
                                  QMessageBox::Ok);
        } else {
            ExportDialog ed(latest_time_based->GetConnectedViewer(), main_window_);
            ed.exec();
        }
    }
}

void Core::CreateNewFolder()
{
    // Locate the most recently focused Project panel (assume that's the panel the user wants to import into)
    ProjectPanel* active_project_panel = PanelManager::instance()->MostRecentlyFocused<ProjectPanel>();
    Project* active_project;

    if (active_project_panel == nullptr // Check that we found a Project panel
            || (active_project = active_project_panel->project()) == nullptr) { // and that we could find an active Project
        QMessageBox::critical(main_window_, tr("Failed to create new folder"), tr("Failed to find active project"));
        return;
    }

    // Get the selected folder in this panel
    Folder* folder = active_project_panel->GetSelectedFolder();

    // Create new folder
    ItemPtr new_folder = std::make_shared<Folder>();

    // Set a default name
    new_folder->set_name(tr("New Folder"));

    // Create an undoable command
    ProjectViewModel::AddItemCommand* aic = new ProjectViewModel::AddItemCommand(active_project_panel->model(),
            folder,
            new_folder);

    Core::instance()->undo_stack()->push(aic);

    // Trigger an automatic rename so users can enter the folder name
    active_project_panel->Edit(new_folder.get());
}

void Core::CreateNewSequence()
{
    ProjectPtr active_project = GetActiveProject();

    if (!active_project) {
        QMessageBox::critical(main_window_, tr("Failed to create new sequence"), tr("Failed to find active project"));
        return;
    }

    // Create new sequence
    SequencePtr new_sequence = CreateNewSequenceForProject(active_project.get());

    // Set all defaults for the sequence
    new_sequence->set_default_parameters();

    SequenceDialog sd(new_sequence.get(), SequenceDialog::kNew, main_window_);

    // Make sure SequenceDialog doesn't make an undo command for editing the sequence, since we make an undo command for
    // adding it later on
    sd.SetUndoable(false);

    if (sd.exec() == QDialog::Accepted) {
        // Create an undoable command
        ProjectViewModel::AddItemCommand* aic = new ProjectViewModel::AddItemCommand(GetActiveProjectModel(),
                GetSelectedFolderInActiveProject(),
                new_sequence);

        new_sequence->add_default_nodes();

        Core::instance()->undo_stack()->push(aic);

        Core::instance()->main_window()->OpenSequence(new_sequence.get());
    }
}

void Core::AddOpenProject(ProjectPtr p)
{
    // Ensure project is not open at the moment
    foreach (ProjectPtr already_open, open_projects_) {
        if (already_open == p) {
            // Signal UI to switch to this project
            emit ProjectOpened(p.get());
            return;
        }
    }

    // If we currently have an empty project, close it first
    if (!open_projects_.isEmpty() && open_projects_.last()->is_new()) {
        CloseProject(open_projects_.last(), false);
    }

    connect(p.get(), &Project::ModifiedChanged, this, &Core::ProjectWasModified);
    open_projects_.append(p);

    PushRecentlyOpenedProject(p->filename());

    emit ProjectOpened(p.get());
}

void Core::ImportTaskComplete(QUndoCommand *command)
{
    undo_stack_.pushIfHasChildren(command);
}

bool Core::ConfirmImageSequence(const QString& filename)
{
    QMessageBox mb(main_window_);

    mb.setIcon(QMessageBox::Question);
    mb.setWindowTitle(tr("Possible image sequence detected"));
    mb.setText(tr("The file '%1' looks like it might be part of an image "
                  "sequence. Would you like to import it as such?").arg(filename));

    mb.addButton(QMessageBox::Yes);
    mb.addButton(QMessageBox::No);

    return (mb.exec() == QMessageBox::Yes);
}

void Core::ProjectWasModified(bool e)
{
    //Project* p = static_cast<Project*>(sender());

    if (e) {
        // If this project is modified, we know for sure the window should show a "modified" flag (the * in the titlebar)
        main_window_->setWindowModified(true);
    } else {
        // If we just set this project to "not modified", see if all projects are not modified in which case we can hide
        // the modified flag
        foreach (ProjectPtr open, open_projects_) {
            if (open->is_modified()) {
                main_window_->setWindowModified(true);
                return;
            }
        }

        main_window_->setWindowModified(false);
    }
}

void Core::DeclareTypesForQt()
{
    qRegisterMetaType<NodeDependency>();
    qRegisterMetaType<rational>();
    qRegisterMetaType<OpenGLTexturePtr>();
    qRegisterMetaType<OpenGLTextureCache::ReferencePtr>();
    qRegisterMetaType<NodeValueTable>();
    qRegisterMetaType<NodeValueDatabase>();
    qRegisterMetaType<FramePtr>();
    qRegisterMetaType<SampleBufferPtr>();
    qRegisterMetaType<AudioRenderingParams>();
    qRegisterMetaType<NodeKeyframe::Type>();
    qRegisterMetaType<Decoder::RetrieveState>();
    qRegisterMetaType<OLIVE_NAMESPACE::TimeRange>();
    qRegisterMetaType<Color>();
    qRegisterMetaType<OLIVE_NAMESPACE::ProjectPtr>();
}

void Core::StartGUI(bool full_screen)
{
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

    // Initialize pixel service
    PixelFormat::CreateInstance();

    // Connect the PanelFocusManager to the application's focus change signal
    connect(qApp,
            &QApplication::focusChanged,
            PanelManager::instance(),
            &PanelManager::FocusChanged);

    // Create main window and open it
    main_window_ = new MainWindow();
    if (full_screen) {
        main_window_->showFullScreen();
    } else {
        main_window_->showMaximized();
    }

    // When a new project is opened, update the mainwindow
    connect(this, &Core::ProjectOpened, main_window_, &MainWindow::ProjectOpen);
    connect(this, &Core::ProjectClosed, main_window_, &MainWindow::ProjectClose);

    // Start autorecovery timer using the config value as its interval
    SetAutorecoveryInterval(Config::Current()["AutorecoveryInterval"].toInt());
    autorecovery_timer_.start();

    // Load recently opened projects list
    {
        QFile recent_projects_file(GetRecentProjectsFilePath());
        if (recent_projects_file.open(QFile::ReadOnly | QFile::Text)) {
            QTextStream ts(&recent_projects_file);

            while (!ts.atEnd()) {
                recent_projects_.append(ts.readLine());
            }

            recent_projects_file.close();
        }
    }
}

void Core::SaveProjectInternal(ProjectPtr project)
{
    // Create save manager
    ProjectSaveManager* psm = new ProjectSaveManager(project);

    connect(psm, &ProjectSaveManager::ProjectSaveSucceeded, this, &Core::ProjectSaveSucceeded);

    TaskDialog* task_dialog = new TaskDialog(psm, tr("Save Project"), main_window());
    task_dialog->open();
}

void Core::SaveAutorecovery()
{
    foreach (ProjectPtr p, open_projects_) {
        if (!p->has_autorecovery_been_saved()) {
            // FIXME: SAVE AN AUTORECOVERY PROJECT
            p->set_autorecovery_saved(true);
        }
    }
}

void Core::ProjectSaveSucceeded(ProjectPtr p)
{
    PushRecentlyOpenedProject(p->filename());

    p->set_modified(false);
}

ProjectPtr Core::GetActiveProject() const
{
    ProjectPanel* active_project_panel = PanelManager::instance()->MostRecentlyFocused<ProjectPanel>();

    if (active_project_panel && active_project_panel->project()) {
        return GetSharedPtrFromProject(active_project_panel->project());
    }

    return nullptr;
}

ProjectViewModel *Core::GetActiveProjectModel() const
{
    ProjectPanel* active_project_panel = PanelManager::instance()->MostRecentlyFocused<ProjectPanel>();

    if (active_project_panel) {
        return active_project_panel->model();
    } else {
        return nullptr;
    }
}

Folder *Core::GetSelectedFolderInActiveProject() const
{
    ProjectPanel* active_project_panel = PanelManager::instance()->MostRecentlyFocused<ProjectPanel>();

    if (active_project_panel) {
        return active_project_panel->GetSelectedFolder();
    } else {
        return nullptr;
    }
}

Timecode::Display Core::GetTimecodeDisplay() const
{
    return static_cast<Timecode::Display>(Config::Current()["TimecodeDisplay"].toInt());
}

void Core::SetTimecodeDisplay(Timecode::Display d)
{
    Config::Current()["TimecodeDisplay"] = d;

    emit TimecodeDisplayChanged(d);
}

void Core::SetAutorecoveryInterval(int minutes)
{
    // Convert minutes to milliseconds
    autorecovery_timer_.setInterval(minutes * 60000);
}

void Core::CopyStringToClipboard(const QString &s)
{
    QGuiApplication::clipboard()->setText(s);
}

QString Core::PasteStringFromClipboard()
{
    return QGuiApplication::clipboard()->text();
}

bool Core::SaveActiveProject()
{
    ProjectPtr active_project = GetActiveProject();

    if (active_project) {
        return SaveProject(active_project);
    }

    return false;
}

bool Core::SaveActiveProjectAs()
{
    ProjectPtr active_project = GetActiveProject();

    if (active_project) {
        return SaveProjectAs(active_project);
    }

    return false;
}

bool Core::SaveAllProjects()
{
    foreach (ProjectPtr p, open_projects_) {
        if (!SaveProject(p)) {
            return false;
        }
    }

    return true;
}

bool Core::CloseActiveProject()
{
    return CloseProject(GetActiveProject(), true);
}

bool Core::CloseAllExceptActiveProject()
{
    ProjectPtr active_proj = GetActiveProject();
    QList<ProjectPtr> copy = open_projects_;

    foreach (ProjectPtr p, copy) {
        if (p != active_proj) {
            if (!CloseProject(p, true)) {
                return false;
            }
        }
    }

    return true;
}

QList<rational> Core::SupportedFrameRates()
{
    QList<rational> frame_rates;

    frame_rates.append(rational(10, 1));            // 10 FPS
    frame_rates.append(rational(15, 1));            // 15 FPS
    frame_rates.append(rational(24000, 1001));      // 23.976 FPS
    frame_rates.append(rational(24, 1));            // 24 FPS
    frame_rates.append(rational(25, 1));            // 25 FPS
    frame_rates.append(rational(30000, 1001));      // 29.97 FPS
    frame_rates.append(rational(30, 1));            // 30 FPS
    frame_rates.append(rational(48000, 1001));      // 47.952 FPS
    frame_rates.append(rational(48, 1));            // 48 FPS
    frame_rates.append(rational(50, 1));            // 50 FPS
    frame_rates.append(rational(60000, 1001));      // 59.94 FPS
    frame_rates.append(rational(60, 1));            // 60 FPS

    return frame_rates;
}

QList<int> Core::SupportedSampleRates()
{
    QList<int> sample_rates;

    sample_rates.append(8000);         // 8000 Hz
    sample_rates.append(11025);        // 11025 Hz
    sample_rates.append(16000);        // 16000 Hz
    sample_rates.append(22050);        // 22050 Hz
    sample_rates.append(24000);        // 24000 Hz
    sample_rates.append(32000);        // 32000 Hz
    sample_rates.append(44100);        // 44100 Hz
    sample_rates.append(48000);        // 48000 Hz
    sample_rates.append(88200);        // 88200 Hz
    sample_rates.append(96000);        // 96000 Hz

    return sample_rates;
}

QList<uint64_t> Core::SupportedChannelLayouts()
{
    QList<uint64_t> channel_layouts;

    channel_layouts.append(AV_CH_LAYOUT_MONO);
    channel_layouts.append(AV_CH_LAYOUT_STEREO);
    channel_layouts.append(AV_CH_LAYOUT_2_1);
    channel_layouts.append(AV_CH_LAYOUT_5POINT1);
    channel_layouts.append(AV_CH_LAYOUT_7POINT1);

    return channel_layouts;
}

QString Core::FrameRateToString(const rational &frame_rate)
{
    return tr("%1 FPS").arg(frame_rate.toDouble());
}

QString Core::SampleRateToString(const int &sample_rate)
{
    return tr("%1 Hz").arg(sample_rate);
}

QString Core::ChannelLayoutToString(const uint64_t &layout)
{
    switch (layout) {
    case AV_CH_LAYOUT_MONO:
        return tr("Mono");
    case AV_CH_LAYOUT_STEREO:
        return tr("Stereo");
    case AV_CH_LAYOUT_2_1:
        return tr("2.1");
    case AV_CH_LAYOUT_5POINT1:
        return tr("5.1");
    case AV_CH_LAYOUT_7POINT1:
        return tr("7.1");
    default:
        return tr("Unknown (0x%1)").arg(layout, 1, 16);
    }
}

QString Core::GetProjectFilter()
{
    return QStringLiteral("%1 (*.ove)").arg(tr("Olive Project"));
}

QString Core::GetRecentProjectsFilePath()
{
    return QDir(FileFunctions::GetConfigurationLocation()).filePath(QStringLiteral("recent"));
}

bool Core::SaveProject(ProjectPtr p)
{
    if (p->filename().isEmpty()) {
        return SaveProjectAs(p);
    } else {
        SaveProjectInternal(p);

        return true;
    }
}

bool Core::SaveProjectAs(ProjectPtr p)
{
    QString fn = QFileDialog::getSaveFileName(main_window_,
                 tr("Save Project As"),
                 QString(),
                 GetProjectFilter());

    if (!fn.isEmpty()) {
        p->set_filename(fn);

        SaveProjectInternal(p);

        return true;
    }

    return false;
}

void Core::PushRecentlyOpenedProject(const QString& s)
{
    if (s.isEmpty()) {
        return;
    }

    int existing_index = recent_projects_.indexOf(s);

    if (existing_index >= 0) {
        recent_projects_.move(existing_index, 0);
    } else {
        recent_projects_.prepend(s);
    }
}

void Core::OpenProjectInternal(const QString &filename)
{
    // See if this project is open already
    foreach (ProjectPtr p, open_projects_) {
        if (p->filename() == filename) {
            // This project is already open
            AddOpenProject(p);
            return;
        }
    }

    ProjectLoadManager* plm = new ProjectLoadManager(filename);

    if (gui_active_) {

        // We use a blocking queued connection here because we want to ensure we have this project instance before the
        // ProjectLoadManager is destroyed
        connect(plm, &ProjectLoadManager::ProjectLoaded, this, &Core::AddOpenProject, Qt::BlockingQueuedConnection);

        TaskDialog* task_dialog = new TaskDialog(plm, tr("Load Project"), main_window());
        task_dialog->open();

    } else {

        connect(plm, &ProjectLoadManager::ProjectLoaded, this, &Core::AddOpenProject);

        CLITaskDialog task_dialog(plm);

    }
}

int Core::CountFilesInFileList(const QFileInfoList &filenames)
{
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

QString GetRenderModePreferencePrefix(RenderMode::Mode mode, const QString &preference) {
    QString key;

    key.append((mode == RenderMode::kOffline) ? QStringLiteral("Offline") : QStringLiteral("Online"));
    key.append(preference);

    return key;
}

QVariant Core::GetPreferenceForRenderMode(RenderMode::Mode mode, const QString &preference)
{
    return Config::Current()[GetRenderModePreferencePrefix(mode, preference)];
}

void Core::SetPreferenceForRenderMode(RenderMode::Mode mode, const QString &preference, const QVariant &value)
{
    Config::Current()[GetRenderModePreferencePrefix(mode, preference)] = value;
}

SequencePtr Core::CreateNewSequenceForProject(Project* project) const
{
    SequencePtr new_sequence = std::make_shared<Sequence>();

    // Get default name for this sequence (in the format "Sequence N", the first that doesn't exist)
    int sequence_number = 1;
    QString sequence_name;
    do {
        sequence_name = tr("Sequence %1").arg(sequence_number);
        sequence_number++;
    } while (project->root()->ChildExistsWithName(sequence_name));
    new_sequence->set_name(sequence_name);

    return new_sequence;
}

void Core::OpenProjectFromRecentList(int index)
{
    const QString& open_fn = recent_projects_.at(index);

    if (QFileInfo::exists(open_fn)) {
        OpenProjectInternal(open_fn);
    } else if (QMessageBox::information(main_window(),
                                        tr("Cannot open recent project"),
                                        tr("The project \"%1\" doesn't exist. Would you like to remove this file from the recent list?").arg(open_fn),
                                        QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        recent_projects_.removeAt(index);
    }
}

bool Core::CloseProject(ProjectPtr p, bool auto_open_new)
{
    CloseProjectBehavior b = kCloseProjectOnlyOne;
    return CloseProject(p, auto_open_new, b);
}

bool Core::CloseProject(ProjectPtr p, bool auto_open_new, CloseProjectBehavior &confirm_behavior)
{
    for (int i=0; i<open_projects_.size(); i++) {
        if (open_projects_.at(i) == p) {

            if (p->is_modified() && confirm_behavior != kCloseProjectDontSave) {

                bool save_this_project;

                if (confirm_behavior == kCloseProjectAsk || confirm_behavior == kCloseProjectOnlyOne) {
                    QMessageBox mb(main_window_);

                    mb.setWindowModality(Qt::WindowModal);
                    mb.setIcon(QMessageBox::Question);
                    mb.setWindowTitle(tr("Unsaved Changes"));
                    mb.setText(tr("The project '%1' has unsaved changes. Would you like to save them?")
                               .arg(p->name()));

                    QPushButton* yes_btn = mb.addButton(tr("Save"), QMessageBox::YesRole);

                    QPushButton* yes_to_all_btn;
                    if (confirm_behavior == kCloseProjectOnlyOne) {
                        yes_to_all_btn = nullptr;
                    } else {
                        yes_to_all_btn = mb.addButton(tr("Save All"), QMessageBox::YesRole);
                    }

                    mb.addButton(tr("Don't Save"), QMessageBox::NoRole);

                    QPushButton* no_to_all_btn;
                    if (confirm_behavior == kCloseProjectOnlyOne) {
                        no_to_all_btn = nullptr;
                    } else {
                        no_to_all_btn = mb.addButton(tr("Don't Save All"), QMessageBox::NoRole);
                    }

                    QPushButton* cancel_btn = mb.addButton(QMessageBox::Cancel);

                    mb.exec();

                    if (mb.clickedButton() == cancel_btn) {
                        // Stop closing projects if the user clicked cancel
                        return false;
                    } else if (mb.clickedButton() == yes_to_all_btn) {
                        // Set flag that other CloseProject commands are going to use
                        confirm_behavior = kCloseProjectSave;
                    } else if (mb.clickedButton() == no_to_all_btn) {
                        // Set flag that other CloseProject commands are going to use
                        confirm_behavior = kCloseProjectDontSave;
                    }

                    save_this_project = (mb.clickedButton() == yes_btn || mb.clickedButton() == yes_to_all_btn);

                } else {
                    // We must be saving this project
                    save_this_project = true;
                }

                if (save_this_project && !SaveProject(p)) {
                    // The save failed, stop closing projects
                    return false;
                }

            }

            // For safety, the undo stack is cleared so no commands try to affect a freed project
            undo_stack_.clear();

            disconnect(p.get(), &Project::ModifiedChanged, this, &Core::ProjectWasModified);
            emit ProjectClosed(p.get());
            open_projects_.removeAt(i);
            break;
        }
    }

    // Ensure a project is always active
    if (auto_open_new && open_projects_.isEmpty()) {
        CreateNewProject();
    }

    return true;
}

bool Core::CloseAllProjects(bool auto_open_new)
{
    QList<ProjectPtr> copy = open_projects_;

    // See how many projects are modified so we can set "behavior" correctly
    // (i.e. whether to show "Yes/No To All" buttons or not)
    int modified_count = 0;
    foreach (ProjectPtr p, copy) {
        if (p->is_modified()) {
            modified_count++;
        }
    }

    CloseProjectBehavior behavior;

    if (modified_count > 1) {
        behavior = kCloseProjectAsk;
    } else {
        behavior = kCloseProjectOnlyOne;
    }

    foreach (ProjectPtr p, copy) {
        // If this is the only remaining project and the user hasn't chose "yes/no to all", hide those buttons
        if (modified_count == 1 && behavior == kCloseProjectAsk) {
            behavior = kCloseProjectOnlyOne;
        }

        if (!CloseProject(p, auto_open_new, behavior)) {
            return false;
        }

        modified_count--;
    }

    return true;
}

bool Core::CloseAllProjects()
{
    return CloseAllProjects(true);
}

void Core::OpenProject()
{
    QString file = QFileDialog::getOpenFileName(main_window_,
                   tr("Open Project"),
                   QString(),
                   GetProjectFilter());

    if (!file.isEmpty()) {
        OpenProjectInternal(file);
    }
}

OLIVE_NAMESPACE_EXIT
