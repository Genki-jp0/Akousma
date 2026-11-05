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

#include "config.h"

#include "gtest_include.h"

#include <getopt.h>

#include <QByteArray>
#include <QList>

#include "core/commandlineoptions.h"

namespace {

// Holds the arguments for CommandlineOptions, which keeps pointers to them. On Windows it reads the process command line instead, these tests are not built there.
class Arguments {
 public:
  explicit Arguments(const QList<QByteArray> &args) : args_(args) {
    args_.prepend("akousma");
    for (QByteArray &arg : args_) argv_ << arg.data();
    argv_ << nullptr;
  }
  int argc() const { return static_cast<int>(args_.count()); }
  char **argv() { return argv_.data(); }

 private:
  QList<QByteArray> args_;
  QList<char*> argv_;
};

bool Parse(Arguments *args, CommandlineOptions *options) {
  // Every test parses a new command line, start getopt_long over (glibc and the BSDs do on 0).
  optind = 0;
  *options = CommandlineOptions(args->argc(), args->argv());
  return options->Parse();
}

TEST(CommandlineOptionsTest, SkipImportPromptIsOffByDefault) {

  Arguments args({});
  CommandlineOptions options;
  ASSERT_TRUE(Parse(&args, &options));
  EXPECT_FALSE(options.skip_import_prompt());

}

// Used by the Windows installer when the import from Strawberry or Clementine was declined on its finish page.
TEST(CommandlineOptionsTest, SkipImportPrompt) {

  Arguments args({"--skip-import-prompt"});
  CommandlineOptions options;
  ASSERT_TRUE(Parse(&args, &options));
  EXPECT_TRUE(options.skip_import_prompt());
  EXPECT_TRUE(options.import_from().isEmpty());
  EXPECT_FALSE(options.import_quiet());

}

// The option is handled before a running instance is looked for, it is not sent to it.
TEST(CommandlineOptionsTest, SkipImportPromptIsNotSentToTheRunningInstance) {

  Arguments args({"--skip-import-prompt", "--play"});
  CommandlineOptions options;
  ASSERT_TRUE(Parse(&args, &options));
  ASSERT_TRUE(options.skip_import_prompt());
  EXPECT_EQ(options.player_action(), CommandlineOptions::PlayerAction::Play);

  CommandlineOptions received;
  received.Load(options.Serialize());
  EXPECT_FALSE(received.skip_import_prompt());
  EXPECT_EQ(received.player_action(), CommandlineOptions::PlayerAction::Play);

}

}  // namespace
