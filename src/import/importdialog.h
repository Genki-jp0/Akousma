/*
 * Akousma
 * Copyright 2026, Genki (genki_jp0) <genki_jp0@users.noreply.github.com>
 *
 * Akousma is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Akousma is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Akousma.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef IMPORTDIALOG_H
#define IMPORTDIALOG_H

#include <QtGlobal>
#include <QDialog>
#include <QList>
#include <QString>

#include "playerimporter.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QPushButton;
class CommandlineOptions;

// Lets the user pick a Strawberry or Clementine installation and what to import from it.
class ImportDialog : public QDialog {
  Q_OBJECT

 public:
  enum class Mode {
    Running,   // Opened from the menu: the import is stored and done when Akousma starts again.
    Startup,   // Requested with --import-from: the import is done right away, before the collection is opened.
    FirstRun   // Offered on the first start: like Startup, but the user can skip it.
  };

  explicit ImportDialog(const Mode mode, QWidget *parent = nullptr);

  bool HasInstallations() const { return !installations_.isEmpty(); }
  void SelectPlayer(const PlayerImporter::Player player);
  void SelectItems(const PlayerImporter::Items items);

  // After exec() in Running mode: whether the user wants Akousma to restart now for the import.
  bool restart_now() const { return restart_now_; }
  // After exec() in Startup and FirstRun mode: what was imported.
  PlayerImporter::Items imported() const { return imported_; }

  // Called by main() at startup, before the Application is created.
  // Runs a pending import, an import requested with --import-from, or offers the import on the first start.
  // Returns true when Akousma should restart, so imported settings like the style and language take effect.
  static bool ImportAtStartup(const CommandlineOptions &options, const bool first_start);

  // Shows the result of an import to the user.
  static void ShowResult(QWidget *parent, const PlayerImporter::Installation &installation, const PlayerImporter::Result &result);

 private Q_SLOTS:
  void SourceChanged(const int index);
  void ItemToggled();
  void ImportClicked();

 private:
  const PlayerImporter::Installation *CurrentInstallation() const;
  PlayerImporter::Items SelectedItems() const;

  Mode mode_;
  QList<PlayerImporter::Installation> installations_;

  QComboBox *combobox_source_;
  QLabel *label_details_;
  QCheckBox *checkbox_settings_;
  QCheckBox *checkbox_collection_;
  QCheckBox *checkbox_covers_;
  QCheckBox *checkbox_passwords_;
  QLabel *label_passwords_;
  QLabel *label_warning_;
  QDialogButtonBox *buttonbox_;
  QPushButton *button_import_;

  bool restart_now_;
  PlayerImporter::Items imported_;
};

#endif  // IMPORTDIALOG_H
