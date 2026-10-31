#!/usr/bin/env python3
#
# Akousma
# Copyright 2026, Genki (genki_jp0) <genki_jp0@users.noreply.github.com>
#
# Akousma is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Akousma is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Akousma.  If not, see <http://www.gnu.org/licenses/>.

"""Builds Akousma's bundled icon sets from installed freedesktop icon themes.

Only the icons Akousma uses are taken (see data/iconsets/names.txt).  Each icon is
an original file of its theme, copied unchanged and only renamed to Akousma's icon
name: nothing is rendered, scaled, recoloured or drawn.  Icons a theme doesn't have
are left out; Akousma then uses its built-in icon for them.  Qt scales the icons
when they are shown, and SVG icons need the Qt SVG module.

On Debian/Ubuntu the themes come from adwaita-icon-theme, breeze-icon-theme,
papirus-icon-theme, tango-icon-theme, oxygen-icon-theme and elementary-icon-theme;
Clementine's icons come from a Clementine source checkout (--clementine).

Usage: dist/scripts/extract-iconsets.py [--clementine /path/to/Clementine] [--only id,id]
Writes data/iconsets/<id>/, data/iconsets/<id>.qrc and data/iconsets/SOURCES.txt.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

# The icon sizes taken from each theme. Larger ones only when a theme has nothing up to 64 pixels.
SIZES = (16, 22, 24, 32, 48, 64)

SETS = [
  {
    'id': 'adwaita', 'name': 'Adwaita', 'dir': '/usr/share/icons/Adwaita', 'package': 'adwaita-icon-theme',
    'licence': 'CC-BY-SA-3.0 or LGPL-3, and CC-BY-SA-4.0 (used under LGPL-3 and CC-BY-SA-4.0, both compatible with GPL-3)',
    'copyright': 'The GNOME Project',
    'source': 'https://gitlab.gnome.org/GNOME/adwaita-icon-theme',
  },
  {
    'id': 'breeze', 'name': 'Breeze', 'dir': '/usr/share/icons/breeze', 'package': 'breeze-icon-theme',
    'licence': 'LGPL-3+', 'copyright': 'The KDE Visual Design Group',
    'source': 'https://invent.kde.org/frameworks/breeze-icons',
  },
  {
    'id': 'breeze-dark', 'name': 'Breeze Dark', 'dir': '/usr/share/icons/breeze-dark', 'inherits': ['/usr/share/icons/breeze'], 'package': 'breeze-icon-theme',
    'licence': 'LGPL-3+', 'copyright': 'The KDE Visual Design Group',
    'source': 'https://invent.kde.org/frameworks/breeze-icons',
  },
  {
    'id': 'papirus', 'name': 'Papirus', 'dir': '/usr/share/icons/Papirus', 'package': 'papirus-icon-theme',
    'licence': 'GPL-3', 'copyright': 'Papirus Development Team',
    'source': 'https://github.com/PapirusDevelopmentTeam/papirus-icon-theme',
  },
  {
    'id': 'papirus-dark', 'name': 'Papirus Dark', 'dir': '/usr/share/icons/Papirus-Dark', 'inherits': ['/usr/share/icons/Papirus'], 'package': 'papirus-icon-theme',
    'licence': 'GPL-3', 'copyright': 'Papirus Development Team',
    'source': 'https://github.com/PapirusDevelopmentTeam/papirus-icon-theme',
  },
  {
    'id': 'tango', 'name': 'Tango', 'dir': '/usr/share/icons/Tango', 'package': 'tango-icon-theme',
    'licence': 'Public domain', 'copyright': 'The Tango Desktop Project',
    'source': 'http://tango.freedesktop.org/Tango_Icon_Library',
  },
  {
    'id': 'oxygen', 'name': 'Oxygen', 'dir': '/usr/share/icons/oxygen', 'package': 'oxygen-icon-theme',
    'licence': 'LGPL-3+', 'copyright': 'The Oxygen Team (David Vignoni, Nuno Pinheiro, David Miller, Johann Ollivier Lapeyre, Kenneth Wimer, Riccardo Iaconelli and others)',
    'source': 'https://invent.kde.org/frameworks/oxygen-icons',
  },
  {
    'id': 'elementary', 'name': 'elementary', 'dir': '/usr/share/icons/elementary', 'package': 'elementary-icon-theme',
    'licence': 'GPL-3+', 'copyright': 'elementary, Inc. and contributors',
    'source': 'https://github.com/elementary/icons',
  },
  {
    'id': 'clementine', 'name': 'Clementine', 'dir': None, 'package': None,
    'licence': 'GPL-3+ (cast, computer, speaker and tv: Apache-2.0, Material Symbols by Google)',
    'copyright': 'The Clementine authors',
    'source': 'https://github.com/clementine-player/Clementine (data/icons)',
  },
]

# radio-symbolic is a radio button in GNOME themes, not a radio.
NO_SYMBOLIC = {'radio'}

SIZE_DIR_RE = re.compile(r'^(\d+)(?:x(\d+))?(?:@(\d)x)?$')
SCALE_RE = re.compile(r'@(\d)x$')


def read_names(filename):
  names = []
  with open(filename, encoding='utf-8') as f:
    for line in f:
      line = line.strip()
      if not line or line.startswith('#'):
        continue
      name, _, rest = line.partition(':')
      candidates = rest.split()
      names.append((name.strip(), None if candidates == ['='] else (candidates or [name.strip()])))
  return names


def index_theme(theme_dir):
  """Maps icon name -> list of (pixel size or None for scalable, scale, symbolic, path)."""
  index = {}
  for root, dirs, files in os.walk(theme_dir, followlinks=True):
    rel = os.path.relpath(root, theme_dir)
    parts = [] if rel == '.' else rel.split(os.sep)
    if parts and parts[0] in ('cursors',):
      dirs[:] = []
      continue
    size = None
    scale = 1
    symbolic = False
    scalable = False
    for part in parts:
      m = SIZE_DIR_RE.match(part)
      if m:
        size = int(m.group(1))
        if m.group(3):
          scale = int(m.group(3))
        continue
      m = SCALE_RE.search(part)
      if m:
        scale = int(m.group(1))
      if part.startswith('symbolic'):
        symbolic = True
      if part in ('scalable', 'svg'):
        scalable = True
    for filename in files:
      stem, ext = os.path.splitext(filename)
      if ext not in ('.png', '.svg'):
        continue
      path = os.path.join(root, filename)
      if not os.path.exists(path):
        continue
      if size is None and not (scalable or symbolic):
        continue
      index.setdefault(stem, []).append({
        'size': None if (scalable or (symbolic and size is None)) else size,
        'scale': scale,
        'svg': ext == '.svg',
        'symbolic': symbolic or stem.endswith('-symbolic'),
        'path': path,
      })
  return index


def choose_files(entries):
  """The original files to take for one icon: one per size, PNG before SVG, no @2x variants."""
  entries = [e for e in entries if e['scale'] == 1]
  chosen = {}
  for e in sorted(entries, key=lambda e: e['svg']):
    if e['size'] in SIZES and e['size'] not in chosen:
      chosen[e['size']] = e
  if not chosen:
    larger = sorted([e for e in entries if e['size'] is not None], key=lambda e: (e['size'], e['svg']))
    if larger:
      chosen[larger[0]['size']] = larger[0]
  # The theme's scalable SVG too when its fixed sizes stop below 48 pixels, so large icons stay sharp.
  if not chosen or max(chosen) < 48:
    scalable = sorted([e for e in entries if e['size'] is None], key=lambda e: not e['svg'])
    if scalable:
      chosen['scalable'] = scalable[0]
  return chosen


def build_set(iconset, names, out_dir):
  indexes = [index_theme(d) for d in [iconset['dir']] + iconset.get('inherits', [])]
  set_dir = os.path.join(out_dir, iconset['id'])
  if os.path.isdir(set_dir):
    shutil.rmtree(set_dir)
  covered = {}
  themable = [(n, c) for n, c in names if c is not None]
  for name, candidates in themable:
    source_name = None
    entries = None
    # Real (coloured) icons first, symbolic ones only when the theme has nothing else. A dark variant first, then the
    # theme it is based on.
    for want_symbolic in (False, True):
      for candidate in candidates:
        if want_symbolic and candidate in NO_SYMBOLIC:
          continue
        for index in indexes:
          if want_symbolic:
            found = index.get(candidate + '-symbolic', []) + [e for e in index.get(candidate, []) if e['symbolic']]
          else:
            found = [e for e in index.get(candidate, []) if not e['symbolic']]
          if found:
            source_name, entries = candidate + ('-symbolic' if want_symbolic and not candidate.endswith('-symbolic') else ''), found
            break
        if entries:
          break
      if entries:
        break
    if not entries:
      continue
    chosen = choose_files(entries)
    if not chosen:
      continue
    for size, entry in chosen.items():
      size_dir = os.path.join(set_dir, 'scalable' if size == 'scalable' else str(size))
      os.makedirs(size_dir, exist_ok=True)
      shutil.copyfile(entry['path'], os.path.join(size_dir, name + ('.svg' if entry['svg'] else '.png')))
    covered[name] = source_name
  return covered, len(themable)


def write_ini(iconset, covered, out_dir, version):
  with open(os.path.join(out_dir, iconset['id'], 'iconset.ini'), 'w', encoding='utf-8') as f:
    f.write('[IconSet]\n')
    f.write(f'name={iconset["name"]}\n')
    f.write(f'version={version}\n')
    f.write(f'licence={iconset["licence"]}\n')
    f.write(f'copyright={iconset["copyright"]}\n')
    f.write(f'source={iconset["source"]}\n')
    f.write('\n[Sources]\n')
    for name in sorted(covered):
      f.write(f'{name}={covered[name]}\n')


def write_qrc(iconset, out_dir):
  set_dir = os.path.join(out_dir, iconset['id'])
  files = []
  for root, _, filenames in os.walk(set_dir):
    for filename in filenames:
      files.append(os.path.relpath(os.path.join(root, filename), set_dir).replace(os.sep, '/'))
  with open(os.path.join(out_dir, iconset['id'] + '.qrc'), 'w', encoding='utf-8') as f:
    f.write('<!DOCTYPE RCC><RCC version="1.0">\n')
    f.write(f'<qresource prefix="/iconsets/{iconset["id"]}">\n')
    for filename in sorted(files):
      f.write(f'  <file alias="{filename}">{iconset["id"]}/{filename}</file>\n')
    f.write('</qresource>\n</RCC>\n')


def directory_size(path):
  return sum(os.path.getsize(os.path.join(root, f)) for root, _, files in os.walk(path) for f in files)


def package_version(package):
  try:
    return subprocess.run(['dpkg-query', '-W', '-f', '${Version}', package], check=True, capture_output=True, text=True).stdout.strip()
  except (OSError, subprocess.CalledProcessError):
    return 'unknown'


def main():
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument('--clementine', help='Clementine source checkout, for the Clementine icon set')
  parser.add_argument('--only', help='Comma separated set ids to rebuild')
  args = parser.parse_args()

  top = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
  out_dir = os.path.join(top, 'data', 'iconsets')
  names = read_names(os.path.join(out_dir, 'names.txt'))
  only = set(args.only.split(',')) if args.only else None

  report = []
  for iconset in SETS:
    if only and iconset['id'] not in only:
      continue
    if iconset['id'] == 'clementine':
      if not args.clementine:
        print('Skipping clementine: no --clementine checkout given', file=sys.stderr)
        continue
      iconset = dict(iconset, dir=os.path.join(args.clementine, 'data', 'icons'))
      version = subprocess.run(['git', '-C', args.clementine, 'log', '-1', '--format=%h'], capture_output=True, text=True).stdout.strip() or 'unknown'
      version = 'git ' + version
    else:
      version = package_version(iconset['package'])
    if not os.path.isdir(iconset['dir']):
      print(f'Skipping {iconset["id"]}: {iconset["dir"]} not found', file=sys.stderr)
      continue
    covered, total = build_set(iconset, names, out_dir)
    write_ini(iconset, covered, out_dir, version)
    write_qrc(iconset, out_dir)
    size = directory_size(os.path.join(out_dir, iconset['id']))
    line = f'{iconset["name"]:<12} {len(covered):>3} of {total} icons  {size / 1024:7.1f} KiB  {iconset["licence"]}'
    print(line)
    report.append((iconset, version, len(covered), total, size, sorted(set(n for n, c in names if c is not None) - set(covered))))

  if not only:
    with open(os.path.join(out_dir, 'SOURCES.txt'), 'w', encoding='utf-8') as f:
      f.write('Akousma bundled icon sets\n=========================\n\n')
      f.write('Generated by dist/scripts/extract-iconsets.py.  Only the icons Akousma uses are\n')
      f.write('included: original files of each theme, unchanged, renamed to Akousma\'s icon names\n')
      f.write('(data/iconsets/names.txt).  Missing icons fall back to the built-in set.\n')
      f.write(f'{sum(1 for _, c in names if c is None)} logos and ratings always use the built-in icons.\n\n')
      for iconset, version, count, total, size, missing in report:
        f.write(f'{iconset["name"]} ({iconset["id"]})\n')
        f.write(f'  Version:   {version}\n')
        f.write(f'  Source:    {iconset["source"]}\n')
        f.write(f'  Copyright: {iconset["copyright"]}\n')
        f.write(f'  Licence:   {iconset["licence"]}\n')
        f.write(f'  Coverage:  {count} of {total} icons, {size / 1024:.0f} KiB of icon files\n')
        f.write(f'  Built-in:  {", ".join(missing) if missing else "none"}\n\n')


if __name__ == '__main__':
  main()
