# Akousma app icon

Akousma's app icon is a pomegranate wedge.
It comes in one style per Windows design language, each as an SVG source, a multi-size Windows `.ico` (16, 24, 32, 48, 64, 128 and 256 px) and a 512 px PNG.

| Style | Setting value | Files |
| --- | --- | --- |
| **Fluent (Windows 11)**, the default | `win11` | `akousma-win11.svg`, `akousma-win11.ico`, `akousma-win11-512.png` |
| MDL2 (Windows 10) | `win10` | `akousma-win10.svg`, `akousma-win10.ico`, `akousma-win10-512.png` |
| Modern UI (Windows 8.1) | `win81` | `akousma-win81-desktop.svg`, `akousma-win81-desktop.ico`, `akousma-win81-desktop-512.png` |
| Metro (Windows 8) | `win8` | `akousma-win8-tile.svg`, `akousma-win8-tile.ico`, `akousma-win8-tile-512.png` |
| Aero (Windows Vista / 7) | `aero` | `akousma-aero.svg`, `akousma-aero.ico`, `akousma-aero-512.png`, plus `akousma-aero-32.svg` and `akousma-aero-16.svg` drawn for small sizes (they are the 32 and 16 px images of the `.ico`) |

Each bundled icon set (`data/iconsets/`) also has the pomegranate drawn in its own style, see [In the style of the icon sets](#in-the-style-of-the-icon-sets).

## Choosing the style

Settings > Appearance > Application icon chooses the style; the setting is `Appearance/app_icon` with the values above, and an unknown value means the default.
Below it, "Use the icon set's Akousma icon" (`Appearance/app_icon_from_iconset`, off by default) uses the pomegranate of the chosen icon set instead; the dropdown is disabled then.
With an icon set that has none (the built-in icons, other icon themes) the style of the dropdown is used, and the dropdown stays enabled.
It changes when the settings are applied, without a restart (`src/core/appicon.cpp`), also after choosing another icon set (whose other icons need the restart), and while following the desktop's icon theme when that theme changes:

- the window icons and the taskbar button (Windows and X11; on macOS the Dock icon),
- the system tray icon, also its grey part that shows the progress, made from the colored icon,
- the About dialog, the Context tab icon and the picture the context view shows when nothing plays,
- the faded default playlist background.

The `.ico` and the 512 px PNG of each style are compiled in (`data/icons.qrc`), not the SVGs: Qt SVG can't draw them right (it leaves out the clip paths and filters).
The icons are the `.ico`'s images; they are PNG images, read without Qt's ICO plugin (an `.ico` with BMP images is read with the plugin).
The pictures larger than 256 px (context view, playlist background) are the 512 px PNG, scaled.
The grey icons are made at runtime, desaturated like `convert -colorspace Gray`.
After changing a style's SVG, render its `.ico` (the Aero one's 16 and 32 px images from `akousma-aero-16.svg` and `akousma-aero-32.svg`) and its 512 px PNG again:

```sh
svg=data/icons/akousma/akousma-win10.svg
rsvg-convert -w 512 -h 512 $svg -o ${svg%.svg}-512.png
```

What stays the default (Fluent) icon, because it is part of the installed files and can't change while Akousma runs:

- Windows: the icon of `akousma.exe` (Explorer, Start menu, pinned shortcuts and the installer), from `dist/windows/akousma.ico`, a copy of `akousma-win11.ico`.
  The other styles are in the executable too (`dist/windows/windres.rc.in`), so a shortcut's icon can be changed to one of them in its properties.
- Linux: the icon of the desktop menu entry and of the notifications, from the hicolor icons in `dist/unix/hicolor/`. On Wayland the window icons are the desktop entry's icon too.
- macOS: the app bundle icon, `dist/macos/akousma.icns`, shown in the Finder and the Dock while Akousma isn't running.

## The installed default icon

These files are rendered from `akousma-win11.svg`:

- `dist/windows/akousma.ico`: the executable's icon (`windres.rc.in`) and the installer's (`akousma.nsi.in`), a copy of `akousma-win11.ico`.
- `dist/unix/hicolor/`: the Linux hicolor theme icons (16 to 512 px and scalable), installed by `dist/CMakeLists.txt`.
- `dist/macos/akousma.icns`: the macOS app bundle icon.
- `data/icons/full/akousma.png` and `akousma-grey.png`: 1000 px renders, not used by the build.

To install another style by default, render the files above again from its SVG and copy its `.ico` to `dist/windows/akousma.ico`.
For example, for the Windows and Linux icons (the `.icns` likewise):

```sh
svg=data/icons/akousma/akousma-win10.svg
for s in 16 22 24 32 48 64 128 256 512; do
  rsvg-convert -w $s -h $s $svg -o dist/unix/hicolor/${s}x${s}/apps/akousma.png
done
cp $svg dist/unix/hicolor/scalable/apps/akousma.svg
cp ${svg%.svg}.ico dist/windows/akousma.ico
```

The default of the setting is `AppIcon::kDefaultStyle` in `src/core/appicon.h`.

## In the style of the icon sets

Each bundled icon set has an `akousma` icon: the same pomegranate wedge, crown at the top left, drawn after the set's own icons.
It goes where the set's other icons go, with the sizes and the file type the set uses for them, and is listed in the set's `.qrc`:

| Icon set | Files in `data/iconsets/` | Style |
| --- | --- | --- |
| Adwaita | `adwaita/scalable/akousma.svg` | One scalable drawing on a 128 px canvas like the GNOME app icons: GNOME palette, flat front with soft gradients, the fruit's side below it, lit at both ends. |
| Breeze | `breeze/{16,22,32,48,64}/akousma.svg` | Flat with a slight diagonal gradient, a darker line along the bottom, the seeds cast a 45° long shadow on the pith (32 px and up). |
| Breeze Dark | `breeze-dark/{16,22,32,48,64}/akousma.svg` | The Breeze files: Breeze Dark only recolors its monochrome icons, its colored icons are Breeze's. |
| elementary | `elementary/{16,24,32,48,64,128}/akousma.svg` | elementary palette (Strawberry, Latte), soft gradients, a fine translucent outline, a 1 px inner highlight and a soft shadow. |
| Oxygen | `oxygen/{16,22,32,48,64,128,256}/akousma.png` | Glossy and round: radial gradients, specular highlights, glossy seeds, a blurred drop shadow. |
| Papirus | `papirus/{16,22,24,32,48,64}/akousma.svg` | Flat and bold colors, a light 1 px top edge and a slight shadow under the icon. |
| Papirus Dark | `papirus-dark/{16,22,24,32,48,64}/akousma.svg` | The Papirus files: Papirus Dark shares Papirus' colored app icons. |
| Tango | `tango/{16,22,24,32}/akousma.png`, `tango/scalable/akousma.svg` | Tango palette (Scarlet Red, Aluminium), a 1 px outline in the darkest shade of the same color, a 1 px inner highlight, a shadow on the ground; 24 px is the 22 px drawing with a 1 px border. |

The small sizes (16 to 32 px) are drawn on their own: fewer, larger seeds, a simpler crown and the skin around the cut face.
The SVGs in the sets are drawn by Qt SVG, so they use no clip paths, masks or filters.
The PNGs are rendered from the SVGs in `iconsets/` here, which may use them:

```sh
cd data/icons/akousma/iconsets
for s in 16 22 32 64; do rsvg-convert -w $s -h $s akousma-oxygen-$s.svg -o ../../../iconsets/oxygen/$s/akousma.png; done
rsvg-convert -w 48 -h 48 akousma-oxygen-64.svg -o ../../../iconsets/oxygen/48/akousma.png
for s in 128 256; do rsvg-convert -w $s -h $s akousma-oxygen.svg -o ../../../iconsets/oxygen/$s/akousma.png; done
for s in 16 22 32; do rsvg-convert -w $s -h $s akousma-tango-$s.svg -o ../../../iconsets/tango/$s/akousma.png; done
convert ../../../iconsets/tango/22/akousma.png -background none -gravity center -extent 24x24 ../../../iconsets/tango/24/akousma.png
```

`AppIcon` makes the icons 16 to 256 px from these files (an SVG drawn at the size, a PNG scaled when the set has no file for it), the pictures larger than 256 px from the SVG or the largest PNG, and the grey icons from them, as for the styles.
The desktop's icon theme, an installed theme or one of the user's own themes gets the pomegranate of the bundled set made from that theme: `theme` in the set's `iconset.ini` names the theme's folder (for example `Papirus`).
These icons don't count as icons of the set (the "x of y icons" in the settings), and `dist/scripts/extract-iconsets.py` keeps them when it builds a set again.
