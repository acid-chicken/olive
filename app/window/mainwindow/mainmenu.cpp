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

#include "mainmenu.h"

#include <QEvent>

#include "common/timecodefunctions.h"
#include "core.h"
#include "dialog/actionsearch/actionsearch.h"
#include "mainwindow.h"
#include "panel/panelmanager.h"
#include "tool/tool.h"
#include "ui/style/style.h"
#include "undo/undostack.h"
#include "widget/menu/menushared.h"

MainMenu::MainMenu(QMainWindow* parent) : QMenuBar(parent) {
  //
  // FILE MENU
  //
  file_menu_ = new Menu(this, this, SLOT(FileMenuAboutToShow()));
  file_new_menu_ = new Menu(file_menu_);
  MenuShared::instance()->AddItemsForNewMenu(file_new_menu_);
  file_open_item_ = file_menu_->AddItem("openproj", Core::instance(), SLOT(OpenProject()), "Ctrl+O");
  file_open_recent_menu_ = new Menu(file_menu_);
  file_open_recent_clear_item_ = file_open_recent_menu_->AddItem("clearopenrecent", nullptr, nullptr);
  file_save_item_ = file_menu_->AddItem("saveproj", Core::instance(), SLOT(SaveActiveProject()), "Ctrl+S");
  file_save_as_item_ = file_menu_->AddItem("saveprojas", Core::instance(), SLOT(SaveActiveProjectAs()), "Ctrl+Shift+S");
  file_menu_->addSeparator();
  file_import_item_ = file_menu_->AddItem("import", Core::instance(), SLOT(DialogImportShow()), "Ctrl+I");
  file_menu_->addSeparator();
  file_export_item_ = file_menu_->AddItem("export", Core::instance(), SLOT(DialogExportShow()), "Ctrl+M");
  file_menu_->addSeparator();
  file_project_properties_item_ =
      file_menu_->AddItem("projectproperties", Core::instance(), SLOT(DialogProjectPropertiesShow()));
  file_menu_->addSeparator();
  file_exit_item_ = file_menu_->AddItem("exit", parent, SLOT(close()), "Ctrl+Q");

  //
  // EDIT MENU
  //
  edit_menu_ = new Menu(this);

  edit_undo_item_ = Core::instance()->undo_stack()->createUndoAction(this);
  Menu::ConformItem(edit_undo_item_, "undo", nullptr, nullptr, "Ctrl+Z");
  edit_menu_->addAction(edit_undo_item_);
  edit_redo_item_ = Core::instance()->undo_stack()->createRedoAction(this);
  Menu::ConformItem(edit_redo_item_, "redo", nullptr, nullptr, "Ctrl+Shift+Z");
  edit_menu_->addAction(edit_redo_item_);

  edit_menu_->addSeparator();
  MenuShared::instance()->AddItemsForEditMenu(edit_menu_);
  edit_menu_->addSeparator();
  edit_select_all_item_ = edit_menu_->AddItem("selectall", this, SLOT(SelectAllTriggered()), "Ctrl+A");
  edit_deselect_all_item_ = edit_menu_->AddItem("deselectall", this, SLOT(DeselectAllTriggered()), "Ctrl+Shift+A");
  edit_menu_->addSeparator();
  MenuShared::instance()->AddItemsForClipEditMenu(edit_menu_);
  edit_menu_->addSeparator();
  edit_insert_item_ = edit_menu_->AddItem("insert", this, SLOT(InsertTriggered()), ",");
  edit_overwrite_item_ = edit_menu_->AddItem("overwrite", this, SLOT(OverwriteTriggered()), ".");
  edit_menu_->addSeparator();
  edit_ripple_to_in_item_ = edit_menu_->AddItem("rippletoin", this, SLOT(RippleToInTriggered()), "Q");
  edit_ripple_to_out_item_ = edit_menu_->AddItem("rippletoout", this, SLOT(RippleToOutTriggered()), "W");
  edit_edit_to_in_item_ = edit_menu_->AddItem("edittoin", this, SLOT(EditToInTriggered()), "Ctrl+Alt+Q");
  edit_edit_to_out_item_ = edit_menu_->AddItem("edittoout", this, SLOT(EditToOutTriggered()), "Ctrl+Alt+W");
  edit_menu_->addSeparator();
  MenuShared::instance()->AddItemsForInOutMenu(edit_menu_);
  edit_delete_inout_item_ = edit_menu_->AddItem("deleteinout", nullptr, nullptr, ";");
  edit_ripple_delete_inout_item_ = edit_menu_->AddItem("rippledeleteinout", nullptr, nullptr, "'");
  edit_menu_->addSeparator();
  edit_set_marker_item_ = edit_menu_->AddItem("marker", this, SLOT(SetMarkerTriggered()), "M");

  //
  // VIEW MENU
  //
  view_menu_ = new Menu(this, this, SLOT(ViewMenuAboutToShow()));
  view_zoom_in_item_ = view_menu_->AddItem("zoomin", this, SLOT(ZoomInTriggered()), "=");
  view_zoom_out_item_ = view_menu_->AddItem("zoomout", this, SLOT(ZoomOutTriggered()), "-");
  view_increase_track_height_item_ =
      view_menu_->AddItem("vzoomin", this, SLOT(IncreaseTrackHeightTriggered()), "Ctrl+=");
  view_decrease_track_height_item_ =
      view_menu_->AddItem("vzoomout", this, SLOT(DecreaseTrackHeightTriggered()), "Ctrl+-");
  view_show_all_item_ = view_menu_->AddItem("showall", nullptr, nullptr, "\\");
  view_show_all_item_->setCheckable(true);
  view_menu_->addSeparator();
  view_rectified_waveforms_item_ = view_menu_->AddItem("rectifiedwaveforms", nullptr, nullptr);
  view_rectified_waveforms_item_->setCheckable(true);
  view_menu_->addSeparator();

  frame_view_mode_group_ = new QActionGroup(this);

  view_timecode_view_dropframe_item_ = view_menu_->AddItem("modedropframe", this, SLOT(TimecodeDisplayTriggered()));
  view_timecode_view_dropframe_item_->setData(Timecode::kTimecodeDropFrame);
  view_timecode_view_dropframe_item_->setCheckable(true);
  frame_view_mode_group_->addAction(view_timecode_view_dropframe_item_);

  view_timecode_view_nondropframe_item_ =
      view_menu_->AddItem("modenondropframe", this, SLOT(TimecodeDisplayTriggered()));
  view_timecode_view_nondropframe_item_->setData(Timecode::kTimecodeNonDropFrame);
  view_timecode_view_nondropframe_item_->setCheckable(true);
  frame_view_mode_group_->addAction(view_timecode_view_nondropframe_item_);

  view_timecode_view_seconds_item_ = view_menu_->AddItem("modeseconds", this, SLOT(TimecodeDisplayTriggered()));
  view_timecode_view_seconds_item_->setData(Timecode::kTimecodeSeconds);
  view_timecode_view_seconds_item_->setCheckable(true);
  frame_view_mode_group_->addAction(view_timecode_view_seconds_item_);

  view_timecode_view_frames_item_ = view_menu_->AddItem("modeframes", this, SLOT(TimecodeDisplayTriggered()));
  view_timecode_view_frames_item_->setData(Timecode::kFrames);
  view_timecode_view_frames_item_->setCheckable(true);
  frame_view_mode_group_->addAction(view_timecode_view_frames_item_);

  view_timecode_view_milliseconds_item_ = view_menu_->AddItem("milliseconds", this, SLOT(TimecodeDisplayTriggered()));
  view_timecode_view_milliseconds_item_->setData(Timecode::kMilliseconds);
  view_timecode_view_milliseconds_item_->setCheckable(true);
  frame_view_mode_group_->addAction(view_timecode_view_milliseconds_item_);

  view_menu_->addSeparator();

  view_title_safe_area_menu_ = new Menu(view_menu_);

  QActionGroup* title_safe_group = new QActionGroup(this);

  title_safe_off_item_ = view_title_safe_area_menu_->AddItem("titlesafeoff", nullptr, nullptr);
  title_safe_off_item_->setCheckable(true);
  title_safe_off_item_->setData(qSNaN());
  title_safe_group->addAction(title_safe_off_item_);

  title_safe_default_item_ = view_title_safe_area_menu_->AddItem("titlesafedefault", nullptr, nullptr);
  title_safe_default_item_->setCheckable(true);
  title_safe_default_item_->setData(0.0);
  title_safe_group->addAction(title_safe_default_item_);

  title_safe_43_item_ = view_title_safe_area_menu_->AddItem("titlesafe43", nullptr, nullptr);
  title_safe_43_item_->setCheckable(true);
  title_safe_43_item_->setData(4.0 / 3.0);
  title_safe_group->addAction(title_safe_43_item_);

  title_safe_169_item_ = view_title_safe_area_menu_->AddItem("titlesafe169", nullptr, nullptr);
  title_safe_169_item_->setCheckable(true);
  title_safe_169_item_->setData(16.0 / 9.0);
  title_safe_group->addAction(title_safe_169_item_);

  title_safe_custom_item_ = view_title_safe_area_menu_->AddItem("titlesafecustom", nullptr, nullptr);
  title_safe_custom_item_->setCheckable(true);
  title_safe_custom_item_->setData(-1.0);
  title_safe_group->addAction(title_safe_custom_item_);

  view_menu_->addSeparator();

  view_full_screen_item_ = view_menu_->AddItem("fullscreen", parent, SLOT(SetFullscreen(bool)), "F11");
  view_full_screen_item_->setCheckable(true);

  view_full_screen_viewer_item_ = view_menu_->AddItem("fullscreenviewer", nullptr, nullptr);

  //
  // PLAYBACK MENU
  //
  playback_menu_ = new Menu(this);
  playback_gotostart_item_ = playback_menu_->AddItem("gotostart", this, SLOT(GoToStartTriggered()), "Home");
  playback_prevframe_item_ = playback_menu_->AddItem("prevframe", this, SLOT(PrevFrameTriggered()), "Left");
  playback_playpause_item_ = playback_menu_->AddItem("playpause", this, SLOT(PlayPauseTriggered()), "Space");
  playback_playinout_item_ = playback_menu_->AddItem("playintoout", nullptr, nullptr, "Shift+Space");
  playback_nextframe_item_ = playback_menu_->AddItem("nextframe", this, SLOT(NextFrameTriggered()), "Right");
  playback_gotoend_item_ = playback_menu_->AddItem("gotoend", this, SLOT(GoToEndTriggered()), "End");

  playback_menu_->addSeparator();

  playback_prevcut_item_ = playback_menu_->AddItem("prevcut", this, SLOT(GoToPrevCutTriggered()), "Up");
  playback_nextcut_item_ = playback_menu_->AddItem("nextcut", this, SLOT(GoToNextCutTriggered()), "Down");

  playback_menu_->addSeparator();

  playback_gotoin_item_ = playback_menu_->AddItem("gotoin", nullptr, nullptr, "Shift+I");
  playback_gotoout_item_ = playback_menu_->AddItem("gotoout", nullptr, nullptr, "Shift+O");

  playback_menu_->addSeparator();

  playback_shuttleleft_item_ = playback_menu_->AddItem("decspeed", this, SLOT(ShuttleLeftTriggered()), "J");
  playback_shuttlestop_item_ = playback_menu_->AddItem("pause", this, SLOT(ShuttleStopTriggered()), "K");
  playback_shuttleright_item_ = playback_menu_->AddItem("incspeed", this, SLOT(ShuttleRightTriggered()), "L");

  playback_menu_->addSeparator();

  playback_loop_item_ = playback_menu_->AddItem("loop", nullptr, nullptr);
  // Menu::SetBooleanAction(playback_loop_item_, &olive::config.loop);

  //
  // WINDOW MENU
  //
  window_menu_ = new Menu(this, this, SLOT(WindowMenuAboutToShow()));
  connect(window_menu_, SIGNAL(aboutToHide()), this, SLOT(WindowMenuAboutToHide()));
  window_menu_separator_ = window_menu_->addSeparator();
  window_maximize_panel_item_ = window_menu_->AddItem("maximizepanel", parent, SLOT(ToggleMaximizedPanel()), "`");
  window_lock_layout_item_ = window_menu_->AddItem("lockpanels", PanelManager::instance(), SLOT(SetPanelsLocked(bool)));
  window_lock_layout_item_->setCheckable(true);
  window_menu_->addSeparator();
  window_reset_layout_item_ =
      window_menu_->AddItem("resetdefaultlayout", Core::instance()->main_window(), SLOT(SetDefaultLayout()));

  //
  // TOOLS MENU
  //
  tools_menu_ = new Menu(this, this, SLOT(ToolsMenuAboutToShow()));
  tools_menu_->setToolTipsVisible(true);

  tools_group_ = new QActionGroup(this);

  tools_pointer_item_ = tools_menu_->AddItem("pointertool", this, SLOT(ToolItemTriggered()), "V");
  tools_pointer_item_->setCheckable(true);
  tools_pointer_item_->setData(Tool::kPointer);
  tools_group_->addAction(tools_pointer_item_);

  tools_edit_item_ = tools_menu_->AddItem("edittool", this, SLOT(ToolItemTriggered()), "X");
  tools_edit_item_->setCheckable(true);
  tools_edit_item_->setData(Tool::kEdit);
  tools_group_->addAction(tools_edit_item_);

  tools_ripple_item_ = tools_menu_->AddItem("rippletool", this, SLOT(ToolItemTriggered()), "B");
  tools_ripple_item_->setCheckable(true);
  tools_ripple_item_->setData(Tool::kRipple);
  tools_group_->addAction(tools_ripple_item_);

  tools_rolling_item_ = tools_menu_->AddItem("rollingtool", this, SLOT(ToolItemTriggered()), "N");
  tools_rolling_item_->setCheckable(true);
  tools_rolling_item_->setData(Tool::kRolling);
  tools_group_->addAction(tools_rolling_item_);

  tools_razor_item_ = tools_menu_->AddItem("razortool", this, SLOT(ToolItemTriggered()), "C");
  tools_razor_item_->setCheckable(true);
  tools_razor_item_->setData(Tool::kRazor);
  tools_group_->addAction(tools_razor_item_);

  tools_slip_item_ = tools_menu_->AddItem("sliptool", this, SLOT(ToolItemTriggered()), "Y");
  tools_slip_item_->setCheckable(true);
  tools_slip_item_->setData(Tool::kSlip);
  tools_group_->addAction(tools_slip_item_);

  tools_slide_item_ = tools_menu_->AddItem("slidetool", this, SLOT(ToolItemTriggered()), "U");
  tools_slide_item_->setCheckable(true);
  tools_slide_item_->setData(Tool::kSlide);
  tools_group_->addAction(tools_slide_item_);

  tools_hand_item_ = tools_menu_->AddItem("handtool", this, SLOT(ToolItemTriggered()), "H");
  tools_hand_item_->setCheckable(true);
  tools_hand_item_->setData(Tool::kHand);
  tools_group_->addAction(tools_hand_item_);

  tools_zoom_item_ = tools_menu_->AddItem("zoomtool", this, SLOT(ToolItemTriggered()), "Z");
  tools_zoom_item_->setCheckable(true);
  tools_zoom_item_->setData(Tool::kZoom);
  tools_group_->addAction(tools_zoom_item_);

  tools_transition_item_ = tools_menu_->AddItem("transitiontool", this, SLOT(ToolItemTriggered()), "T");
  tools_transition_item_->setCheckable(true);
  tools_transition_item_->setData(Tool::kTransition);
  tools_group_->addAction(tools_transition_item_);

  tools_menu_->addSeparator();

  tools_snapping_item_ = tools_menu_->AddItem("snapping", nullptr, nullptr, "S");
  tools_snapping_item_->setCheckable(true);
  connect(tools_snapping_item_, SIGNAL(triggered(bool)), Core::instance(), SLOT(SetSnapping(bool)));

  tools_menu_->addSeparator();

  tools_autocut_silence_item_ = tools_menu_->AddItem("autocutsilence", nullptr, nullptr);

  tools_menu_->addSeparator();

  tools_preferences_item_ = tools_menu_->AddItem("prefs", Core::instance(), SLOT(DialogPreferencesShow()), "Ctrl+,");

  //
  // HELP MENU
  //
  help_menu_ = new Menu(this);
  help_action_search_item_ = help_menu_->AddItem("actionsearch", this, SLOT(ActionSearchTriggered()), "/");
  help_menu_->addSeparator();
  help_debug_log_item_ = help_menu_->AddItem("debuglog", nullptr, nullptr);
  help_menu_->addSeparator();
  help_about_item_ = help_menu_->AddItem("about", Core::instance(), SLOT(DialogAboutShow()));

  Retranslate();
}

void MainMenu::changeEvent(QEvent* e) {
  if (e->type() == QEvent::LanguageChange) {
    Retranslate();
  }
  QMenuBar::changeEvent(e);
}

void MainMenu::ToolItemTriggered() {
  // Assume the sender is a QAction
  QAction* action = static_cast<QAction*>(sender());

  // Assume its data() is a member of Tool::Item
  Tool::Item tool = static_cast<Tool::Item>(action->data().toInt());

  // Set the Tool in Core
  Core::instance()->SetTool(tool);
}

void MainMenu::TimecodeDisplayTriggered() {
  // Assume the sender is a QAction
  QAction* action = static_cast<QAction*>(sender());

  // Assume its data() is a member of Timecode::Display
  Timecode::Display display = static_cast<Timecode::Display>(action->data().toInt());

  // Set the current display mode
  Timecode::SetCurrentDisplay(display);
}

void MainMenu::FileMenuAboutToShow() {
  file_project_properties_item_->setEnabled(Core::instance()->GetActiveProject() != nullptr);
}

void MainMenu::ViewMenuAboutToShow() {
  // Parent is QMainWindow
  view_full_screen_item_->setChecked(parentWidget()->isFullScreen());

  // Ensure checked timecode display mode is correct
  QList<QAction*> timecode_display_actions = frame_view_mode_group_->actions();
  foreach (QAction* a, timecode_display_actions) {
    if (a->data() == Timecode::CurrentDisplay()) {
      a->setChecked(true);
      break;
    }
  }
}

void MainMenu::ToolsMenuAboutToShow() {
  // Ensure checked Tool is correct
  QList<QAction*> tool_actions = tools_group_->actions();
  foreach (QAction* a, tool_actions) {
    if (a->data() == Core::instance()->tool()) {
      a->setChecked(true);
      break;
    }
  }

  // Ensure snapping value is correct
  tools_snapping_item_->setChecked(Core::instance()->snapping());
}

void MainMenu::WindowMenuAboutToShow() {
  // QMainWindow generates a perfectly usable menu for this purpose, we just need to copy it to the window menu
  QMenu* panel_menu = static_cast<QMainWindow*>(parentWidget())->createPopupMenu();
  QList<QAction*> panel_menu_actions = panel_menu->actions();

  // Make sure when we delete the panel_menu, it doesn't delete the actions
  foreach (QAction* panel_action, panel_menu_actions) { panel_action->setParent(window_menu_); }

  delete panel_menu;

  window_menu_->insertActions(window_menu_separator_, panel_menu_actions);

  window_lock_layout_item_->setChecked(PanelManager::instance()->ArePanelsLocked());
}

void MainMenu::WindowMenuAboutToHide() {
  while (window_menu_->actions().first() != window_menu_separator_) {
    window_menu_->removeAction(window_menu_->actions().first());
  }
}

void MainMenu::ZoomInTriggered() { PanelManager::instance()->CurrentlyFocused()->ZoomIn(); }

void MainMenu::ZoomOutTriggered() { PanelManager::instance()->CurrentlyFocused()->ZoomOut(); }

void MainMenu::IncreaseTrackHeightTriggered() { PanelManager::instance()->CurrentlyFocused()->IncreaseTrackHeight(); }

void MainMenu::DecreaseTrackHeightTriggered() { PanelManager::instance()->CurrentlyFocused()->DecreaseTrackHeight(); }

void MainMenu::GoToStartTriggered() { PanelManager::instance()->CurrentlyFocused()->GoToStart(); }

void MainMenu::PrevFrameTriggered() { PanelManager::instance()->CurrentlyFocused()->PrevFrame(); }

void MainMenu::PlayPauseTriggered() { PanelManager::instance()->CurrentlyFocused()->PlayPause(); }

void MainMenu::NextFrameTriggered() { PanelManager::instance()->CurrentlyFocused()->NextFrame(); }

void MainMenu::GoToEndTriggered() { PanelManager::instance()->CurrentlyFocused()->GoToEnd(); }

void MainMenu::SelectAllTriggered() { PanelManager::instance()->CurrentlyFocused()->SelectAll(); }

void MainMenu::DeselectAllTriggered() { PanelManager::instance()->CurrentlyFocused()->DeselectAll(); }

void MainMenu::InsertTriggered() { PanelManager::instance()->CurrentlyFocused()->Insert(); }

void MainMenu::OverwriteTriggered() { PanelManager::instance()->CurrentlyFocused()->Overwrite(); }

void MainMenu::RippleToInTriggered() { PanelManager::instance()->CurrentlyFocused()->RippleToIn(); }

void MainMenu::RippleToOutTriggered() { PanelManager::instance()->CurrentlyFocused()->RippleToOut(); }

void MainMenu::EditToInTriggered() { PanelManager::instance()->CurrentlyFocused()->EditToIn(); }

void MainMenu::EditToOutTriggered() { PanelManager::instance()->CurrentlyFocused()->EditToOut(); }

void MainMenu::ActionSearchTriggered() {
  ActionSearch as(parentWidget());
  as.SetMenuBar(this);
  as.exec();
}

void MainMenu::ShuttleLeftTriggered() { PanelManager::instance()->CurrentlyFocused()->ShuttleLeft(); }

void MainMenu::ShuttleStopTriggered() { PanelManager::instance()->CurrentlyFocused()->ShuttleStop(); }

void MainMenu::ShuttleRightTriggered() { PanelManager::instance()->CurrentlyFocused()->ShuttleRight(); }

void MainMenu::GoToPrevCutTriggered() { PanelManager::instance()->CurrentlyFocused()->GoToPrevCut(); }

void MainMenu::GoToNextCutTriggered() { PanelManager::instance()->CurrentlyFocused()->GoToNextCut(); }

void MainMenu::SetMarkerTriggered() { PanelManager::instance()->CurrentlyFocused()->SetMarker(); }

void MainMenu::Retranslate() {
  // MenuShared is not a QWidget and therefore does not receive a LanguageEvent, we use MainMenu's to update it
  MenuShared::instance()->Retranslate();

  // File menu
  file_menu_->setTitle(tr("&File"));
  file_new_menu_->setTitle(tr("&New"));
  file_open_item_->setText(tr("&Open Project"));
  file_open_recent_menu_->setTitle(tr("Open &Recent"));
  file_open_recent_clear_item_->setText(tr("&Clear Recent List"));
  file_save_item_->setText(tr("&Save Project"));
  file_save_as_item_->setText(tr("Save Project &As"));
  file_import_item_->setText(tr("&Import..."));
  file_export_item_->setText(tr("&Export..."));
  file_project_properties_item_->setText(tr("&Project Properties..."));
  file_exit_item_->setText(tr("E&xit"));

  // Edit menu
  edit_menu_->setTitle(tr("&Edit"));
  // edit_undo_item_->setText(tr("&Undo")); FIXME: Does Qt translate these automatically?
  // edit_redo_item_->setText(tr("Redo"));
  edit_insert_item_->setText(tr("Insert"));
  edit_overwrite_item_->setText(tr("Overwrite"));
  edit_select_all_item_->setText(tr("Select &All"));
  edit_deselect_all_item_->setText(tr("Deselect All"));
  edit_ripple_to_in_item_->setText(tr("Ripple to In Point"));
  edit_ripple_to_out_item_->setText(tr("Ripple to Out Point"));
  edit_edit_to_in_item_->setText(tr("Edit to In Point"));
  edit_edit_to_out_item_->setText(tr("Edit to Out Point"));
  edit_delete_inout_item_->setText(tr("Delete In/Out Point"));
  edit_ripple_delete_inout_item_->setText(tr("Ripple Delete In/Out Point"));
  edit_set_marker_item_->setText(tr("Set/Edit Marker"));

  // View menu
  view_menu_->setTitle(tr("&View"));
  view_zoom_in_item_->setText(tr("Zoom In"));
  view_zoom_out_item_->setText(tr("Zoom Out"));
  view_increase_track_height_item_->setText(tr("Increase Track Height"));
  view_decrease_track_height_item_->setText(tr("Decrease Track Height"));
  view_show_all_item_->setText(tr("Toggle Show All"));
  view_rectified_waveforms_item_->setText(tr("Rectified Waveforms"));
  view_timecode_view_frames_item_->setText(tr("Frames"));
  view_timecode_view_dropframe_item_->setText(tr("Drop Frame"));
  view_timecode_view_nondropframe_item_->setText(tr("Non-Drop Frame"));
  view_timecode_view_milliseconds_item_->setText(tr("Milliseconds"));
  view_timecode_view_seconds_item_->setText(tr("Seconds"));

  // View->Title/Action Safe Area Menu
  view_title_safe_area_menu_->setTitle(tr("Title/Action Safe Area"));
  title_safe_off_item_->setText(tr("Off"));
  title_safe_default_item_->setText(tr("Default"));
  title_safe_43_item_->setText(tr("4:3"));
  title_safe_169_item_->setText(tr("16:9"));
  title_safe_custom_item_->setText(tr("Custom"));

  // View menu (cont'd)
  view_full_screen_item_->setText(tr("Full Screen"));
  view_full_screen_viewer_item_->setText(tr("Full Screen Viewer"));

  // Playback menu
  playback_menu_->setTitle(tr("&Playback"));
  playback_gotostart_item_->setText(tr("Go to Start"));
  playback_prevframe_item_->setText(tr("Previous Frame"));
  playback_playpause_item_->setText(tr("Play/Pause"));
  playback_playinout_item_->setText(tr("Play In to Out"));
  playback_nextframe_item_->setText(tr("Next Frame"));
  playback_gotoend_item_->setText(tr("Go to End"));
  playback_prevcut_item_->setText(tr("Go to Previous Cut"));
  playback_nextcut_item_->setText(tr("Go to Next Cut"));
  playback_gotoin_item_->setText(tr("Go to In Point"));
  playback_gotoout_item_->setText(tr("Go to Out Point"));
  playback_shuttleleft_item_->setText(tr("Shuttle Left"));
  playback_shuttlestop_item_->setText(tr("Shuttle Stop"));
  playback_shuttleright_item_->setText(tr("Shuttle Right"));
  playback_loop_item_->setText(tr("Loop"));

  // Window menu
  window_menu_->setTitle("&Window");
  window_maximize_panel_item_->setText(tr("Maximize Panel"));
  window_lock_layout_item_->setText(tr("Lock Panels"));
  window_reset_layout_item_->setText(tr("Reset to Default Layout"));

  // Tools menu
  tools_menu_->setTitle(tr("&Tools"));
  tools_pointer_item_->setText(tr("Pointer Tool"));
  tools_edit_item_->setText(tr("Edit Tool"));
  tools_ripple_item_->setText(tr("Ripple Tool"));
  tools_rolling_item_->setText(tr("Rolling Tool"));
  tools_razor_item_->setText(tr("Razor Tool"));
  tools_slip_item_->setText(tr("Slip Tool"));
  tools_slide_item_->setText(tr("Slide Tool"));
  tools_hand_item_->setText(tr("Hand Tool"));
  tools_zoom_item_->setText(tr("Zoom Tool"));
  tools_transition_item_->setText(tr("Transition Tool"));
  tools_snapping_item_->setText(tr("Enable Snapping"));
  tools_autocut_silence_item_->setText(tr("Auto-Cut Silence"));
  tools_preferences_item_->setText(tr("Preferences"));

  // Help menu
  help_menu_->setTitle(tr("&Help"));
  help_action_search_item_->setText(tr("A&ction Search"));
  help_debug_log_item_->setText(tr("Debug Log"));
  help_about_item_->setText(tr("&About..."));
}
