# WebTap

WebTap is a VST3/AU instrument plugin that embeds a browser inside your DAW and
bridges it into your session: it captures whatever a web page renders with the
Web Audio API, feeds it into the host's audio stream, exchanges MIDI in both
directions, and gives the page your host's tempo/transport so time-based web
audio apps stay in sync. It's an open-source scaffold of the same idea as
[WAX by AudioFusion](https://audiofusion.com/wax) — "use web apps like native
plugins" — built with [JUCE](https://juce.com).

## What it does today

- Loads any URL (or a bundled example synth) into an embedded browser inside
  the plugin window.
- Captures audio the page connects to its `AudioContext` destination and mixes
  it into the host's output, instead of letting it play out of the machine's
  speakers.
- Bridges MIDI both ways: host → page arrives through a synthetic
  `navigator.requestMIDIAccess()` device, and page → host goes out through the
  plugin as regular MIDI output.
- Exposes host transport (BPM, playhead position in quarter notes,
  play/record state) to the page as `window.WebTap.getTransport()` and a
  `webtaptransport` window event.
- Remembers the last loaded URL in the plugin's saved state, so reopening a
  DAW project reloads the same page.

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for how the pieces fit
together.

## What it doesn't do (yet)

- **Effect mode.** WAX can also feed the host's audio *into* the page (using
  it as a "mic" input for web-based effects). This plugin is instrument-only
  for now — it has no audio input bus. Adding an input bus and a matching
  `page ← host audio` path in the bridge script is the natural next step.
- **Low-latency audio path.** Captured audio is bounced through
  `window.__JUCE__.backend.emitEvent`, which round-trips it through
  base64-encoded JSON over the browser's native-message channel. That's fine
  for demos and most instrument use, but it's higher-latency and more
  CPU-hungry than a real native audio tap. A production version would want a
  lower-level capture path (e.g. reading back a `MediaStreamAudioDestinationNode`
  through the browser engine's own audio pipeline, if the embedding API
  exposes one).
- **Sandboxing arbitrary URLs.** `WebBrowserComponent`'s native integration
  (required for the audio/MIDI/transport bridge) is enabled for every page
  loaded in the browser, including third-party URLs you type into the URL
  bar. JUCE's own docs flag this as a security consideration — only load
  pages you trust. See the note in `docs/ARCHITECTURE.md`.

## Building

Requires CMake ≥ 3.22 and a C++17 compiler. JUCE 8.0.6 is fetched
automatically via `FetchContent` — no manual JUCE install needed.

```sh
cmake -B build
cmake --build build --target WebTap_VST3 -j
```

- **macOS**: also builds an AU (`WebTap_AU` target); needs Xcode command line
  tools.
- **Windows**: needs the WebView2 backend (`NEEDS_WEBVIEW2` is already set in
  `CMakeLists.txt` — JUCE's CMake fetches the WebView2 SDK via NuGet
  automatically).
- **Linux**: needs WebKitGTK + GTK3 dev headers, e.g. on Debian/Ubuntu:
  ```sh
  sudo apt install libwebkit2gtk-4.1-dev libgtk-3-dev libasound2-dev \
    libjack-jackd2-dev libcurl4-openssl-dev libfreetype-dev libx11-dev \
    libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
    libxrandr-dev libxrender-dev
  ```

The built plugin is written under `build/WebTap_artefacts/`.

## Using it

1. Load WebTap on an instrument track in your DAW.
2. Type a URL into the address bar and hit **Go**, or click **Load Example
   Synth** to try the bundled demo instrument.
3. Play the track's MIDI as usual — notes reach the page, and audio the page
   renders comes back out through the plugin.

The bundled example (`Resources/example_synth.html`) is a small polyphonic
Web Audio synth with an on-screen keyboard, computer-keyboard input, and a
transport readout, meant to exercise every part of the bridge end to end. It
also runs fine in a normal browser tab (outside WebTap) for quick iteration —
the bridge script no-ops when `window.__JUCE__` isn't present.

## Project layout

```
CMakeLists.txt              Plugin target, JUCE fetch, embedded resources
Source/
  PluginProcessor.{h,cpp}   AudioProcessor: audio/MIDI FIFOs, transport, state
  PluginEditor.{h,cpp}      AudioProcessorEditor: embedded WebBrowserComponent
  WebAudioFifo.h            Lock-free ring buffer, browser thread -> audio thread
  BridgeResources.{h,cpp}   Access to the embedded bridge script / example page
Resources/
  bridge.js                 Injected into every loaded page (capture + MIDI + transport)
  example_synth.html        Bundled demo instrument
docs/
  ARCHITECTURE.md           How the pieces fit together, message formats, tradeoffs
```
