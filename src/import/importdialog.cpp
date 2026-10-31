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

#include <optional>

#include <QtGlobal>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFont>
#include <QGroupBox>
#include <QIcon>
#include <QGuiApplication>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include "core/iconloader.h"
#include "core/logging.h"
#include "core/commandlineoptions.h"
#include "importdialog.h"

using namespace Qt::Literals::StringLiterals;

ImportDialog::ImportDialog(const Mode mode, QWidget *parent)
    : QDialog(parent),
      mode_(mode),
      installations_(PlayerImporter::Detect()),
      combobox_source_(new QComboBox(this)),
      label_details_(new QLabel(this)),
      checkbox_settings_(new QCheckBox(tr("&Settings"), this)),
      checkbox_collection_(new QCheckBox(tr("&Collection (songs, play counts, ratings and playlists)"), this)),
      checkbox_covers_(new QCheckBox(tr("&Album covers cache"), this)),
      checkbox_passwords_(new QCheckBox(tr("Saved &passwords and logins"), this)),
      label_passwords_(new QLabel(tr("Copies service passwords and login tokens to Akousma's keyring. Off: log in again yourself."), this)),
      label_warning_(new QLabel(this)),
      buttonbox_(new QDialogButtonBox(this)),
      button_import_(nullptr),
      restart_now_(false) {

  setWindowTitle(tr("Import from Strawberry / Clementine"));
  setWindowIcon(IconLoader::Load(u"akousma"_s));
  setMinimumWidth(560);

  QVBoxLayout *layout = new QVBoxLayout(this);

  QLabel *label_intro = new QLabel(this);
  label_intro->setWordWrap(true);
  if (mode_ == Mode::FirstRun) {
    label_intro->setText(tr("Welcome to Akousma. Akousma found the settings and music collection of another music player on this computer. Do you want to import them?"));
  }
  else {
    label_intro->setText(tr("Akousma can copy the settings, the music collection and the album covers of Strawberry or Clementine on this computer."));
  }
  layout->addWidget(label_intro);

  QLabel *label_source = new QLabel(tr("Import &from:"), this);
  label_source->setBuddy(combobox_source_);
  layout->addWidget(label_source);
  layout->addWidget(combobox_source_);

  label_details_->setWordWrap(true);
  label_details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label_details_->setContentsMargins(12, 0, 0, 0);
  layout->addWidget(label_details_);

  QGroupBox *groupbox_items = new QGroupBox(tr("Import"), this);
  QVBoxLayout *layout_items = new QVBoxLayout(groupbox_items);
  layout_items->addWidget(checkbox_settings_);
  layout_items->addWidget(checkbox_collection_);
  layout_items->addWidget(checkbox_covers_);
  layout_items->addWidget(checkbox_passwords_);
  // Indented below its checkbox, like a description.
  label_passwords_->setWordWrap(true);
  label_passwords_->setContentsMargins(checkbox_passwords_->style()->pixelMetric(QStyle::PM_IndicatorWidth) + checkbox_passwords_->style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing), 0, 0, 0);
  QFont font_passwords = label_passwords_->font();
  font_passwords.setPointSizeF(font_passwords.pointSizeF() * 0.9);
  label_passwords_->setFont(font_passwords);
  layout_items->addWidget(label_passwords_);
  layout->addWidget(groupbox_items);

  label_warning_->setWordWrap(true);
  layout->addWidget(label_warning_);

  layout->addStretch();
  layout->addWidget(buttonbox_);

  button_import_ = buttonbox_->addButton(tr("&Import"), QDialogButtonBox::AcceptRole);
  button_import_->setIcon(IconLoader::Load(u"document-download"_s));
  if (mode_ == Mode::FirstRun) {
    buttonbox_->addButton(tr("&Not now"), QDialogButtonBox::RejectRole);
  }
  else {
    buttonbox_->addButton(QDialogButtonBox::Cancel);
  }

  for (const PlayerImporter::Installation &installation : std::as_const(installations_)) {
    // The icons of the other players come from the icon theme, when they are installed.
    combobox_source_->addItem(QIcon::fromTheme(installation.player == PlayerImporter::Player::Strawberry ? u"strawberry"_s : u"clementine"_s), installation.name);
  }

  if (installations_.isEmpty()) {
    combobox_source_->addItem(tr("No Strawberry or Clementine installation was found"));
    combobox_source_->setEnabled(false);
    groupbox_items->setEnabled(false);
    button_import_->setEnabled(false);
  }

  if (PlayerImporter::AkousmaHasData()) {
    label_warning_->setText(tr("The selected parts replace Akousma's current settings and collection. They are backed up to %1 first.").arg(QDir::toNativeSeparators(PlayerImporter::AkousmaDataDir() + u"/import-backup-*"_s)));
  }
  if (mode_ == Mode::Running) {
    const QString restart_text = tr("Akousma restarts to do the import.");
    label_warning_->setText(label_warning_->text().isEmpty() ? restart_text : label_warning_->text() + u' ' + restart_text);
  }
  label_warning_->setVisible(!label_warning_->text().isEmpty());

  QObject::connect(combobox_source_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImportDialog::SourceChanged);
  QObject::connect(checkbox_settings_, &QCheckBox::toggled, this, &ImportDialog::ItemToggled);
  QObject::connect(checkbox_collection_, &QCheckBox::toggled, this, &ImportDialog::ItemToggled);
  QObject::connect(checkbox_covers_, &QCheckBox::toggled, this, &ImportDialog::ItemToggled);
  QObject::connect(checkbox_passwords_, &QCheckBox::toggled, this, &ImportDialog::ItemToggled);
  QObject::connect(button_import_, &QPushButton::clicked, this, &ImportDialog::ImportClicked);
  QObject::connect(buttonbox_, &QDialogButtonBox::rejected, this, &ImportDialog::reject);

  SourceChanged(combobox_source_->currentIndex());

}

const PlayerImporter::Installation *ImportDialog::CurrentInstallation() const {

  const int index = combobox_source_->currentIndex();
  if (index < 0 || index >= installations_.count()) return nullptr;
  return &installations_.at(index);

}

void ImportDialog::SelectPlayer(const PlayerImporter::Player player) {

  for (int i = 0; i < installations_.count(); ++i) {
    if (installations_.at(i).player == player) {
      combobox_source_->setCurrentIndex(i);
      return;
    }
  }

}

void ImportDialog::SelectItems(const PlayerImporter::Items items) {

  checkbox_settings_->setChecked(checkbox_settings_->isEnabled() && items & PlayerImporter::Item::Settings);
  checkbox_collection_->setChecked(checkbox_collection_->isEnabled() && items & PlayerImporter::Item::Collection);
  checkbox_covers_->setChecked(checkbox_covers_->isEnabled() && items & PlayerImporter::Item::Covers);
  checkbox_passwords_->setChecked(checkbox_passwords_->isEnabled() && items & PlayerImporter::Item::Passwords);

}

PlayerImporter::Items ImportDialog::SelectedItems() const {

  PlayerImporter::Items items;
  if (checkbox_settings_->isEnabled() && checkbox_settings_->isChecked()) items |= PlayerImporter::Item::Settings;
  if (checkbox_collection_->isEnabled() && checkbox_collection_->isChecked()) items |= PlayerImporter::Item::Collection;
  if (checkbox_covers_->isEnabled() && checkbox_covers_->isChecked()) items |= PlayerImporter::Item::Covers;
  if (checkbox_passwords_->isEnabled() && checkbox_passwords_->isChecked()) items |= PlayerImporter::Item::Passwords;
  return items;

}

void ImportDialog::SourceChanged(const int index) {

  Q_UNUSED(index)

  const PlayerImporter::Installation *installation = CurrentInstallation();
  if (!installation) {
    label_details_->clear();
    return;
  }

  QStringList details;
  details << tr("Settings: %1").arg(installation->has_settings ? installation->settings_location : tr("not found"));
  details << tr("Collection: %1").arg(installation->has_database() ? QDir::toNativeSeparators(installation->database_file) : tr("not found"));
  QStringList cover_dirs;
  for (const QString &dir : installation->cover_dirs) cover_dirs << QDir::toNativeSeparators(dir);
  details << tr("Album covers: %1").arg(installation->has_covers() ? cover_dirs.join(u", "_s) : tr("not found"));
  label_details_->setText(u"<small>"_s + details.join(u"<br />"_s).toHtmlEscaped().replace(u"&lt;br /&gt;"_s, u"<br />"_s) + u"</small>"_s);

  checkbox_settings_->setEnabled(installation->has_settings);
  checkbox_collection_->setEnabled(installation->has_database());
  checkbox_covers_->setEnabled(installation->has_covers());
  checkbox_passwords_->setEnabled(installation->available_items() & PlayerImporter::Item::Passwords);
  label_passwords_->setEnabled(checkbox_passwords_->isEnabled());
  checkbox_collection_->setText(installation->player == PlayerImporter::Player::Clementine ? tr("&Library (songs, play counts, ratings and playlists)") : tr("&Collection (songs, play counts, ratings and playlists)"));
  // Passwords are only imported when the user asks for them.
  SelectItems(PlayerImporter::Item::All | (checkbox_passwords_->isChecked() ? PlayerImporter::Items(PlayerImporter::Item::Passwords) : PlayerImporter::Items()));

  ItemToggled();

}

void ImportDialog::ItemToggled() {
  button_import_->setEnabled(CurrentInstallation() && SelectedItems() != PlayerImporter::Item::None);
}

void ImportDialog::ImportClicked() {

  const PlayerImporter::Installation *installation = CurrentInstallation();
  const PlayerImporter::Items items = SelectedItems();
  if (!installation || items == PlayerImporter::Item::None) return;

  if (mode_ == Mode::Running) {
    // Akousma has the collection open while it runs, so the import is done when it starts again.
    PlayerImporter::SetPendingImport(*installation, items);
    QMessageBox box(QMessageBox::Question, windowTitle(), tr("Akousma has to restart to import from %1. Restart Akousma now?").arg(installation->name), QMessageBox::NoButton, this);
    QPushButton *button_restart = box.addButton(tr("&Restart now"), QMessageBox::AcceptRole);
    box.addButton(tr("&Later"), QMessageBox::RejectRole);
    box.setDefaultButton(button_restart);
    box.exec();
    restart_now_ = box.clickedButton() == button_restart;
    if (!restart_now_) {
      QMessageBox::information(this, windowTitle(), tr("The import from %1 is done the next time Akousma starts.").arg(installation->name));
    }
    accept();
    return;
  }

  setEnabled(false);
  QGuiApplication::setOverrideCursor(Qt::WaitCursor);
  QApplication::processEvents();
  const PlayerImporter::Result result = PlayerImporter::Import(*installation, items);
  QGuiApplication::restoreOverrideCursor();
  setEnabled(true);

  imported_ = result.imported;
  ShowResult(this, *installation, result);
  accept();

}

void ImportDialog::ShowResult(QWidget *parent, const PlayerImporter::Installation &installation, const PlayerImporter::Result &result) {

  QString text;
  if (result.success) {
    text = tr("The import from %1 is finished.").arg(installation.name);
  }
  else {
    text = tr("The import from %1 did not finish.").arg(installation.name);
  }

  // Sections: errors, what was imported, what was skipped, what was dropped from smart playlists.
  QString details;
  const auto add_section = [&details](const QString &title, const QStringList &lines) {
    if (lines.isEmpty()) return;
    details += u"<p><b>"_s + title.toHtmlEscaped() + u"</b></p><ul style=\"margin-left: 12px; -qt-list-indent: 0;\">"_s;
    for (const QString &line : lines) {
      details += u"<li>"_s + line.toHtmlEscaped() + u"</li>"_s;
    }
    details += u"</ul>"_s;
  };
  add_section(tr("Errors"), result.errors);
  add_section(tr("Imported"), result.messages);
  add_section(tr("Skipped"), result.skipped);
  add_section(tr("Dropped from smart playlists"), result.dropped);

  QMessageBox box(result.success ? QMessageBox::Information : QMessageBox::Warning, tr("Import from Strawberry / Clementine"), text, QMessageBox::Ok, parent);
  box.setTextFormat(Qt::PlainText);
  box.setInformativeText(details);
  box.exec();

}

bool ImportDialog::ImportAtStartup(const CommandlineOptions &options, const bool first_start) {

  // An import chosen in the running application before it restarted.
  PlayerImporter::Items items;
  const std::optional<PlayerImporter::Installation> pending = PlayerImporter::TakePendingImport(&items);
  if (pending.has_value()) {
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const PlayerImporter::Result result = PlayerImporter::Import(pending.value(), items);
    QGuiApplication::restoreOverrideCursor();
    ShowResult(nullptr, pending.value(), result);
    return result.imported & PlayerImporter::Item::Settings;
  }

  // Requested on the command line.
  if (!options.import_from().isEmpty()) {
    ImportDialog dialog(Mode::Startup);
    if (options.import_from().compare("auto"_L1, Qt::CaseInsensitive) != 0) {
      const std::optional<PlayerImporter::Player> player = PlayerImporter::PlayerFromString(options.import_from());
      if (player.has_value()) dialog.SelectPlayer(player.value());
    }
    PlayerImporter::Items requested_items = PlayerImporter::Item::All;
    if (!options.import_items().isEmpty()) {
      bool ok = false;
      requested_items = PlayerImporter::ItemsFromString(options.import_items(), &ok);
      if (!ok) requested_items = PlayerImporter::Item::All;
    }
    if (options.import_passwords()) requested_items |= PlayerImporter::Item::Passwords;
    dialog.SelectItems(requested_items);
    dialog.exec();
    PlayerImporter::SetFirstRunPromptDone();
    return dialog.imported() & PlayerImporter::Item::Settings;
  }

  // Offered once, on the first start, when Strawberry or Clementine is found.
  if (first_start && !PlayerImporter::FirstRunPromptDone()) {
    ImportDialog dialog(Mode::FirstRun);
    if (dialog.HasInstallations()) {
      dialog.exec();
      PlayerImporter::SetFirstRunPromptDone();
      return dialog.imported() & PlayerImporter::Item::Settings;
    }
  }

  return false;

}
