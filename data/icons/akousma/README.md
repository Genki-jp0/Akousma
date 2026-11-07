# Akousma app icon

Akousma's app icon is a pomegranate wedge.
It comes in one style per Windows design era, each as an SVG source and a multi-size Windows `.ico` (16, 24, 32, 48, 64, 128 and 256 px).

| Variant | Files |
| --- | --- |
| **Windows 11 Fluent (default)** | `akousma-win11.svg`, `akousma-win11.ico` |
| Windows 10 | `akousma-win10.svg`, `akousma-win10.ico` |
| Windows 8.1 flat desktop | `akousma-win81-desktop.svg`, `akousma-win81-desktop.ico` |
| Windows 8 Metro tile | `akousma-win8-tile.svg`, `akousma-win8-tile.ico` |
| Windows Vista/7 Aero | `akousma-aero.svg`, `akousma-aero.ico`, plus `akousma-aero-32.svg` and `akousma-aero-16.svg` drawn for small sizes |

The Windows 11 Fluent icon is the one Akousma uses everywhere.
These files are rendered from `akousma-win11.svg`:

- `dist/windows/akousma.ico`: the executable's icon (`windres.rc.in`) and the installer's (`akousma.nsi.in`), a copy of `akousma-win11.ico`.
- `data/icons/<size>/akousma.png` and `akousma-grey.png`: the window, About dialog and system tray icons (built-in icon set, `data/icons.qrc`); the grey one is the tray icon when nothing plays. `data/icons/full/` has the 1000 px originals.
- `data/pictures/strawberry.png`, `strawberry-grey.png` and `strawberry-faded.png`: the macOS tray icon, the context view's placeholder picture and the faded playlist background (`data/data.qrc`).
- `dist/unix/hicolor/`: the Linux hicolor theme icons (16 to 512 px and scalable), installed by `dist/CMakeLists.txt`.
- `dist/macos/akousma.icns`: the macOS app bundle icon.

The other variants are alternates, not used by the build.
To switch to one of them, render the files above again from its SVG and copy its `.ico` to `dist/windows/akousma.ico`.
For example, for the icon sets and the Windows and Linux icons (`data/icons/full/`, `data/pictures/` and the `.icns` likewise):

```sh
svg=data/icons/akousma/akousma-win10.svg
for s in 22 32 48 64 128; do
  rsvg-convert -w $s -h $s $svg -o data/icons/${s}x${s}/akousma.png
  convert data/icons/${s}x${s}/akousma.png -colorspace Gray -define png:color-type=6 data/icons/${s}x${s}/akousma-grey.png
done
for s in 16 22 24 32 48 64 128 256 512; do
  rsvg-convert -w $s -h $s $svg -o dist/unix/hicolor/${s}x${s}/apps/akousma.png
done
cp $svg dist/unix/hicolor/scalable/apps/akousma.svg
cp ${svg%.svg}.ico dist/windows/akousma.ico
```
