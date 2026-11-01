# Akousma

Akousma is a **music player and music collection organizer**, a fork of [Strawberry Music Player](https://github.com/strawberrymusicplayer/strawberry), developed by Genki (genki_jp0).
It’s written in **C++ using the Qt framework**, designed for **audiophiles and music collectors**.

![Screenshot of Strawberry Music Player, the upstream project Akousma is based on](https://raw.githubusercontent.com/strawberrymusicplayer/strawberry/master/data/screenshot/screenshot.png)

*Screenshot of upstream Strawberry Music Player.*

---

## :globe_with_meridians: Resources

- **GitHub:** https://github.com/Genki-jp0/Akousma
- **Issues:** https://github.com/Genki-jp0/Akousma/issues

---

## :warning: Opening an Issue

Before creating a new GitHub issue:

1. **Search [existing issues](https://github.com/Genki-jp0/Akousma/issues)** to avoid duplicates. If one already exists, comment there with any additional information.
2. **Report Akousma problems here**, not to the upstream Strawberry project, since Akousma has its own changes.
3. **Include your Akousma version and operating system.**

---

## :white_check_mark: Features

- Play and organize your music collection
- Support for WAV, FLAC, Ogg FLAC, WavPack, Ogg Vorbis, Opus, Ogg Speex, MPC, TrueAudio, AIFF, MP4/AAC, ALAC, MP3, ASF, Monkey’s Audio, and DSD (DSF/DSDIFF)
- Bit-perfect playback on Linux
- MPRIS2 / D-Bus remote control on Linux
- Native desktop notifications
- Advanced playlist management
- Smart and dynamic playlists
- Audio analyzer, equalizer, moodbar, and waveform seek bar
- Volume normalization with ReplayGain and EBU R128 loudness analysis
- Editing tags, and fetching missing tags via acoustic fingerprinting using [AcoustID](https://acoustid.org/) and [MusicBrainz](https://musicbrainz.org/)
- Album cover art from: [Last.fm](https://www.last.fm/), [MusicBrainz](https://musicbrainz.org/), [Discogs](https://www.discogs.com/), [Musixmatch](https://www.musixmatch.com/), [Deezer](https://www.deezer.com/), [Tidal](https://www.tidal.com/), [Qobuz](https://www.qobuz.com/), [Spotify](https://www.spotify.com/)
- Lyrics from: [Genius](https://genius.com/), [Musixmatch](https://www.musixmatch.com/), [lyrics.ovh](https://lyrics.ovh/), [songlyrics](https://www.songlyrics.com/), [azlyrics](https://www.azlyrics.com/), [elyrics](https://www.elyrics.net/), [letras](https://www.letras.mus.br) and [lrclib.net](https://lrclib.net/)
- Audio format conversion (transcoding) to MP3, AAC, FLAC, Ogg Vorbis, Opus, Speex, WavPack, and ASF
- Music transfer to USB, MTP and iPod devices
- Scrobbling to [Last.fm](https://www.last.fm/), [ListenBrainz](https://listenbrainz.org/), and Subsonic
- Global keyboard shortcuts (Linux, macOS, and Windows)
- Discord Rich Presence
- Audio CD playback
- Internet radio from [Radio Paradise](https://radioparadise.com/), [SomaFM](https://somafm.com/), [Radio Browser](https://www.radio-browser.info/), and custom streams
- Streaming from Subsonic-compatible servers
- Unofficial Tidal, Spotify, and Qobuz integration
- Import settings, collection, album covers, smart playlists and saved passwords from Strawberry or Clementine

---

:white_check_mark: Runs on **Linux**, **OpenBSD**, **FreeBSD**, **macOS**, and **Windows**.

---

## :gear: Requirements

To build Akousma from source, you’ll need:

**Dependencies:**
- [CMake 3.25 or higher](https://cmake.org/)
- C/C++ compiler ([GCC](https://gcc.gnu.org/), [Clang](https://clang.llvm.org/), or [MSVC](https://visualstudio.microsoft.com/vs/features/cplusplus/))
- [pkg-config](https://www.freedesktop.org/wiki/Software/pkg-config/) or [pkgconf](https://github.com/pkgconf/pkgconf)
- [GLib](https://developer.gnome.org/glib/)
- [OpenSSL](https://www.openssl.org/)
- [Qt 6.4 or higher](https://www.qt.io/), 6.8 or higher on Windows (Core, Concurrent, Gui, Widgets, Network, SQL, D-Bus)
- [SQLite 3.9 or higher](https://www.sqlite.org)
- [ALSA (Linux only)](https://www.alsa-project.org/)
- [GStreamer](https://gstreamer.freedesktop.org/)
- [TagLib 1.12 or higher](https://www.taglib.org/)
- [ICU](https://unicode-org.github.io/icu/)
- [uchardet](https://www.freedesktop.org/wiki/Software/uchardet/)
- [KDSingleApplication 1.1.0 or higher](https://github.com/KDAB/KDSingleApplication)

**Dependencies for optional features:**
- Fingerprinting & tagging: [Chromaprint](https://acoustid.org/chromaprint)
- Fast Spectrum Moodbar: [FFTW3](http://www.fftw.org/)
- PulseAudio integration: [PulseAudio](https://www.freedesktop.org/wiki/Software/PulseAudio/)
- Audio CD support: [libcdio](https://www.gnu.org/software/libcdio/)
- MTP devices: [libmtp](http://libmtp.sourceforge.net/)
- iPod Classic: [libgpod](http://www.gtkpod.org/libgpod/)
- EBU R128 normalization: [libebur128](https://github.com/jiixyj/libebur128)
- Secure password storage using libsecret on Linux/BSD: [libsecret](https://gnome.pages.gitlab.gnome.org/libsecret/)
- Pretty OSD on Wayland: [LayerShellQt 6](https://invent.kde.org/plasma/layer-shell-qt) and [Wayland](https://wayland.freedesktop.org/) (requires Qt 6.5 or higher)

Also install GStreamer plugins **base**, **good**, and optionally **bad**, **ugly** and **libav** for full codec support.

## :wrench: Build from Source

**Get the code:**

    git clone https://github.com/Genki-jp0/Akousma

**Build and install:**

    cd Akousma
    cmake -S . -B build
    cmake --build build --parallel $(nproc)
    sudo cmake --install build

This builds and installs the `akousma` executable.

For building on Windows with Visual Studio 2022/2026, see: :point_right: https://github.com/Genki-jp0/akousma-build-tools

---

## :strawberry: Based on Strawberry

Akousma is based on [Strawberry Music Player](https://github.com/strawberrymusicplayer/strawberry) by Jonas Kvinge and contributors, which was originally forked from *Clementine* in 2018.
Akousma is **free software released under the GPL**, like Strawberry. Credit for the Strawberry code base goes to its authors.

The links below belong to the **upstream Strawberry project**, not to Akousma. Please don’t report Akousma issues there.

- **Strawberry website:** https://www.strawberrymusicplayer.org
- **Strawberry wiki:** https://wiki.strawberrymusicplayer.org
- **Strawberry forum:** https://forum.strawberrymusicplayer.org
- **Strawberry on GitHub:** https://github.com/strawberrymusicplayer/strawberry
- **Strawberry builds:** https://builds.strawberrymusicplayer.org
- **Strawberry on the openSUSE Build Service:**
  - Stable: https://build.opensuse.org/package/show/home:jonaski:strawberry/strawberry
  - Unstable: https://build.opensuse.org/package/show/home:jonaski:strawberry-dev/strawberry
- **Strawberry Ubuntu PPAs:**
  - Stable: https://launchpad.net/~jonaski/+archive/ubuntu/strawberry
  - Unstable: https://launchpad.net/~jonaski/+archive/ubuntu/strawberry-unstable
- **Strawberry translations (Crowdin):** https://crowdin.com/project/strawberrymusicplayer
- **Strawberry packaging status:** https://repology.org/metapackage/strawberry/versions
- **Strawberry Windows build tools:** https://github.com/strawberrymusicplayer/strawberry-msvc-build-tools
