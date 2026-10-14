# StepMania 3.9 AURA

> **This is just StepMania 3.9.**  The 2004 release, nothing new: the same
> features, the same gameplay, the same look.  Your 3.9 songs, themes,
> noteskins, announcers, courses, settings and profiles work as they are, with
> nothing to convert.  Underneath it's been rebuilt for today (SDL 3, native
> Wayland, Windows 10/11, macOS on Apple Silicon, FreeBSD and OpenBSD, lower
> input and display latency, faster loading), but you shouldn't be able to tell.
> If something that worked in 3.9 works differently here, that's a bug.  Please
> report it.
>
> **BUT** turn on **Wife** (Gameplay Options, Wife Scoring) and you get:
>
> - **Wife3 scoring**, the same formula Etterna uses, on every play, next to 3.9's
>   own score and grade.  A Wife% counter in gameplay, and replays saved for
>   every score.
> - **F5 on the song select screen:** your local leaderboard for the chart, and
>   its **difficulty per skillset** (Stream, Jumpstream, Handstream, Stamina,
>   JackSpeed, Chordjack, Technical) at the current rate, from Etterna's own
>   calculator.  F5 on the results screen shows your SSRs.
> - **Online rankings at [aurasm.online](https://aurasm.online):** press **F7** on
>   song select to log in, and every play is sent to the server, which re-judges
>   it from your key presses and ranks it.  Chart leaderboards, player ratings per
>   skillset, and global and per-country rankings, Etterna Online style, on
>   more than 1,700 packs and 100,000 ranked charts.  Registration needs an invite
>   code, which you can get on the [Discord](https://discord.gg/CCygPSpenu).
>   **F8** opens a chat with everyone else who is online, and **F10** the
>   multiplayer rooms: play a chart together with a live scoreboard.
>
> All of that is the **3.9a** build, where Wife is on from the start.  Turn it
> off and none of it exists: no counter, no panels, no network traffic.  Just
> 3.9.  The **3.9** build is the same game without any of it in it at all
> (see [Two builds](#two-builds)).

**Play online in two steps:** with the 3.9a build, register at
[aurasm.online](https://aurasm.online) with an invite code, and press F7 on the
song select screen to log in.  More in [Etterna-style
scoring](#etterna-style-scoring) and [Online](#online).

The few intentional differences:

- Judgments land about one frame later than in stock 3.9.  3.9 guessed that key
  presses happened a whole frame earlier than they did; this guesses the middle
  of the frame.  If you had tuned a global offset for 3.9, retune it.
- "Wait For Vsync" is off by default, and there are two new graphics settings:
  Fast Rendering (on by default) and `MaxFPS` (1000 by default).
- The song cache is a single `Cache/cache.db` file.  The old cache files are
  deleted on the first start, which reloads every song once.
- The menu timer is off by default ("Menu Timer" in Machine Options turns it back
  on).  A `MenuTimer` value already in your `StepMania.ini` is kept.
- In 3.9a, Remember Options is on by default: the options a player last used on the player
  and song options screens come back in the next game, with or without a profile
  (see [Remember Options](#remember-options)).  Turn "Remember Options" off in
  Gameplay Options (or `RememberOptions=0` in `StepMania.ini`) to get exactly 3.9's
  behavior, where only a profile keeps a few options.
- Constant C Speed is on by default: a C-mod (C500, say) scrolls at the same speed
  on screen at any music rate, so at 2.0x the arrows are twice as dense instead of
  twice as fast.  Turn "Constant C Speed" off in Gameplay Options (or
  `ConstantCSpeed=0` in `StepMania.ini`) to get 3.9's behavior, where C speeds
  follow the music (see [Music rate keys](#music-rate-keys)).  X-mods are not
  affected.
- Sound goes through SDL; the ALSA, OSS and sndio drivers are gone (see
  [Sound](#sound)).
- The loading window is SDL's; the GTK one is gone.
- An optional Etterna-style scoring mode, off by default and adding the two
  settings `Wife` and `ShowWifeInGameplay`.  With it off nothing is different (see
  [Etterna-style scoring](#etterna-style-scoring)).
- Optional online scores on top of it, at https://aurasm.online by default.
  Like the rest of Wife they're off while Wife is off (see [Online](#online)).
- In 3.9a, on the music select screen `-`, `=` and `+` change the music rate by
  0.1x, from 0.5x to 2.0x; in 3.9 those keys do nothing there.  They are two new buttons,
  "Rate Up" and "Rate Down", so Config Key/Joy has two more rows at the end of
  each game's list, and you can map them to other keys or to pad and joystick
  buttons (see [Music rate keys](#music-rate-keys)).
- In 3.9a, in dance, Player 1's Left, Down, Up and Right are also on `D`, `F`,
  `J` and `K` by default, besides the arrow keys.  Like the `+` key they go in the second
  slot of Config Key/Joy, and only if that slot and the key are free.
- Event Mode is on by default, so you can keep playing songs instead of being
  sent back to the title screen after the last stage.  Turn it off in the coin
  options for the arcade-style stage limit.
- If Discord is running, it shows that you're playing Stepmania 3.9 and what
  you're playing (see [Discord](#discord)).  "Discord Presence" in Gameplay
  Options turns it off.

Linux, FreeBSD, OpenBSD, Windows 10/11 and macOS 14 and later on Apple Silicon
are supported.  The build uses [Meson](https://mesonbuild.com/) (1.1 or newer).  Windows and macOS have their own sections, [Windows](#windows) and [macOS](#macos).

## Downloads

Ready-made builds are on the
[Releases](https://github.com/twinklingwitch/stepmania-3.9a/releases) page.
There are two flavors, the [two builds](#two-builds):

- **vanilla**, `StepMania-3.9-<version>-<system>.zip`: plain StepMania 3.9 with
  the fixes (`-Dextras=false`): no Wife scoring, MSD, leaderboards, online
  scores, chat or rooms.
- **aura**, `StepMania-3.9a-<version>-<system>.zip`: 3.9a, everything in this
  README.

Each comes for `windows-x64`, `linux-x64` and `macos-arm64`.  The zip holds a
game folder (on macOS an app) that runs as it is: the game, the libraries it
needs, and the themes, noteskins, announcers, courses and the three songs that
come with the game.  Put your own songs in its `Songs` folder (see [Where
user files go](#where-user-files-go) for where they go if the folder isn't
writable).  The two flavors can be installed side by side, and keep their
settings apart.

- **Windows** 10 or 11, x64: unzip, then run `stepmania.exe` in the
  `StepMania-3.9` or `StepMania-3.9a` folder.  The executable isn't signed,
  so SmartScreen may warn about it the first time: More info, Run anyway.
- **Linux**, x64: unzip, then run `./stepmania` in the folder.  It needs glibc
  2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 36 or later, any current
  rolling release), an OpenGL driver, OpenSSL 3 (`libcrypto.so.3`) and, for
  aura, libcurl (`libcurl.so.4`).  These are part of every desktop install.  SDL
  3, FFmpeg and the other libraries are in the folder's `lib`; SDL uses the
  system's X11 or Wayland and PipeWire, PulseAudio or ALSA.
- **macOS** 14 or later on Apple Silicon: unzip and move the app to
  Applications.  vanilla is `StepMania.app`, aura `StepMania-aura.app`.  Neither
  is notarized, so macOS refuses to open it at first; see
  [Gatekeeper](#gatekeeper).

### Making a release

Push a tag that starts with `v`:

    git tag v0.3.0
    git push origin v0.3.0

`.github/workflows/release.yml` then builds both flavors for the three
systems, runs the tests, and makes a GitHub release for the tag with the six
zips, named as above with the tag as the version, and notes generated from the
pull requests and commits since the previous release.  It takes 10 to 15
minutes.  If a build fails there is no release; fix it, then delete the tag
(`git push origin :v0.3.0`, `git tag -d v0.3.0`) and tag again.

The same workflow can be started by hand to try the builds without making a
release: Actions, Release, Run workflow (or `gh workflow run release.yml --ref
<branch>`).  The zips are then the run's artifacts, with the commit instead of
a tag in their names.  GitHub only offers manual workflows that are on the
default branch, but they can build any branch.

How each system is built:

- Windows is cross-compiled in a Debian 13 container against MSYS2's
  libraries (see [Cross-compiling from Linux](#cross-compiling-from-linux)),
  and the tests run under Wine.  `Utils/msys2-sysroot.py` pins nothing: it
  takes the packages MSYS2 has on the day (checked against MSYS2's own
  checksums), so two releases can ship different versions of SDL and the
  other DLLs.  FFmpeg is built there, trimmed to what decoding movies takes,
  which makes for a few DLLs instead of MSYS2's hundred.
- Linux is built in an Ubuntu 22.04 container, which sets the glibc it needs.
  SDL 3 and the same trimmed FFmpeg are built there (their versions are set
  in the workflow), and `Utils/linux-bundle.sh build <folder>` makes the game
  folder: it copies the game, its data and every library it uses that isn't
  in the list above into the folder, and points the game at them with an
  `$ORIGIN` run path (needs `patchelf`).
- macOS is built on GitHub's macOS 14 runner with Homebrew's libraries (as
  they are on the day, like MSYS2's), and `Utils/macos-bundle.sh` makes the
  app (see [Making StepMania.app](#making-stepmaniaapp)); the app needs the
  macOS it was built on or later.

Each build job also uploads its meson logs as an artifact if it fails; the
macOS jobs always upload theirs, with the game's log and a screenshot from
trying to start it on the runner, which has no real GPU, so that part may fail
without anything being wrong.

## Building

    meson setup build
    meson compile -C build
    meson test -C build        # needs -Dtests=true at setup

Optional features are `auto` by default and can be forced with
`-D<name>=enabled` or `-D<name>=disabled`: `ffmpeg`, `vorbis`, `mad`, `jpeg`,
`online` (libcurl).  The boolean options are `extras`, `networking`,
`crash_handler` and `tests`.  `meson configure build` lists them all.

### Two builds

The same source builds two games:

- **3.9a**, the default: everything in this README, with Wife scoring on from
  the start.
- **3.9**, with `-Dextras=false`: plain StepMania 3.9 with the fixes and
  Discord, and none of the rest built in: no Wife scoring, MSD, local
  leaderboards and replays, online scores, chat or rooms, and no libcurl.
  Discord shows what you play, without the online buttons.  The Wife options
  aren't in Gameplay Options, and the themes' Wife lines on the music select
  and results screens stay empty.  It keeps 3.9's keys and behavior: the
  Rate Up and Rate Down buttons are there for the key config but have no keys
  until you map them, `D`, `F`, `J` and `K` aren't dance keys, and Remember
  Options starts off.  It comes with 3.9's own themes only, `default` and
  `techno`, and no songs: Nocturnal Dream and the Default song pack are
  3.9a's.

Both read the same songs, themes and settings files, and both can be
installed side by side: where a name of the 3.9a build would clash with plain
3.9's, it's `stepmania-aura` (a system-wide install, see
[Installing](#installing)) or `3.9a` (its user folder, see [Where user files
go](#where-user-files-go)).  In a game folder of its own the executable is just
`stepmania`.

Lua 5.0.3 is bundled and built with the game.  Everything else comes from the
system and is found with pkg-config where possible.

## Installing

    meson install -C build

The default prefix is `/usr/local`, which gives:

| Path                              | Contents                                                |
|-----------------------------------|---------------------------------------------------------|
| `<prefix>/bin/stepmania`          | wrapper script that starts the game                     |
| `<prefix>/libexec/stepmania/`     | the `stepmania` binary                                  |
| `<prefix>/share/stepmania/`       | Announcers, BGAnimations, CDTitles, Characters, Courses, Data, NoteSkins, Packages, RandomMovies, Songs, Themes, Visualizations |
| `<prefix>/share/doc/stepmania/`   | Docs, NEWS and the HTML manual                          |

The wrapper changes to `<prefix>/share/stepmania` and runs the binary, which
looks for `Songs` in the current directory.  Use `--prefix` and `--destdir` as
usual to install elsewhere; the wrapper contains the final prefix.

The 3.9a build (see [Two builds](#two-builds)) installs next to it without
touching it: it's `stepmania-aura` everywhere, `<prefix>/bin/stepmania-aura`,
`<prefix>/libexec/stepmania-aura/`, `<prefix>/share/stepmania-aura/` and
`<prefix>/share/doc/stepmania-aura/`.

### Where user files go

If the game directory is writable by the user running the game, everything
(preferences, profiles, caches, screenshots, logs) is stored there, as in 3.9
itself.  If it isn't, as with a system-wide install, the game creates
`~/.stepmania-3.9` (`%APPDATA%\StepMania\3.9` on Windows,
`~/Library/Application Support/StepMania/3.9` on macOS) and mounts it on top of
the installed files:

- Preferences (`Data/StepMania.ini`, `Data/GamePrefs.ini`, `Data/Keymaps.ini`),
  profiles (`Data/MachineProfile`, `Data/LocalProfiles`), `Cache`, `Screenshots`
  and `log.txt`/`info.txt` are written there.
- Songs, themes, noteskins, announcers, courses, characters and so on that you
  put in `~/.stepmania-3.9/Songs`, `Themes`, `NoteSkins`, ... are found next to
  the installed ones, and files in them override installed files with the same
  name.  The standard subdirectories are created on first run.

The macOS app always does this, since it must not write into itself (see
[macOS](#macos)).

The 3.9a build's folder is `~/.stepmania-3.9a` (`%APPDATA%\StepMania\3.9a`,
`~/Library/Application Support/StepMania/3.9a`), so it never touches the plain
3.9 one.  The first time it makes it, it copies `Data/` over from the 3.9
folder if there is one (preferences, profiles and scores, the online login),
and it finds the songs, themes and so on in the 3.9 folder as well as its own,
without changing or removing anything there.  New ones go into the 3.9a
folder.

### Uninstalling

If you installed with `meson install`, run this from the source tree, with the
same build directory you installed from:

    sudo ninja -C build uninstall

Meson keeps a list of every file it installed and removes exactly those.  If the
build directory is gone, remove the installed files by hand (with the default
`/usr/local` prefix):

    sudo rm -rf /usr/local/bin/stepmania /usr/local/libexec/stepmania \
        /usr/local/share/stepmania /usr/local/share/doc/stepmania

(For the 3.9a build, the same with `stepmania-aura`.)

If you run the game from the source tree or any other folder you can write to,
it never writes outside that folder, so deleting the folder removes everything.
On Windows, delete the game folder; on macOS, StepMania.app or StepMania-aura.app.

Your own files (see above) aren't removed by any of this.  Delete
`~/.stepmania-3.9` (`%APPDATA%\StepMania\3.9` on Windows,
`~/Library/Application Support/StepMania/3.9` on macOS), or the same with
`3.9a` for the 3.9a build, to remove them too.
Back up `Data/` first if you want to keep your profiles and scores.

## Running from the build directory

The game finds its data in the directory of the executable or, failing that, in
the current directory, so run it from the top of the source tree:

    cd /path/to/stepmania
    build/src/stepmania

Preferences and logs are then written into the source tree.

## Dependencies

Required: SDL 3 (3.2 or newer), OpenGL and GLU, libpng, zlib, SQLite 3, libcrypto
(OpenSSL or LibreSSL), PCRE2, libsamplerate and pthreads.  Everything else is
optional.

SDL 3 uses Wayland when the compositor supports it, and X11 otherwise; set
`SDL_VIDEODRIVER=x11` to force X11.

Debian and Ubuntu:

    apt install build-essential meson ninja-build pkgconf libsdl3-dev \
        libgl-dev libglu1-mesa-dev libpng-dev zlib1g-dev libsqlite3-dev \
        libssl-dev libpcre2-dev libsamplerate0-dev libjpeg-dev libvorbis-dev \
        libmad0-dev libavformat-dev libavcodec-dev libavutil-dev libswscale-dev

FreeBSD (OpenSSL is part of the base system):

    pkg install meson ninja pkgconf sdl3 mesa-libs libGLU png sqlite3 \
        pcre2 libsamplerate jpeg-turbo libvorbis libmad ffmpeg
    meson setup build

OpenBSD (X11, sndio, libexecinfo and LibreSSL are part of the base system):

    pkg_add meson ninja sdl3 png sqlite3 pcre2 libsamplerate jpeg libvorbis libmad \
        ffmpeg
    CPPFLAGS=-I/usr/local/include LDFLAGS=-L/usr/local/lib \
    PKG_CONFIG_PATH=/usr/X11R6/lib/pkgconfig:/usr/local/lib/pkgconfig \
        meson setup build

Libraries without a pkg-config file (libmad, libiconv) are looked up in the
compiler's default paths, so on the BSDs set `CPPFLAGS` and `LDFLAGS` as above
if one of them is not found.

Both BSDs build with clang and libc++, which is the default there.  On Linux the
same toolchain works with `CXX="clang++ -stdlib=libc++"`.

## Sound

Sound goes through SDL on every platform, with the Null driver (no sound) as the
fallback if SDL can't open an audio device.  Set `SoundDrivers=Null` in
`StepMania.ini` to run without sound.

The SDL driver mixes the sound itself and hands SDL the finished stream, so it
works with whatever SDL's audio backend is (PipeWire, PulseAudio, ALSA, sndio,
...); set `SDL_AUDIO_DRIVER` to force one.  It asks SDL for a 512 frame device
buffer, or `SoundWriteAhead` frames if that preference is set, and logs the
size it actually got.  SDL cannot report how much latency lies beyond its own
buffer (a PipeWire or PulseAudio server, a Bluetooth link), so use the global
offset in the options to correct for it.

## Video

The game is started full screen by default.  Set `Windowed=1` in
`Data/StepMania.ini` (or use the options menu) to run in a window.

## Etterna-style scoring

An extra for people who also play [Etterna](https://etternaonline.com): score
every song the way Etterna does, next to 3.9's own scoring.  It is in the 3.9a
build only, **on by default** there, and with it off the game behaves exactly as
without it: the same
judgments, scores, grades, life and combo, the same saved profiles, screens and
keys, and nothing extra is drawn or written.

**What Wife3 is.**  Etterna's scoring gives every tap and hold head up to 2
points by how far from the note you hit it: all of it within 5 ms (at Judge 4),
then falling smoothly (an erf curve) to 0 at 65 ms and linearly to -5.5 at
180 ms.  A miss costs 5.5, a mine 7 and a dropped hold 4.5 (a hold whose head
you missed counts as dropped).  Wife% is your points over the points of the whole
chart, so it can go negative.  The curve follows the Judge Window Scale
(`JudgeWindowScale`, whose Judge 1 to Justice values are the same as Etterna's
judges); the default is Judge 4, which is also 3.9's default timing.
`JudgeWindowAdd` isn't taken into account.  Wife% is in addition to the score,
grade and percent 3.9 keeps, which are not touched.

**Turning it on or off.**  Options, Gameplay Options, Wife Scoring (or `Wife=1` in
`Data/StepMania.ini`); in the 3.9a build it starts on.  "Show Wife% In Gameplay" there hides the gameplay counter
(`ShowWifeInGameplay=0`).  Only the default and Nocturnal Dream themes have these
two lines; with another theme use the ini file, or add `conf,Wife` and `conf,ShowWifeInGameplay`
lines to its `ScreenGameplayOptions` metrics.
Only songs that a person plays count: not courses, demonstrations, autoplay or
the computer's play, and only plays that 3.9 would save (not failed or
disqualified ones).

**What you get**, all drawn over the theme in the same way with every theme, and
only when Wife is on:

| Where          | Key / what                                                          |
|----------------|---------------------------------------------------------------------|
| Music select   | F5 shows or hides the local leaderboard of the selected chart at the current music rate, with the chart's difficulty at that rate (see below) |
| Music select   | F6, with the leaderboard up, switches between the player's profile (the machine's if there isn't one) and all profiles |
| Music select   | With Wife on, F7 is the online panel (see [Online](#online)); with `OnlineServer` empty it says how to set one.  With Wife off it does nothing |
| Gameplay       | a small Wife% counter for each player at the bottom of the screen, between the score boxes in the default theme; no key |
| Results        | F5 shows or hides this play's Wife%, its rank on the chart's leaderboard and its SSRs |

None of F5 to F7 is used by the music select or results screens in 3.9 (during
gameplay they are the end song, auto sync and assist tick keys, so the counter
has no key).  Leaderboards are per chart and music rate, best Wife% first, from
the machine profile and the local profiles.

**Difficulty.**  The F5 panel also shows the selected chart's MSD at the current
music rate, as a list down the right side of the panel: Etterna's difficulty in
each skillset (Overall, Stream, Jumpstream, Handstream, Stamina, JackSpeed,
Chordjack and Technical; the ones far under Overall are dimmer), from Etterna's
calculator (MinaCalc, see [Online](#online)).  Only 4-key dance-single charts
that aren't autogen are rated; others say "no rating".  The MSDs of a chart are
worked out once, when it has been selected for a moment with the panel up, and
kept in the song cache; at rates under 0.7x, which Etterna's table doesn't
cover (the [rate keys](#music-rate-keys) go down to 0.5x), the difficulty is
worked out for that rate, and kept only until the game closes.  The results panel shows the play's SSRs, the skillset
values the play is worth, worked out from its Wife% at Judge 4 as Etterna does.

**Files.**  Everything the feature adds is next to Stats.xml in the profile
(`Data/LocalProfiles/<id>/` or `Data/MachineProfile/`):

- `WifeScores.xml`, every Wife score with its chart key, rate and judge.  Unlike
  the high score lists in Stats.xml it isn't limited to the best few.
- `Replays/<score key>`, what was played, in the layout of Etterna's input data
  (a header line, a line per key press with the nearest note and its offset, and
  `H`, `M` and `X` lines for dropped holds, hit mines and misses), as plain text.
  Replays go with the player's profile, or the machine's for a player without one.

**Profiles stay readable by stock 3.9.**  Stats.xml only gets two extra nodes
inside the high scores of plays made with Wife on, `WifePercent` and
`WifeScoreKey`.  3.9 reads those nodes by name and skips ones it doesn't know,
so a profile used with Wife works unchanged in 3.9 (which drops the two nodes the
next time it saves).  The other files are ones 3.9 never looks at.

**Chart keys.**  Charts are matched by Etterna's chart key, so the same chart has
the same key here and in Etterna: `X` and the SHA-1 of the notes and BPM of every
row.  Keys are worked out when needed, not while loading songs.  They follow
what the .sm file says, so they differ from Etterna's for charts that use
negative BPMs or stops (Etterna turns those into warps), that have no BPM at beat
0, or that have rolls, lifts, fakes or keysounds, which 3.9 doesn't load.

**EtternaOnline is not supported.**  The game has its own optional online
scores instead, see [Online](#online).

**Credits.**  The Wife3 scoring, the chart key and the replay layout are ported
from [Etterna](https://github.com/etternagame/etterna), which is MIT licensed,
Copyright (c) 2016-2023 Etterna.  The license text is in `src/Wife.h`, and
MinaCalc's in `extern/MinaCalc/LICENSE`.

## Themes

A new install starts with 3.9's `default` theme, as always; `techno` is there
too.  A third theme, **Nocturnal Dream** (an anime theme for 3.9 by Anime
Toshokan), comes with the 3.9a build and shows more about the charts and your
scores.
Pick it in Options, Appearance Options, Theme, or set `Theme=Nocturnal Dream` in
the game's section of `Data/GamePrefs.ini`.

What it adds to the original theme:

| Where        | What                                                                 |
|--------------|----------------------------------------------------------------------|
| Music select | the chart's length at the current music rate                        |
| Music select | your best score on the chart: 3.9's best grade and percent, or with Wife on the best Wife% at the current rate (the best at any rate, with its rate, if there is none at this one) |
| Music select | with Wife on, the chart's MSD and its two highest skillsets at the current rate, and the grade of your best Wife% on the wheel |
| Results      | the song's title, artist and pack, the music rate, and each judgment's share of the taps under its count |
| Results      | with Wife on, the Wife% (and its grade in the grade box), the judge, the chart's MSD and the play's SSR, where it ranks among your scores on the chart at that rate, "PERSONAL BEST" when it beats them, and the mean and standard deviation of your hits and the combo breaks (Goods, Boos and Misses) |

The Wife grades are Etterna's: AAAA from 99.955%, AAA from 99.7%, AA from 93%,
A from 80%, B from 70%, C from 60%, D under that.  The best scores are the
player's profile's, or the machine's for a player without one.  The F5 to F7
panels work as with any other theme.

Compared with the theme as it was released, a screen in the attract loop that
advertised an adult theme is gone, the warning screen has new art, images whose
frame sizes 3.9 complains about were padded by a transparent pixel, and two
music loops were re-encoded smaller.

**For theme authors.**  The lines Nocturnal Dream shows are there for any theme;
the default theme has them all off.  Each one that is on needs `<name>X`,
`<name>Y`, `<name>OnCommand` and `<name>OffCommand`.

| Metric (section)                              | Turns on                                     |
|-----------------------------------------------|----------------------------------------------|
| `ShowRateDisplay` (`ScreenSelectMusic`)       | `RateDisplay`: "Rate 1.1x"                   |
| `ShowChartLength`, `ShowChartBPM`             | `ChartLength`, `ChartBPM`, at the rate       |
| `ShowChartNPS`, `ShowChartNotes`              | `ChartNPS`: average notes per second; `ChartNotes`: notes, jumps, hands, holds and mines |
| `ShowChartMSD`, `ShowChartSkillsets`          | `ChartMSD`, `ChartSkillsets` (with Wife on)  |
| `ShowChartBest`                               | `ChartBest`: the best score                  |
| `GradesFromWife` (`MusicWheelItem`)           | the wheel's grades from Wife3 scores (with Wife on) |
| `ShowSongInfo` (`ScreenEvaluation`)           | `SongTitle`, `SongArtist`, `SongPack`        |
| `ShowRate`                                    | `RateDisplay`                                |
| `ShowJudgmentPercents`                        | `MarvelousPercentP1` ... `MissPercentP2`     |
| `ShowWifeArea`                                | `WifePercentP1`, `WifeDetailP1`, `WifeRankP1`, `WifeRecordP1` and `WifeStatsP1` (and P2), with Wife on |
| `GradeFromWife`                               | the grade area shows the Wife3 score's grade (with Wife on) |

The music select lines are for the first player's chart and use the font
`ScreenSelectMusic chart info`; the results ones use `ScreenEvaluation song
info`, `judge percent`, `wife` and `wife info`.  All of them are `Common normal`
in the default theme.

## Online

Optional online scores: an account on a score server, leaderboards for every
chart, and a player rating, Etterna style.  The server is
[AURA Stepmania 3.9](https://aurasm.online) by default.  **Online only works with
Wife on**, in the 3.9a build: with Wife off nothing goes over the network and
nothing changes.  The server is a
separate project, [39server](https://github.com/raccoon-byte/39server); the
protocol, including how scores are checked and ratings worked out, is in its
`docs/API.md`.

**Turning it on.**  Turn on Wife (Gameplay Options, Wife Scoring, or `Wife=1` in
`Data/StepMania.ini`), register on the server's website (aurasm.online needs an
invite code), and press F7 on the song select screen to log in.  To use another
server, set its address in `Data/StepMania.ini`:

    OnlineServer=https://scores.example.org

and to keep Wife scoring but never go online, set `OnlineServer=` (empty).  The
other settings:

| Setting | Default | |
|---|---|---|
| `OnlineServer` | `https://aurasm.online` | The server.  Empty is no online at all.  Only used with Wife on. |
| `OnlineAutoSubmit` | 1 | Send every ranked play when its results come up.  With 0, F7 on the results screen sends it. |
| `OnlineAllowInsecureHttp` | 0 | Testing only: allow plain `http://` to other machines than this one.  `http://localhost` and `http://127.0.0.1` always work. |
| `OnlineWriteTestVectors` | 0 | Testing only: write every play to `Data/OnlineVectors/` as a test vector for the server's judge. |

Anything else must be `https://`, and the server's certificate is always checked.

**Keys**, drawn over the theme like the rest of Wife:

| Where | Key | |
|---|---|---|
| Music select | F7 | The online panel of the first player: the selected chart's online leaderboard at the current rate (the top ten and your own place), and your rating.  If the chart can't be ranked (see "What counts" below), or the rate can't, it says so and why instead; if your options or settings would keep your plays from being ranked, it says why beside the leaderboard.  F7 again shows the other player's, if there are two, then hides it. |
| Music select | F6 | With the online panel up: log out. |
| Results | F5 | The play's SSRs and what happened to it online: sending, accepted (with your rank on the chart and your new rating), pending review, rejected (with the server's reason), unranked (with the reason), or queued to try again. |
| Results | F7 | Send the play, with `OnlineAutoSubmit=0`. |
| Anywhere but gameplay and the editor | F8 | The chat: one room for everyone logged in to the server, IRC style, with who's online on the right.  Type and press Enter to send; `/me waves` is an action.  Esc or F8 closes it.  While it's open it takes the keyboard (a key that was already down when it opened still reaches the game).  It chats as the first player who is logged in.  Gameplay and the editor keep F8 for themselves. |
| Anywhere but gameplay and the editor | F10 | Multiplayer rooms, in a panel like the chat's: see [Rooms](#rooms).  F8 and F10 switch between the two. |

**Accounts.**  Make an account on the server's web site; the online panel shows
the address (`<server>/register`).  If you aren't logged in, F7 opens a login
form: type the username, Tab or Enter, the password (shown as stars), Enter.
Esc closes it.  While the form is open it takes the keyboard (US layout, as the
game's other text entry), and only then.  Each player side logs in on its own:
with a profile, the login belongs to the profile; without one, to that side of
the machine.  A login lasts 30 days (the server decides); after that, or if the
server says it's no longer valid, F7 asks again.

**Where the login is kept.**  Never the password, only the token the server
gives at login, with the server's address and your username: in
`OnlineToken.ini` in the profile's folder (next to `Stats.xml`), or in
`Data/Online/GuestP1.ini` and `GuestP2.ini` for players without a profile.  On
Linux and the BSDs the file can only be read by you (mode 0600).  Logging out
deletes it.

**What counts.**  A play is ranked when it is a single song in regular play
(not a course, battle or rave), played by a person (no autoplay), on a chart
the server ranks, at a music rate from 0.7 to 2.0, at Judge 4 or harder with
3.9's judge windows, with no turn but Mirror, nothing that adds or removes
notes, no RandomVanish, no AutoSync, PadStickSeconds and
ConstantUpdateDeltaSeconds at 0, and not changed during the song (offset or BPM
keys, the speed keys Tab and `, autoplay).  The server ranks the 4-key
dance-single charts of the packs it has (not autogen ones) that have no attacks,
no hold ends without a start (that is what the rolls of newer simfiles become in
3.9), and a rating from the difficulty calculator.  The simfile's format
doesn't matter (3.9 doesn't read .ssc files at all, only .sm, .dwi, .bms and
.ksf).  Assist Tick is allowed.  Failed plays and plays that aren't
saved aren't sent.  The game checks these before the song so the reason is
shown on the results, and the online panel shows them on the song select
screen; the server checks them all again.

**What is sent.**  When a ranked song starts, the game asks the server for a
play ticket (with the chart, rate, judge and options), so the server knows the
play began and that it can't arrive before the song could have been played.
When the results come up it sends the score: the Wife%, 3.9's judgment counts,
max combo and grade, the options, the date, the Wife replay, and the **input
log**: every press and release of every arrow with its exact time, the mines a
held arrow crossed, and the misses and hold results.  The server judges the play
again from the input log against its own copy of the chart, and keeps the
replay and the input log with the score, where they can be looked at.  Nothing
else about you or your computer is sent (the requests say the game's version
and the operating system).

**When the server can't be reached**, the play is still saved locally as
always.  A score that couldn't be sent is kept in `Data/Online/Queue/` and tried
again, 5 seconds later and then less and less often, also after a restart, until
the server has it or its ticket runs out (half an hour after the song).  A score
the server has accepted is never sent again (`Data/Online/Submitted.txt`).  If
the ticket didn't arrive when the song started, the play is unranked ("no
ticket").

### Rooms

Rooms are osu!-style multiplayer on the online server: players meet in a room,
the host picks a chart, everyone who has it and is ready plays it at the same
time, with a live scoreboard and the round's results at the end.  F10 opens
the rooms panel, IRC style: commands start with `/`, anything else is said in
the room.  You play as the first player who is logged in.

| Command | |
|---|---|
| `/list` | The rooms on the server (shown when you open the panel outside one). |
| `/create [name]` | Make a room and be its host. |
| `/join <#> [password]` | Join a room by its number. |
| `/leave` | Leave the room. |
| `/ready`, `/unready` | You have the chart and want to play it.  Needed again for each round. |
| `/pick` | Host: the chart and rate selected on the music wheel become the room's. |
| `/start [force]` | Host: start the round.  `force` starts without whoever isn't ready. |
| `/kick <name>`, `/host <name>` | Host: remove a player, or make them the host. |
| `/name <text>`, `/password [password]` | Host: rename the room, set or clear its password. |
| `/help` | The commands. |

**The chart.**  When the host picks a chart, the game looks for it in your
songs: where the host has it (the same group and folder), then any song by the
same title, and checks that the notes are the same (the chart key).  If you
have it, the music wheel moves to it and the rate is set; if not, the panel
says what's missing.  Charts don't have to be ranked online.

**A round.**  When the host starts, the game starts the song for everyone in the
round from the music select screen, as if Start was pressed, and waits in
gameplay until everyone has loaded (at most 30 seconds, then it goes on without
whoever hasn't).  During the song the top right corner is the live scoreboard
(Wife%); after it, the results screen shows the round's ranking, also in the
panel.  Players who finished rank before those who gave up or left.  Rooms
don't change anything about saving scores or sending them as ranked plays.

With the panel closed, a line at the top right says what the room is doing.
Rooms are kept by the server in memory only; a player who's offline for 20
seconds leaves the room.  The protocol is section 8.6 of the server's
`docs/API.md`.

**MinaCalc.**  The difficulty numbers are Etterna's calculator, MinaCalc (calc
version 527), in `extern/MinaCalc`, built as its own C++20 library with a C
interface.  It is Etterna's code unchanged but for one line, and gives the same
numbers as Etterna's own build on the same input.  On 64-bit ARM (Apple Silicon)
its fast square root goes through sse2neon, as in Etterna's ARM builds, and the
MSDs and SSRs come out up to about 0.2 different from x86's.  Online, only the
server's numbers count.

**Building.**  On Linux and the BSDs online play needs libcurl (`libcurl4-openssl-dev`
on Debian and Ubuntu, `curl` on FreeBSD and OpenBSD); without it the game builds
without online support and says so in the panel.  `-Donline=disabled` leaves it
out on purpose.  Windows uses WinHTTP, which is part of Windows, so there is
nothing to install and no DLL to ship.  macOS uses its own libcurl.

**The chart export.**  The server learns its charts from the game itself.  This
loads a song pack with the game's own loader and writes every chart as JSON:
metadata, chart key, the rows MinaCalc rates, the MSD table, timing and notes.
Run it from the game directory:

    stepmania --export-charts=/path/to/Pack --export-out=pack.json

It replaces the game for that run: nothing is shown, and the song cache, banner
cache and preferences aren't touched.  The format is section 4.1 of the
server's `docs/API.md`.

## Music rate keys

On the music select screen the buttons "Rate Down" and "Rate Up" lower and raise
the music rate by 0.1x, from 0.5x to 2.0x in steps of 0.1; at either end the
button stops there.  A small box at the top of the screen says "Rate 1.20x" for a
moment.  In 3.9a, Rate Down is `-` and Rate Up is `=` and `+` by default, so
there is a key for it on layouts where `=` needs Shift (Spanish and Latin
American, say).  In the 3.9 build they have no keys until you map them in
Config Key/Joy.
3.9 has no use for these keys on that screen, and this works with Wife on or off
and with any theme.

- They change the same music rate as the Rate line of the Song Options screen:
  the next song is played at it, Song Options shows it selected, and
  [Remember Options](#remember-options) keeps it.  A rate that isn't on a tenth
  (from a profile's modifiers, say) goes to the next tenth in the direction of the
  key.
- The song's preview plays at the rate, pitch included, as the song does in
  play.  It starts again a moment after the last key press.  It plays the same
  part of the song as at 1.0x, the sample start and length being seconds of the
  song, so at 0.5x the loop takes twice as long and at 2.0x half as long.  The
  beat the themes animate to keeps up with the music.  At 1.0x nothing is
  different from before.
- With "Constant C Speed" on (the default), a C-mod looks the same at any rate:
  C500 at 2.0x scrolls as fast on screen as at 1.0x, with the arrows twice as
  close, as in Etterna.  With it off, C speeds follow the music's speed, so C500
  at 2.0x looks like C1000, as in 3.9.  X-mods always follow the BPM.
- They're ordinary buttons of the keymap: map them in Config Key/Joy (Options,
  Config Key/Joy Mappings) to any key, or to a button of a pad or joystick, like
  the other buttons.  They are the last two rows of the list, after Operator, for
  every game, and have no use outside the music select screen, where they work
  for either player's controller, joined or not.  Only Player 1's controller has
  default keys: `-` for Rate Down, `=` and `+` (the `+` key, not the keypad's)
  for Rate Up.  The `+` key goes in the second slot of the row if it's free, and
  like the other default keys isn't used if you have mapped it to something else.
  Ctrl, Alt and Meta with a key do nothing.
- `Keymaps.ini` keeps its format, which stores a button by its number (`0-14=`,
  controller and button), so the new buttons are the numbers after the old ones
  and a `Keymaps.ini` from before them keeps all its mappings and just gets the
  default keys for the new buttons.  3.9 reads a `Keymaps.ini` that has the new
  numbers without complaint (it keeps the lines it doesn't know the buttons of
  and writes them back), so the same file works in both.
- The F5 panel shows the difficulty at the new rate straight away.
- Online scores are ranked from 0.7x to 2.0x, so a play at 0.5x or 0.6x is
  scored and saved locally as usual but is not sent: the results say "unranked,
  the music rate must be between 0.7x and 2.0x", and the online panel says that
  only those rates are ranked.
- The Rate line of a theme's Song Options screen may list fewer rates than
  these (the default theme lists 0.3x to 2.0x).  If the
  current rate isn't in the list, the line is left alone and the rate stays as
  it is until you move in that line.

## Remember Options

In 3.9 a profile keeps only a few of your player options (speed, reverse, scroll
BPM, tilt, no holds/mines/jumps/hands/quads and the noteskin), a player without a
profile keeps nothing, and the song options screen is forgotten every time.  With
**Remember Options** the options you last played with come back at the start of
the next game, on the music select screen and in the options screens.  It is on
by default in 3.9a and off in 3.9; "Remember Options: OFF" in Gameplay Options (`RememberOptions=0` in
`Data/StepMania.ini`) gives you 3.9's behavior exactly.  Only the default and
Nocturnal Dream themes have the line in their menu; for another theme add `conf,RememberOptions` to its
`ScreenGameplayOptions` metrics, or use the ini file.

**What is remembered.**  Everything on the player options screen: speed, every
modifier (accel, effects, appearance, turn, insert, scroll, holds, mines, hide,
perspective), the noteskin and so on.  From the song options screen: the music
rate, the bar drain, the assist tick and the fail type.  The fail type is only
remembered if you changed it from the machine's "Default Fail Type"; otherwise
3.9's own rule stays (it makes beginner and easy steps easier to fail).

**What is not.**  Life type and battery lives (the play mode and the course set
those), auto sync and save scores (they change songs and scores, and you would
not expect them to come back).  Also not what the game did to your options: it
remembers what you chose, not the changes of course attacks or of the extra
stage, and nothing is saved on extra stages, in demonstrations or in the editor.

**When.**  The options are written when a song starts, so the choices that count
are the ones you started a song with.  They are put back when the players are
final (after the style is chosen) at the start of each game.  A player with a
profile gets the profile's options (written with the rest of the profile, so
after a stage has been played, like 3.9's own default modifiers); a player
without one gets the options kept for that side, left or right, in `StepMania.ini`.
The song options are shared by both players; if both have some, player 1's are
used.  The remembered options replace the profile's 3.9 default modifiers, which
are still saved and read as before and are what you get with the option off.

**Where.**  In a profile, in `RememberedOptions.ini` next to `Stats.xml` (3.9
doesn't know it and leaves it alone).  Without a profile, in `StepMania.ini`:

    RememberedPlayerOptionsP1=clearall, 2x, Boost, Drunk, Reverse, Mirror
    RememberedSongOptionsP1=NoRecover, FailEndOfSong, 1.20xMusic, AssistTick

(`P2` for the right side).  These are the usual modifier strings; delete the
lines to forget them.  They are not specific to a game type, so options that do
not exist in another game (a noteskin for example) are left out there.

Remembered options count for scoring as if you had picked them by hand.

## Discord

If the Discord app is running on the same computer, your Discord profile shows
"Playing Stepmania 3.9" and what you're doing:

| Where | Discord shows |
|---|---|
| Menus, title screens | In the menus, and for how long you've been playing |
| Music select | Choosing a song, and the group of the song under the cursor |
| Gameplay | The song and artist, the difficulty and meter, the music rate if it isn't 1.0x, the chart's MSD (with its main skillset when one clearly stands out), and a countdown to the end of the song |
| Courses | The course, "Stage 2/4" and the song, unless it's a mystery song |
| Results | The song, your grade, and with Wife on your Wife% and "new best" if it's your best on the chart |
| Editor | Editing a chart |
| Demonstration, attract screens | Idle |

With two players it shows a party of 2.  The MSD is only there for the charts
that Etterna rates (4-key dance-single, not autogen; see
[Etterna-style scoring](#etterna-style-scoring)), and is worked out off the
main thread when the song starts, then kept like the F5 panel's.  There's
always an "AURA rankings" button to the online rankings, and when you're logged
in online (see [Online](#online)) the small online icon with your username and
a button to your profile on the server.

**Turning it off.**  Gameplay Options, Discord Presence: OFF
(`DiscordRichPresence=0` in `Data/StepMania.ini`).  It is on by default; Discord
itself can also hide what you play (Settings, Activity Privacy).  For a theme other
than the default and Nocturnal Dream, add `conf,DiscordRichPresence` to its
`ScreenGameplayOptions` metrics, or use the ini file.

**Privacy.**  The game only talks to the Discord app on your own computer,
through its local socket (a named pipe on Windows); nothing goes over the
internet from the game for this, and nothing at all happens if Discord isn't
running (the game looks for it again every 15 seconds, quietly).  Discord then
shows the activity to your friends as it does for any game.  What it gets is
listed above: no file names or paths, no profile names, and of your online
account only your public username, while you're logged in.

**The Discord application.**  What Discord shows as the game's name and its
pictures come from a Discord application set up by the project ("Stepmania
3.9", with the images `logo` and `online`); its ID is built in.  To use another
one, set `DiscordAppId=<application ID>` in `Data/StepMania.ini`; an empty
`DiscordAppId=` turns Rich Presence off.  The game speaks Discord's IPC protocol
itself, so no Discord SDK or library is needed, on any system.

## Windows

Windows 10 and 11 on x86-64 are supported; older versions are not.  The game is
built with mingw-w64 gcc, either natively in MSYS2 or cross-compiled from
Linux.  MSVC is not supported.

Windows builds differ from the Unix ones as follows:

- Everything platform specific goes through SDL 3: the OpenGL window, keyboard,
  mouse and joystick input, error dialogs and sound (WASAPI).  There is no
  Direct3D, DirectInput or DirectSound code.
- There is no crash handler.  A failed assertion writes the reason to `log.txt`
  and shows a message box; there is no backtrace.  The `crash_handler` option
  is ignored.
- USB memory cards and parallel port lights work, see [Memory cards](#memory-cards)
  and [Parallel port lights](#parallel-port-lights) below.
- The executable has a manifest that makes the process use UTF-8 for file
  names, so songs and folders with Japanese (or any other) names work.  It also
  opts in to paths longer than 260 characters, where Windows allows them.
- Sleeping in the frame limiter and the sound threads uses a high resolution
  timer, and the system timer period is set to 1ms.

### Building in MSYS2

In an MSYS2 `MINGW64` or `UCRT64` shell:

    pacman -S --needed $MINGW_PACKAGE_PREFIX-{gcc,meson,ninja,pkgconf,sdl3,libpng,zlib,\
        sqlite3,openssl,pcre2,libsamplerate,libiconv,libjpeg-turbo,libvorbis,libmad}
    meson setup build --prefix=C:/StepMania
    meson compile -C build
    meson install -C build --strip

Add `$MINGW_PACKAGE_PREFIX-ffmpeg` for movie backgrounds.  It pulls in about a
hundred codec DLLs, which the install step copies too, so the folder grows from
about 70MB to 230MB; pass `-Dffmpeg=disabled` to leave it out even if it is
installed.

The install step produces a folder that can be copied anywhere (or zipped):
`stepmania.exe`, the DLLs it needs (found by following its imports with
`objdump`, from the `bin` directory of the SDL 3 install, or from the
directories in the `dll_dirs` option), and the data directories, `Docs` and
the HTML manual next to it.  Run `stepmania.exe` from there.  The compiler's
runtime and winpthreads are linked in, so the standard build needs these DLLs
besides `SDL3.dll`: libcrypto, libiconv, libjpeg, libmad, libogg, libpcre2-8,
libpng16, libsamplerate, libsqlite3, libvorbis, libvorbisfile and zlib1.
`Utils/windows-dlls.py` is the script that does this.

### Cross-compiling from Linux

Install the mingw-w64 compiler with the posix threads model (winpthreads), and
meson.  On Debian or Ubuntu:

    apt install gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix \
        meson ninja-build pkgconf python3 zstd

The libraries are the MSYS2 ones.  `Utils/msys2-sysroot.py` downloads them,
checks their checksums, unpacks them into a directory and writes a meson cross
file next to them (edit its `[binaries]` if your compilers are named
differently):

    Utils/msys2-sysroot.py ~/mingw-sysroot            # add ffmpeg to also get movies
    meson setup build-win --cross-file ~/mingw-sysroot/cross.ini --prefix=/stepmania
    meson compile -C build-win
    meson install -C build-win --strip --destdir $PWD/out

The game folder is then `out/stepmania`.  Add `exe_wrapper = 'wine'` to the cross
file's `[binaries]` to run the tests (`-Dtests=true`, `meson test -C build-win`)
under Wine, with `WINEPATH` pointing at `~/mingw-sysroot/mingw64/bin`.

### Memory cards

Set `MemoryCards=1` in `Data/StepMania.ini` (or turn it on in the options).  Any
removable drive with media in it is a memory card: USB sticks and the slots of
card readers.  The game looks at the drives five times a second, so cards
can be put in and taken out at any time, and no "no disk in the drive" boxes
appear for empty readers.  Drives that Windows reports as fixed (some USB
sticks and USB hard disks do) are not used, nor is the drive the game itself
runs from.  The profile is kept in the `StepMania` folder on the card, or in
the folder named by `MemoryCardProfileSubdir`.

Without further settings the first card that appears goes to the first player
without one.  To tie a card slot to a player, give the drive letter, with a
slash, in `Data/StepMania.ini`:

    MemoryCardOsMountPointP1=E:/
    MemoryCardOsMountPointP2=F:/

The letter is the one Windows gives the drive.  A card reader keeps its letters
whatever is in it, but a loose stick may get a different one each time, so for a
cabinet use readers or a hub whose ports keep their letters (assign them in Disk
Management).  `MemoryCardUsbBusP1`, `MemoryCardUsbPortP1` and the like don't do
anything here, because the USB topology isn't read; setting one stops cards from
being assigned to that player.

When a game ends and the profiles have been saved, the game flushes the cards, which
makes Windows write everything out to the device instead of keeping it in its
cache.  This is done with `FlushFileBuffers` on the volume, or on the files in
the profile folder if the volume can't be opened.  There is no eject, and the card
is not locked: if it is pulled out while the game is writing to it, that write is
lost.

### Parallel port lights

Set `LightsDriver=Win32Parallel` in `Data/StepMania.ini`.  The driver sends one byte
to the parallel port, with the same bits as the Linux `LinuxParallel` driver: bit 0
is `LIGHT_MARQUEE_UP_LEFT`, then up-right, lower-left, lower-right, buttons left,
buttons right, bass left and bass right.  Controller button lights are not sent.

Windows 10 and 11 don't let a program use I/O ports, so this needs
[InpOut](https://www.highrez.co.uk/downloads/inpout32/) (`InpOutBinaries`).  Put
`inpoutx64.dll` next to `stepmania.exe`, or into `C:\Windows\System32`.  It brings
a signed kernel driver, which it installs the first time it is used, and only an
administrator can do that: run the game once as administrator, and after that
as a normal user.  If the DLL or its driver is missing, `log.txt` says so and the
game runs without lights, like the Null driver.

The port is 0x378 (LPT1).  Cards in a PCI or PCIe slot have other addresses, which
the Device Manager shows under the port's resources (Properties, Resources, "I/O
Range").  Give it in hex in `Data/StepMania.ini`:

    ParallelLightsPort=0xE010

### Limits

- Only built and run under Wine so far.  Real hardware has not been tested.
- A GPU driver with OpenGL is needed.  The game uses the legacy fixed function
  pipeline, so a core profile only driver will not do.
- No installer.  If the folder is not writable (`C:\Program Files`), the game
  stores preferences, profiles, the song cache and logs in
  `%APPDATA%\StepMania\3.9` (`3.9a` for the 3.9a build), and looks there for
  added songs and themes.

## macOS

macOS 14 (Sonoma) and later on Apple Silicon (M1 and later) is the target.
Intel Macs and older versions of macOS are not supported, and there is no
universal binary.

It has been built and played on an Apple Silicon Mac.  If something goes
wrong, `log.txt` and `info.txt` in `~/Library/Application Support/StepMania/3.9`
(`3.9a` for the 3.9a build) are what's needed to look into it.

It differs from the other systems like this:

- Everything platform specific goes through SDL 3: the OpenGL window, keyboard
  and joystick input, sound (CoreAudio) and error dialogs.  None of 3.9's
  Mac OS X code (Carbon, QuickTime, the old CoreAudio driver) is left.
- OpenGL is Apple's legacy 2.1 context, the only one with the fixed function
  pipeline 3.9 draws with.  Apple has deprecated OpenGL, but it still works,
  translated to Metal.
- On Retina displays the game draws at the display's full resolution.  The
  resolution in the options is in the usual (point) units, so 640x480 is a
  640x480 point window drawn with 1280x960 pixels.
- Cmd+Q quits (Alt+F4 elsewhere) and Cmd+F12 saves a screenshot.  The Command
  keys are "left meta" and "right meta" in the key config, as in 3.9 on a Mac.
- There is no crash handler.  After a crash, macOS keeps a report with a
  backtrace in `~/Library/Logs/DiagnosticReports` (or Console, Crash Reports),
  and `log.txt` says what the game was doing.
- No memory card or lights drivers.
- StepMania.app never writes into itself (that would break its signature):
  preferences, profiles, the song cache, screenshots and logs always go to
  `~/Library/Application Support/StepMania/3.9` (`3.9a` for the 3.9a build),
  and songs, themes and so on
  that you put in its `Songs`, `Themes`, ... folders are found next to the ones
  in the app.  The Library folder is hidden; in the Finder use Go, Go to Folder
  (Shift+Cmd+G).  Run from a plain folder (as from the build directory below),
  the game keeps everything in that folder as on the other systems.

### Building

Install the Xcode command line tools (`xcode-select --install`) and
[Homebrew](https://brew.sh), then:

    brew install meson ninja pkgconf sdl3 libpng pcre2 libsamplerate openssl@3 \
        libvorbis mad jpeg-turbo
    export PKG_CONFIG_PATH="$(brew --prefix openssl@3)/lib/pkgconfig:$(brew --prefix jpeg-turbo)/lib/pkgconfig"
    meson setup build
    meson compile -C build
    meson test -C build        # needs -Dtests=true at setup

openssl@3 is needed for libcrypto (macOS's own isn't usable by programs) and
isn't on pkg-config's path by itself.  zlib, SQLite, iconv and libcurl (for
[online scores](#online)) come with macOS.  Add `ffmpeg` to the `brew install`
for movie backgrounds; it brings about a hundred libraries along.

To run it from the build directory, start it from the top of the source tree,
as on the other systems:

    build/src/stepmania

### Making StepMania.app

    Utils/macos-bundle.sh build

makes `build/StepMania-aura.app`, or `build/StepMania.app` for a build with
`-Dextras=false` (a second argument puts it elsewhere): the game, its data in
`Contents/Resources`, and the Homebrew libraries it uses in
`Contents/Frameworks`, so it runs on Macs without Homebrew.  The two have
different bundle identifiers, so both can be in Applications.  The script points
the game at those copies with `install_name_tool` and signs the app ad hoc
(`codesign -s -`), which Apple Silicon requires.  Move it to Applications or
anywhere else.  The data is copied from the source tree as it is, the same
directories `meson install` installs.

### Gatekeeper

The app is signed ad hoc, not with an Apple Developer ID, and it isn't
notarized.  On the Mac that built it, it opens normally.  A copy that was
downloaded (for example from the [releases](#downloads)) is quarantined, and
macOS refuses to open it at first ("Apple could not verify..."):

- macOS 14: Control-click (or right-click) the app, choose Open, then Open.
- macOS 15 and later: open it once, then in System Settings, Privacy &
  Security, click Open Anyway.
- Or remove the quarantine in Terminal:

      xattr -dr com.apple.quarantine /Applications/StepMania.app
      xattr -dr com.apple.quarantine /Applications/StepMania-aura.app

If macOS says the app "is damaged", that's the quarantine too; the `xattr`
command fixes it.  Getting rid of all this takes notarizing the app, which needs
a paid Apple Developer Program membership and a Developer ID certificate; that
isn't set up.

### Building on GitHub Actions

The [release workflow](#making-a-release) builds both apps on an Apple Silicon
runner, runs the tests, and uploads them as zips.  Started by hand, it only
builds, so that's the way to try a branch on a Mac without having one.
