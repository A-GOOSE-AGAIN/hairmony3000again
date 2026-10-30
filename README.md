# HAIRMONY 3000

**The choir machine from the year 2089.** A Windows VST3 (plus standalone app) that turns one sung vocal into a full choir that follows your lyrics, melody and timing — wrapped in a chrome-and-neon, hair-metal-from-the-future interface.

HAIRMONY 3000 follows the same three-screen workflow as AM I AUDIO's *I AM CHOIR* (load → craft → review/export), but it is an independent project with its own name, artwork and engine. It is not affiliated with AM I AUDIO. Where I AM CHOIR uses AI models trained on real singers, HAIRMONY 3000 builds each choir from DSP: every virtual singer is a pitch- and formant-shifted, humanised copy of your vocal.

---

## Get the Windows VST3 from GitHub

1. Create a new GitHub repository and push this folder to it (the `main` branch is fine).
2. Open the repo's **Actions** tab. The workflow **Build HAIRMONY 3000 (Windows VST3)** runs automatically on every push (or press **Run workflow**).
3. When it finishes (~15–25 min), open the run and download the **HAIRMONY-3000-Windows-x64** artifact. It contains:
   - `HAIRMONY 3000.vst3` — copy to `C:\Program Files\Common Files\VST3\`
   - `HAIRMONY 3000.exe` — standalone app
   - `INSTALL.txt`
4. Optional: push a tag such as `v1.0.0` and the zip is attached to a GitHub Release automatically.

Every Windows build runs the engine tests, the plugin tests and **pluginval at strictness level 10** before packaging. A Linux job runs the same suites as a second check.

---

## The workflow

| Screen | Controls |
|---|---|
| **01 LOAD** | **Record from track** (REC + level meter), drop zone (drag from your DAW or Explorer), **Browse Files**, ⚙ settings (temp-file folder, About, Tips) |
| **02 CRAFT** | **Start Over**, vocal list with per-take **mute** and timeline position, **+ Import Audio**, **REC** (another take), **play/stop**, **Choir** selector (◀ ▶ or click for a list), **Microphone** selector, **Voicing** (Unison / Octaves / Full Harmony), **Octave Up / Down**, **Key**, **Scale**, **Above: 3rd / 5th**, **Below**, **Stereo / Mono**, **CREATE VOCALS** |
| **03 REVIEW** | **Back**, play/stop all, **Play With DAW**, **Gain**, one lane per output with waveform, play (solo), **mute** and a draggable tape icon, **Lo-Cut**, **Brightness**, **Reverb**, **STEMS** tile (drag every output at once), **Save Stems…** |

### Record from track
Put HAIRMONY on your vocal track, press **REC** and hit play in your DAW. The plugin records the track's audio; stopping the DAW (or looping back) ends the take and imports it as *Take 1, Take 2…* with its timeline position. After **Create Vocals**, **Play With DAW** makes the choir play along at exactly that spot whenever the DAW plays — no dragging needed (drag-out still works). In the standalone app recording starts immediately and **STOP** ends it. Takes up to 5 minutes; they're saved with your project.

### Voicing
- **Full Harmony** (default) — the main choir sings in parts in your Key/Scale: Gospel = soprano on the tune, alto a 3rd below, tenor a 6th below, bass an octave below. Kids = 2-part, Gang = tune + octave + 3rd, Cyborg = close triad.
- **Octaves** — the tune plus an octave (below for adult choirs, above for kids).
- **Unison** — everyone on the tune.

The 3rd/5th Above and Octave Up/Down switches still add their own separate stems on top.

### Choirs
| Model | Singers | Character |
|---|---|---|
| Gospel Choir | 10 | Mixed lower/higher sections, warm, natural vibrato, ~16 ms ensemble spread |
| Kids' Choir | 11 | Smaller vocal tracts (formants up ~28%), airy breath, light vibrato |
| Gang Vocals | 12 | Loose timing (~35 ms), wide detune, shout presence and grit |
| Cyborg Choir | 8 | The 2089 bonus: hard-tuned to your key/scale, metallic formant pairs |

### Microphones
Studio Condenser · Velvet Ribbon · Stage Dynamic · Valve Tube · Chrome Laser

### Harmony
3rd and 5th harmonies are **diatonic**: the pitch tracker follows your melody note-by-note and picks the right interval in the chosen key and scale (12 keys × 12 scales, including modes, pentatonics and blues). **Below** adds an octave-below choir under each harmony.

---

## How the engine works

- **Pitch tracking:** YIN with an FFT difference function, median smoothing and non-causal note stabilisation (renders are offline, so it can look ahead — no harmony lag).
- **Voices:** a peak-locked phase vocoder (Laroche–Dolson peak shifting with phase tracking) shifts pitch; a harmonic-peak spectral envelope moves formants independently, so pitch shifts keep the vowel and kids' voices get smaller vocal tracts.
- **Humanising:** every singer is an independent voice — its own random partial phases, pitch drift, timing (early and late), loudness movement, brightness and vocal-tract size. Each singer drops part of *your* vibrato and sings its own, so the choir doesn't wobble in lockstep with the lead.
- **Consonants:** unpitched sounds (s, t, sh, breaths) pass through unshifted, so they stay crisp instead of smearing.
- **Pitch tracking:** sub-octave checks plus per-phrase octave correction, and note decisions made on a vibrato-free pitch with hysteresis — harmonies don't flicker between notes when you sing with vibrato.
- **Finish:** per-choir EQ and 4× oversampled saturation (no aliasing), mic models, then Lo-Cut (24 dB/oct), Brightness (shelves) and a Freeverb-style reverb. These are baked into dragged/saved stems; Gain only affects audition volume.
- Rendering runs on a background thread across CPU cores. A 10-second vocal with all 7 outputs (×12 singers) renders in ~9 s on a 2-core machine.
- Output stems are sample-aligned with your input, so they drop straight into the DAW at the same position as the source clip.

Sessions are saved with your project: imported file paths, settings and the rendered stems (kept in the temp folder) all come back when you reopen it.

---

## Build locally (Windows)

Requirements: Visual Studio 2022 (Desktop C++ workload) and CMake 3.22+. JUCE 8.0.10 is downloaded automatically.

```bat
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The plugin lands in `build\Hairmony_artefacts\Release\VST3\HAIRMONY 3000.vst3`.

## Tests

- `tests/EngineTests.cpp` — 10,000+ checks on the DSP, including realistic jittery/breathy voices (no octave errors, stable harmony under vibrato, vibrato removal, independent singers, SATB chord tones, unshifted consonants, alias-free saturation), plus FFT accuracy, resampler, pitch tracking across 44.1/48/96 kHz, every key × scale harmony interval, shifter pitch accuracy at ±12 semitones, formant preservation and shifting (harmonic-by-harmonic), latency/timing alignment, all choir × mic combinations, stereo width, edge cases (empty, silent, clipped, DC), determinism, threading, cancellation, the review FX and random automation stability.
- `tests/PluginTests.cpp` — drives the real processor and editor, including record-from-track against a simulated DAW transport (start/stop, loops, silence, standalone, timeline-synced playback within 10 ms, recall), pluginval-style editor automation and background-thread state stress, and import errors, resampling, playback/mute/solo, rendering, review playback, random automation, WAV export, save/restore of whole sessions, start over/back/cancel, multi-file sessions at odd sample rates, every UI control, resizing, closing the editor mid-render, and PNG snapshots of each screen.

## Project layout

```
Source/core/        DSP engine (plain C++, no JUCE)
Source/Session.*    file I/O + background render job
Source/Plugin*      processor (state, playback, export) and editor shell
Source/ui/          look-and-feel and the three screens
tests/              engine + plugin test suites
.github/workflows/  Windows VST3 build, tests, pluginval, packaging
```

## Limits

- Imports up to 10 minutes per file, 16 files per session.
- Best with dry, monophonic lead vocals; heavy reverb or stacked doubles confuse the pitch tracker.
- Drag-in from the DAW depends on the host (Pro Tools and Cubase don't support it — use Browse Files). Use **Save Stems…** where dragging out isn't supported.
