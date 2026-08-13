// DawBridge bridge script.
//
// Injected into every page the embedded browser navigates to (see
// WebBrowserComponent::Options::withUserScript in PluginEditor.h). Runs
// before any other resource on the page loads, with window.__JUCE__.backend
// already available.
//
// Responsibilities:
//   1. Capture whatever audio the page connects to its AudioContext
//      destination and stream it to the native host (instead of letting it
//      play out of the machine's speakers).
//   2. Provide a synthetic Web MIDI implementation wired to the host's MIDI
//      input/output, so pages built against navigator.requestMIDIAccess work
//      unmodified.
//   3. Expose host transport (tempo, playhead, play/record state) via
//      window.DawBridge.getTransport() and a "dawbridgetransport" window event.
(function () {
  "use strict";

  if (window.__dawBridgeInstalled) return;
  window.__dawBridgeInstalled = true;

  if (!window.__JUCE__ || !window.__JUCE__.backend) {
    // Not running inside DawBridge's native integration (e.g. previewed in a
    // normal browser tab) - nothing to bridge.
    return;
  }

  const backend = window.__JUCE__.backend;

  //========================================================================
  // Audio capture
  //========================================================================

  // Runs inside the AudioWorkletGlobalScope. Buffers incoming frames and
  // posts them to the main thread in larger chunks so we're not paying
  // native-bridge overhead every 128-sample render quantum.
  const CAPTURE_WORKLET_SOURCE = `
    class DawBridgeCaptureProcessor extends AudioWorkletProcessor {
      constructor (options) {
        super();
        const opts = (options && options.processorOptions) || {};
        this.numChannels = opts.channelCount || 2;
        this.framesPerPost = opts.framesPerPost || 1024;
        this.buffers = [];
        for (let ch = 0; ch < this.numChannels; ch++)
          this.buffers.push(new Float32Array(this.framesPerPost));
        this.writeIndex = 0;
      }

      process (inputs) {
        const input = inputs[0];
        const numFrames = (input && input[0]) ? input[0].length : 128;

        for (let i = 0; i < numFrames; i++) {
          for (let ch = 0; ch < this.numChannels; ch++) {
            const channelData = (input && (input[ch] || input[0])) || null;
            this.buffers[ch][this.writeIndex] = channelData ? channelData[i] : 0;
          }

          this.writeIndex++;

          if (this.writeIndex >= this.framesPerPost)
            this.flush();
        }

        return true;
      }

      flush () {
        if (this.writeIndex === 0) return;

        const interleaved = new Float32Array(this.writeIndex * this.numChannels);
        for (let i = 0; i < this.writeIndex; i++)
          for (let ch = 0; ch < this.numChannels; ch++)
            interleaved[i * this.numChannels + ch] = this.buffers[ch][i];

        this.port.postMessage(
          { numFrames: this.writeIndex, numChannels: this.numChannels, sampleRate, pcm: interleaved.buffer },
          [interleaved.buffer]
        );
        this.writeIndex = 0;
      }
    }
    registerProcessor("dawbridge-capture-processor", DawBridgeCaptureProcessor);
  `;

  const captureStates = new WeakMap();

  function bytesToBase64(bytes) {
    let binary = "";
    const chunkSize = 0x8000;
    for (let i = 0; i < bytes.length; i += chunkSize) {
      binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunkSize));
    }
    return btoa(binary);
  }

  function handleCapturedAudio(msg) {
    backend.emitEvent("webAudioChunk", {
      sampleRate: msg.sampleRate,
      numChannels: msg.numChannels,
      numFrames: msg.numFrames,
      pcmBase64: bytesToBase64(new Uint8Array(msg.pcm)),
    });
  }

  // Returns a node that's always safe to connect to synchronously. The real
  // capture worklet is attached to it once its module has finished loading.
  function getCaptureStub(context) {
    let state = captureStates.get(context);
    if (state) return state.stub;

    const stub = context.createGain();
    stub.gain.value = 1;
    state = { stub, node: null };
    captureStates.set(context, state);

    try {
      const blob = new Blob([CAPTURE_WORKLET_SOURCE], { type: "application/javascript" });
      const url = URL.createObjectURL(blob);

      context.audioWorklet
        .addModule(url)
        .then(() => {
          const node = new AudioWorkletNode(context, "dawbridge-capture-processor", {
            numberOfInputs: 1,
            numberOfOutputs: 0,
            channelCount: 2,
            channelCountMode: "explicit",
            processorOptions: { channelCount: 2, framesPerPost: 1024 },
          });
          node.port.onmessage = (event) => handleCapturedAudio(event.data);
          stub.connect(node);
          state.node = node;
          URL.revokeObjectURL(url);
        })
        .catch((err) => {
          console.error("[DawBridge] failed to start audio capture:", err);
        });
    } catch (err) {
      console.error("[DawBridge] audio capture unavailable:", err);
    }

    return stub;
  }

  const originalConnect = AudioNode.prototype.connect;
  AudioNode.prototype.connect = function (target) {
    if (typeof AudioDestinationNode !== "undefined" && target instanceof AudioDestinationNode) {
      const stub = getCaptureStub(this.context);
      return originalConnect.apply(this, [stub, ...Array.prototype.slice.call(arguments, 1)]);
    }
    return originalConnect.apply(this, arguments);
  };

  //========================================================================
  // Web MIDI shim
  //========================================================================

  class DawBridgeMidiPort extends EventTarget {
    constructor(id, name, type) {
      super();
      this.id = id;
      this.name = name;
      this.manufacturer = "DawBridge";
      this.type = type;
      this.version = "1.0";
      this.state = "connected";
      this.connection = "open";
      this._onmidimessage = null;
    }

    get onmidimessage() {
      return this._onmidimessage;
    }

    set onmidimessage(fn) {
      if (this._onmidimessage) this.removeEventListener("midimessage", this._onmidimessage);
      this._onmidimessage = fn;
      if (fn) this.addEventListener("midimessage", fn);
    }

    open() {
      return Promise.resolve(this);
    }
    close() {
      return Promise.resolve(this);
    }
  }

  class DawBridgeMidiOutput extends DawBridgeMidiPort {
    send(data) {
      backend.emitEvent("webMidiOut", { bytes: Array.from(data) });
    }
    clear() {}
  }

  const midiInput = new DawBridgeMidiPort("dawbridge-host-in", "DawBridge Host", "input");
  const midiOutput = new DawBridgeMidiOutput("dawbridge-host-out", "DawBridge Host", "output");

  const fakeMidiAccess = {
    inputs: new Map([[midiInput.id, midiInput]]),
    outputs: new Map([[midiOutput.id, midiOutput]]),
    sysexEnabled: false,
    onstatechange: null,
    addEventListener() {},
    removeEventListener() {},
  };

  navigator.requestMIDIAccess = function () {
    return Promise.resolve(fakeMidiAccess);
  };

  backend.addEventListener("hostMidi", (payload) => {
    const data = new Uint8Array(payload.bytes);
    const event = new Event("midimessage");
    event.data = data;
    midiInput.dispatchEvent(event);
  });

  //========================================================================
  // Host transport
  //========================================================================

  window.DawBridge = {
    isHost: true,
    _transport: { bpm: 120, ppqPosition: 0, isPlaying: false, isRecording: false, sampleRate: 44100 },
    getTransport() {
      return { ...window.DawBridge._transport };
    },
    sendMidi(bytes) {
      midiOutput.send(bytes);
    },
  };

  backend.addEventListener("hostTransport", (payload) => {
    window.DawBridge._transport = payload;
    window.dispatchEvent(new CustomEvent("dawbridgetransport", { detail: payload }));
  });

  window.dispatchEvent(new Event("dawbridgeready"));
})();
