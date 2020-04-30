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

#include "preferences.h"

#include <QDialogButtonBox>
#include <QListWidget>
#include <QSplitter>
#include <QVBoxLayout>

#include "config/config.h"
#include "tabs/preferencesappearancetab.h"
#include "tabs/preferencesaudiotab.h"
#include "tabs/preferencesbehaviortab.h"
#include "tabs/preferencesdisktab.h"
#include "tabs/preferencesgeneraltab.h"
#include "tabs/preferenceskeyboardtab.h"
#include "tabs/preferencesqualitytab.h"

OLIVE_NAMESPACE_ENTER

PreferencesDialog::PreferencesDialog(QWidget* parent, QMenuBar* main_menu_bar) : QDialog(parent) {
  setWindowTitle(tr("Preferences"));

  QVBoxLayout* layout = new QVBoxLayout(this);

  QSplitter* splitter = new QSplitter();
  splitter->setChildrenCollapsible(false);
  layout->addWidget(splitter);

  list_widget_ = new QListWidget();

  preference_pane_stack_ = new QStackedWidget(this);

  AddTab(new PreferencesGeneralTab(), tr("General"));
  AddTab(new PreferencesAppearanceTab(), tr("Appearance"));
  AddTab(new PreferencesBehaviorTab(), tr("Behavior"));
  AddTab(new PreferencesQualityTab(), tr("Quality"));
  AddTab(new PreferencesDiskTab(), tr("Disk"));
  AddTab(new PreferencesAudioTab(), tr("Audio"));
  AddTab(new PreferencesKeyboardTab(main_menu_bar), tr("Keyboard"));

  splitter->addWidget(list_widget_);
  splitter->addWidget(preference_pane_stack_);

  QDialogButtonBox* buttonBox = new QDialogButtonBox(this);
  buttonBox->setOrientation(Qt::Horizontal);
  buttonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

  layout->addWidget(buttonBox);

  connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
  connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));

  connect(list_widget_, SIGNAL(currentRowChanged(int)), preference_pane_stack_, SLOT(setCurrentIndex(int)));
}

void PreferencesDialog::accept() {
  foreach (PreferencesTab* tab, tabs_) {
    if (!tab->Validate()) {
      return;
    }
  }

  foreach (PreferencesTab* tab, tabs_) { tab->Accept(); }

  QDialog::accept();
}

void PreferencesDialog::AddTab(PreferencesTab* tab, const QString& title) {
  list_widget_->addItem(title);
  preference_pane_stack_->addWidget(tab);

  tabs_.append(tab);
}

OLIVE_NAMESPACE_EXIT
