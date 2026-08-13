# Architecture

DawBridge is a native VST3/AU plugin (built with JUCE) that embeds a browser and
bridges it into the host. It is **not** a web app by itself — a DAW only
loads native plugin binaries, so the "web audio app" part only ever runs
inside the native plugin's embedded `WebBrowserComponent`.

```
┌─────────────────────────────────────────────────────────────────────┐
│ DAW (Ableton / Logic / Reaper / ...)                                 │
│                                                                       │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │ DawBridge (VST3 / AU)                                             │  │
│  │                                                                 │  │
│  │  PluginProcessor (audio thread)                                │  │
│  │    - WebAudioFifo: web page audio -> host output buffer        │  │
│  │    - MidiBuffer x2: host <-> page MIDI queues                  │  │
│  │    - getPlayHead(): host tempo / transport                     │  │
│  │             ▲                     │                            │  │
│  │             │ pushWebAudio()      │ drainHostMidiForWeb()       │  │
│  │             │ pushWebMidi()       │ getTransportSnapshot()      │  │
│  │             │                     ▼                            │  │
│  │  PluginEditor (message thread)                                 │  │
│  │    - BridgedWebView (WebBrowserComponent)                      │  │
│  │      native events: "webAudioChunk", "webMidiOut"   (JS→native)│  │
│  │      emitted events: "hostTransport", "hostMidi"    (native→JS)│  │
│  │             ▲                     │                            │  │
│  │             │ window.__JUCE__.backend.emitEvent/addEventListener│ │
│  │             ▼                     │                            │  │
│  │  ┌───────────────────────────────────────────────────────────┐│  │
│  │  │ Embedded browser (WebKitGTK / WKWebView / WebView2)        ││  │
│  │  │                                                             ││  │
│  │  │  bridge.js (injected into every loaded page)                ││  │
│  │  │   - hijacks AudioNode.connect(destination)                  ││  │
│  │  │   - AudioWorklet captures + ships PCM out                   ││  │
│  │  │   - fakes navigator.requestMIDIAccess()                     ││  │
│  │  │   - exposes window.DawBridge.getTransport()                    ││  │
│  │  │             ▲                                                ││ │
│  │  │             │ arbitrary web audio app (user-supplied URL,    ││ │
│  │  │             │ or the bundled example_synth.html)             ││ │
│  │  └───────────────────────────────────────────────────────────┘│  │
│  └───────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

## Audio path: page → host

A page can't be trusted (or expected) to route its audio anywhere special —
it just calls `someNode.connect(audioContext.destination)` like any other Web
Audio app. `bridge.js` intercepts exactly that call:

1. `AudioNode.prototype.connect` is monkey-patched. Whenever a page connects a
   node to `audioContext.destination`, the patched version silently redirects
   the connection to a capture node instead (so nothing plays out of the
   host machine's own speakers — the DAW is the only listener that matters).
2. The capture node is an `AudioWorkletNode` running a small processor
   (`DawBridgeCaptureProcessor`, defined inline in `bridge.js` and loaded from a
   `Blob` URL — no network fetch, so it works regardless of the page's
   origin). It buffers ~1024 frames (~23 ms at 44.1 kHz) before posting a
   message, to keep the message rate down.
3. The main thread receives each buffered chunk, base64-encodes the raw
   `Float32Array` bytes, and calls
   `window.__JUCE__.backend.emitEvent("webAudioChunk", {...})`.
4. `PluginEditor::handleWebAudioChunk` (registered via
   `WebBrowserComponent::Options::withEventListener`) decodes the base64 back
   to floats and calls `DawBridgeAudioProcessor::pushWebAudio`, which writes into
   a lock-free ring buffer (`WebAudioFifo`).
5. `processBlock`, running on the real-time audio thread, pulls whatever is
   available from that ring buffer each block and mixes it into the host's
   output.

This is deliberately the simple version of this pipeline: it trades some
latency and CPU for portability (it works the same way on every JUCE-supported
browser backend, with no OS-specific audio-tap code). A lower-latency
production implementation would look for a way to read the browser engine's
audio output more directly, if the embedding API on a given platform exposes
one.

## MIDI path (both directions)

- **Host → page**: MIDI the host sends to the plugin arrives in
  `processBlock`'s `MidiBuffer` and is stashed in a queue
  (`hostToWebMidi`). On a 30 Hz timer, `PluginEditor` drains that queue and
  emits a `hostMidi` event per message. `bridge.js` turns each one into a
  `midimessage` event dispatched on a synthetic `MIDIInput`, so any page
  written against the standard Web MIDI API works without modification.
- **Page → host**: `bridge.js` fakes `navigator.requestMIDIAccess()`
  entirely — real Web MIDI permission prompts and OS MIDI devices are bypassed
  in favor of one synthetic input and one synthetic output representing "the
  host". Calling `.send(bytes)` on the synthetic output (or the convenience
  `window.DawBridge.sendMidi(bytes)`) emits a `webMidiOut` event, which
  `PluginEditor::handleWebMidiOut` turns into a `juce::MidiMessage` and queues
  it (`pushWebMidi`) for the next `processBlock` to emit to the host.

## Transport sync

Every 1/30s, `PluginEditor` reads `AudioProcessor::getPlayHead()` (via
`DawBridgeAudioProcessor::getTransportSnapshot`) and emits a `hostTransport`
event with BPM, playhead position (in quarter notes), and play/record state.
`bridge.js` caches the latest value as `window.DawBridge._transport` and fires a
`dawbridgetransport` `CustomEvent` on `window` so pages can react to tempo or
transport changes (the bundled example page uses this to show a play/BPM
readout).

## Security note

`WebBrowserComponent::Options::withNativeIntegrationEnabled()` is required for
any of this bridging to work, and JUCE's own documentation is explicit that
native integration should only be enabled for content you fully control —
"navigating to 3rd party websites with these integrations enabled may expose
the application and the computer to security risks." This plugin, like WAX,
is built around loading arbitrary user-supplied URLs, so that tradeoff is
inherent to the feature, not a bug in this scaffold. `PluginEditor` shows a
persistent reminder in the UI; a production build should also consider origin
allow-listing, disabling native integration for non-bundled origins, or a
sandboxed capture worklet with a minimized native surface.

## Why an instrument, not an effect (yet)

WAX supports both roles: as an instrument it turns page audio into the
track's output; as an effect it also feeds the host's incoming audio into the
page (e.g. as a "mic" input for a web-based effect). This scaffold only
implements the instrument path — the processor has no audio input bus. Effect
mode would need: an input bus, a `host audio → page` direction in the bridge
(the mirror image of the capture path — push samples in, expose them to the
page as a `MediaStream` or an `AudioWorkletNode` source), and UI to switch
modes. Left as a follow-up rather than half-implemented here.
