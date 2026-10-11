// Sample Snagger - web audio tap.
// Runs in the page shown in the built-in browser (installed before the page's own scripts, and
// injected again later as a fallback). It listens to whatever the page plays and hands the raw
// audio to the plug-in, which polls __snag.drain().
//
// Two kinds of page audio are captured:
//  * <video> / <audio> elements - in the document or created in script (new Audio()) - through
//    captureStream(), or on WebKit by re-routing same-origin media through our AudioContext.
//  * Web Audio: anything a page sends to its speakers (SoundCloud-style players, sound-effect
//    sites, Howler / WaveSurfer, games...). Connections to an AudioContext's destination are
//    mirrored into a MediaStream destination that we listen to.
// Safety rules: page audio is never re-routed away from the speakers, and a media element that
// the page already plays through Web Audio is captured once (at the speakers), not twice.
//
// Frames: a frame from the same site as the page that holds it (a player in an <iframe>, srcdoc
// frames) is listened to by the page's own tap - there is one tap per page. A frame from another
// site is left alone: the page can't reach into it, and what plays in there must not be handed
// to the page around it either.
(function () {
  var VERSION = 6;
  if (window.__snag && window.__snag.version === VERSION) return 'ok';

  if (window.top !== window) {
    // (where the browser runs this script in every frame: join the page's tap, or stay out of it)
    try {
      var above = window.top.__snag;
      if (above && above.version === VERSION) { above.install(window); window.__snag = above; }
    } catch (e) {}
    return 'frame';
  }

  var S = window.__snag = {
    version: VERSION,
    armed: false,
    ctx: null,
    proc: null,
    sink: null,
    wins: [],            // same-site frames this tap also listens in
    via: 'none',         // how the audio is picked up: 'worklet' or 'script'
    taps: [],            // media element taps: { el, src, mode, stream, track }
    blocked: [],         // media we may not listen to (cross-origin files without CORS)
    known: [],           // media elements seen via play(), including ones not in the document
    pageTaps: [],        // Web Audio taps: { ctx, node, src }
    q: [],               // queued Float32Array chunks (interleaved stereo)
    qFrames: 0,
    maxFrames: 48000 * 10,
    level: 0,
    err: '',

    // Audio that arrived from the tap: interleaved stereo.
    enqueue: function (il) {
      if (!S.armed) return;
      var n = il.length / 2, pk = 0;
      for (var i = 0; i < il.length; i++) { var m = il[i]; if (m < 0) m = -m; if (m > pk) pk = m; }
      S.level = Math.max(pk, S.level * 0.85);
      S.q.push(il); S.qFrames += n;
      while (S.qFrames > S.maxFrames && S.q.length) { S.qFrames -= S.q[0].length / 2; S.q.shift(); }
    },

    ensureCtx: function () {
      if (S.ctx) return S.ctx;
      var AC = window.AudioContext || window.webkitAudioContext;
      if (!AC) { S.err = 'Web Audio not supported'; return null; }
      try { S.ctx = new AC({ latencyHint: 'playback' }); } catch (e) { S.ctx = new AC(); }

      // Everything that is listened to goes into one bus (kept under its old name, "proc").
      S.proc = S.ctx.createGain();
      S.proc.channelCount = 2; S.proc.channelCountMode = 'explicit';
      // ...and whatever reads the bus is kept pulling, without making any sound of its own
      S.sink = S.ctx.createGain();
      S.sink.gain.value = 0;
      S.sink.connect(S.ctx.destination);

      S.startScriptReader();   // works everywhere, at once
      S.startWorklet();        // the better way, where the page allows it: takes over when it's ready
      S.ctx.onstatechange = function () { if (S.ctx.state === 'running') S.scan(); };
      return S.ctx;
    },

    // Reads the bus on the page's own thread. A page that is busy (loading, scrolling, a heavy
    // player) makes this one skip audio - which is why the worklet below replaces it when it can.
    startScriptReader: function () {
      S.reader = S.ctx.createScriptProcessor(4096, 2, 2);
      S.reader.onaudioprocess = function (e) {
        var ib = e.inputBuffer, n = ib.length;
        var out = e.outputBuffer;
        for (var c = 0; c < out.numberOfChannels; c++) out.getChannelData(c).fill(0);
        if (!S.armed) return;
        var l = ib.getChannelData(0), r = ib.numberOfChannels > 1 ? ib.getChannelData(1) : l;
        var il = new Float32Array(n * 2);
        for (var i = 0; i < n; i++) { il[2 * i] = l[i]; il[2 * i + 1] = r[i]; }
        S.enqueue(il);
      };
      S.proc.connect(S.reader);
      S.reader.connect(S.sink);
      S.via = 'script';
    },

    // Reads the bus on the audio thread, so nothing is lost while the page is busy. Pages whose
    // security policy doesn't allow a script from a blob: keep the reader above.
    startWorklet: function () {
      var ctx = S.ctx;
      if (!ctx.audioWorklet || typeof AudioWorkletNode === 'undefined' || typeof Blob === 'undefined') return;
      var code =
        'class SnagTap extends AudioWorkletProcessor {' +
        ' constructor () { super(); this.buf = new Float32Array(4096); this.n = 0; }' +
        ' process (inputs) {' +
        '  var inp = inputs[0], l = inp && inp[0], r = inp && (inp[1] || inp[0]), frames = l ? l.length : 128;' +
        '  for (var i = 0; i < frames; i++) {' +
        '   this.buf[this.n++] = l ? l[i] : 0; this.buf[this.n++] = l ? r[i] : 0;' +
        '   if (this.n === this.buf.length) { this.port.postMessage(this.buf, [this.buf.buffer]); this.buf = new Float32Array(4096); this.n = 0; }' +
        '  }' +
        '  return true;' +
        ' }' +
        '}' +
        'registerProcessor("snag-tap", SnagTap);';
      var url = null;
      try {
        url = URL.createObjectURL(new Blob([code], { type: 'application/javascript' }));
        ctx.audioWorklet.addModule(url).then(function () {
          var node = new AudioWorkletNode(ctx, 'snag-tap', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1],
                                                             channelCount: 2, channelCountMode: 'explicit', channelInterpretation: 'speakers' });
          node.port.onmessage = function (e) { S.enqueue(e.data); };
          // (both changes in one go, so no stretch of audio is read twice or not at all)
          S.proc.connect(node);
          node.connect(S.sink);
          S.reader.onaudioprocess = null;   // (first, and for sure: both reading would double the audio)
          try { S.proc.disconnect(S.reader); S.reader.disconnect(); } catch (e) {}
          S.reader = null;
          S.worklet = node;
          S.via = 'worklet';
        }).catch(function () {}).then(function () { try { URL.revokeObjectURL(url); } catch (e) {} });
      } catch (e) { try { if (url) URL.revokeObjectURL(url); } catch (e2) {} }
    },

    resume: function () {
      if (S.ctx && S.ctx.state !== 'running') { try { S.ctx.resume(); } catch (e) {} }
    },

    sameOrigin: function (el) {
      var src = el.currentSrc || el.src || '';
      if (!src || src.indexOf('blob:') === 0 || src.indexOf('data:') === 0 || src.indexOf('mediastream:') === 0) return true;
      if (el.crossOrigin) return true;
      try { return new URL(src, el.ownerDocument.location.href).origin === location.origin; } catch (e) { return false; }
    },

    // The page's document and those of the same-site frames in it (frames that are gone are dropped).
    docs: function () {
      var list = [document];
      for (var i = S.wins.length - 1; i >= 0; i--) {
        var d = null;
        try { var w = S.wins[i]; if (!w.closed && w.frameElement && w.frameElement.isConnected) d = w.document; } catch (e) {}
        if (d) list.push(d); else S.wins.splice(i, 1);
      }
      return list;
    },

    // Frames the browser didn't run this script in (WebKit only runs it in the page itself):
    // same-site ones are taken in here. Looking into a frame from another site throws - it stays out.
    adoptFrames: function () {
      var docs = S.docs();
      for (var i = 0; i < docs.length; i++) {
        var frames = [];
        try { frames = docs[i].querySelectorAll('iframe, frame'); } catch (e) {}
        for (var j = 0; j < frames.length; j++) {
          try {
            var w = frames[j].contentWindow;
            if (w && w.document && !w.__snagInstalled) { S.install(w); docs.push(w.document); }
          } catch (e) {}
        }
      }
    },

    remember: function (el) {
      if (!el || S.known.indexOf(el) >= 0) return;
      S.known.push(el);
      if (S.known.length > 64) S.known.shift();
    },

    mediaElements: function () {
      var list = [], docs = S.docs();
      for (var d = 0; d < docs.length; d++) {
        try { list = list.concat(Array.prototype.slice.call(docs[d].querySelectorAll('video, audio'))); } catch (e) {}
      }
      for (var i = 0; i < S.known.length; i++) if (list.indexOf(S.known[i]) < 0) list.push(S.known[i]);
      return list;
    },

    detach: function (el) {
      for (var i = S.taps.length - 1; i >= 0; i--) {
        if (S.taps[i].el !== el) continue;
        try { if (S.taps[i].src) S.taps[i].src.disconnect(); } catch (e) {}
        S.taps.splice(i, 1);
      }
    },

    attach: function (el) {
      if (!S.armed || !el) return;
      if (el.__snagViaWebAudio) return;   // the page plays it through Web Audio: captured at its speakers
      for (var i = 0; i < S.taps.length; i++) if (S.taps[i].el === el) return;
      var ctx = S.ensureCtx(); if (!ctx) return;

      var capture = el.captureStream || el.mozCaptureStream;
      if (capture) {
        try {
          var stream = capture.call(el);
          var tracks = stream.getAudioTracks();
          var tap = { el: el, src: null, mode: 'stream', stream: stream, track: tracks[0] || null };
          if (tap.track) { tap.src = ctx.createMediaStreamSource(new MediaStream([tap.track])); tap.src.connect(S.proc); }
          S.taps.push(tap);
          S.unblock(el);
          return;
        } catch (e) {
          S.err = 'captureStream: ' + e;
          if (String(e).indexOf('cross-origin') >= 0 || (e && e.name === 'SecurityError')) { S.block(el); return; }
        }
      }

      // WebKit path: re-route the element through our context, only when it's safe.
      if (!S.sameOrigin(el)) { S.block(el); return; }
      if (ctx.state !== 'running') return;
      try {
        var src = ctx.createMediaElementSource(el);
        src.connect(ctx.destination);   // keep it audible
        src.connect(S.proc);
        S.taps.push({ el: el, src: src, mode: 'element', track: null });
      } catch (e) { S.err = 'mediaElementSource: ' + e; }
    },

    block: function (el) { if (S.blocked.indexOf(el) < 0) S.blocked.push(el); },
    unblock: function (el) { var i = S.blocked.indexOf(el); if (i >= 0) S.blocked.splice(i, 1); },
    blockedPlaying: function () {
      var n = 0;
      for (var i = 0; i < S.blocked.length; i++) { var el = S.blocked[i]; if (!el.paused && !el.ended) n++; }
      return n;
    },

    // Direct links to the audio / video files this page is playing or has loaded, best first.
    // HQ SNAG falls back to these on sites its downloader doesn't support.
    mediaUrls: function () {
      var out = [];
      function add(u) {
        if (!u || typeof u !== 'string') return;
        if (u.indexOf('http') !== 0) return;               // blob: / data: / mediastream: can't be fetched
        if (out.indexOf(u) < 0) out.push(u);
      }
      var els = S.mediaElements(), i;
      for (i = 0; i < els.length; i++) if (!els[i].paused && !els[i].ended) add(els[i].currentSrc || els[i].src);
      for (i = S.known.length - 1; i >= 0; i--) add(S.known[i].currentSrc || S.known[i].src);
      for (i = 0; i < els.length; i++) add(els[i].currentSrc || els[i].src);
      try {
        var res = performance.getEntriesByType('resource');
        var ext = /\.(mp3|wav|wave|ogg|oga|opus|m4a|aac|flac|weba|webm|mp4|aif|aiff)(\?|#|$)/i;
        for (i = res.length - 1; i >= 0 && out.length < 12; i--) {
          var r = res[i];
          if (r.initiatorType === 'audio' || r.initiatorType === 'video' || ext.test(r.name)) add(r.name);
        }
      } catch (e) {}
      return JSON.stringify(out.slice(0, 12));
    },

    refreshStreams: function () {
      for (var i = S.taps.length - 1; i >= 0; i--) {
        var t = S.taps[i];
        if (t.mode !== 'stream') continue;
        if (t.el.__snagViaWebAudio) { S.detach(t.el); continue; }
        var tr = t.stream.getAudioTracks()[0] || null;
        if (tr !== t.track || (tr && tr.readyState === 'ended')) {
          try { if (t.src) t.src.disconnect(); } catch (e) {}
          t.src = null; t.track = null;
          try {
            var fresh = (t.el.captureStream || t.el.mozCaptureStream).call(t.el);
            t.stream = fresh;
            t.track = fresh.getAudioTracks()[0] || null;
            if (t.track) { t.src = S.ctx.createMediaStreamSource(new MediaStream([t.track])); t.src.connect(S.proc); }
          } catch (e) {
            S.err = 'refresh: ' + e;
            // the file turned out to come from another site that doesn't allow listening
            if (String(e).indexOf('cross-origin') >= 0 || (e && e.name === 'SecurityError')) { S.taps.splice(i, 1); S.block(t.el); }
          }
        }
      }
    },

    // ---- Web Audio ------------------------------------------------------------------------
    pageTapFor: function (pctx, create) {
      for (var i = 0; i < S.pageTaps.length; i++) if (S.pageTaps[i].ctx === pctx) return S.pageTaps[i];
      if (!create || !pctx || pctx === S.ctx || typeof pctx.createMediaStreamDestination !== 'function') return null;   // e.g. OfflineAudioContext
      var t = { ctx: pctx, node: null, src: null };
      try { t.node = pctx.createMediaStreamDestination(); } catch (e) { S.err = 'webaudio: ' + e; return null; }
      S.pageTaps.push(t);
      S.hookPageTap(t);
      return t;
    },

    hookPageTap: function (t) {
      if (!S.armed || t.src || !t.node) return;
      if (t.ctx.state === 'closed') return;
      var ctx = S.ensureCtx(); if (!ctx) return;
      try { t.src = ctx.createMediaStreamSource(t.node.stream); t.src.connect(S.proc); } catch (e) { S.err = 'webaudio: ' + e; }
    },

    activeTaps: function () {
      var n = S.taps.length;
      for (var i = 0; i < S.pageTaps.length; i++) if (S.pageTaps[i].src && S.pageTaps[i].ctx.state !== 'closed') n++;
      return n;
    },

    // Media the page has thrown away (the last track, an ad, a feed that scrolled on) is let go:
    // its tap would otherwise stay connected for as long as the page lives.
    // Only taps on a stream, though. An element that was re-routed through our context (WebKit)
    // plays through that one node for good - letting go of it would leave the element silent if
    // the page ever brings it back.
    prune: function (els) {
      var i, el;
      for (i = S.taps.length - 1; i >= 0; i--) {
        el = S.taps[i].el;
        if (S.taps[i].mode === 'stream' && els.indexOf(el) < 0 && (el.paused || el.ended)) S.detach(el);
      }
      for (i = S.blocked.length - 1; i >= 0; i--) {
        el = S.blocked[i];
        if (els.indexOf(el) < 0 && (el.paused || el.ended)) S.blocked.splice(i, 1);
      }
    },

    scan: function () {
      if (!S.armed) return 0;
      S.adoptFrames();
      var els = S.mediaElements();
      S.prune(els);
      for (var i = 0; i < els.length; i++) S.attach(els[i]);
      for (var j = 0; j < S.pageTaps.length; j++) S.hookPageTap(S.pageTaps[j]);
      return S.activeTaps();
    },

    arm: function (on) {
      S.armed = !!on;
      if (S.armed) { S.ensureCtx(); S.resume(); S.scan(); }
      else { S.q = []; S.qFrames = 0; }
      return S.armed;
    },

    playing: function () {
      var els = S.mediaElements();
      for (var i = 0; i < els.length; i++) if (!els[i].paused && !els[i].ended && els[i].readyState > 2) return true;
      return S.level > 0.002;   // Web Audio players have no element to ask
    },

    b64: function (f32) {
      var bytes = new Uint8Array(f32.buffer, f32.byteOffset, f32.byteLength);
      var parts = [], CH = 0x8000;
      for (var i = 0; i < bytes.length; i += CH)
        parts.push(String.fromCharCode.apply(null, bytes.subarray(i, Math.min(bytes.length, i + CH))));
      return btoa(parts.join(''));
    },

    drain: function () {
      if (S.armed) { S.scan(); if (S.ctx) S.refreshStreams(); }
      var out = { sr: S.ctx ? S.ctx.sampleRate : 0, n: 0, b64: '', lvl: S.level,
                  st: S.ctx ? S.ctx.state : 'none', taps: S.activeTaps(), playing: S.playing(), err: S.err,
                  blocked: S.blockedPlaying(), via: S.via, frames: S.wins.length };
      if (S.q.length) {
        var all = new Float32Array(S.qFrames * 2), off = 0;
        for (var i = 0; i < S.q.length; i++) { all.set(S.q[i], off); off += S.q[i].length; }
        out.n = S.qFrames;
        out.b64 = S.b64(all);
        S.q = []; S.qFrames = 0;
      }
      S.level *= 0.7;
      S.err = '';
      return JSON.stringify(out);
    },

    current: function () {
      // the element that is actually playing wins; otherwise the first video / audio
      var els = S.mediaElements();
      for (var i = 0; i < els.length; i++) if (!els[i].paused && !els[i].ended) return els[i];
      return document.querySelector('video') || document.querySelector('audio');
    },

    info: function () {
      var v = S.current();
      var title = document.title || '';
      var meta = document.querySelector('meta[property="og:title"]');
      if (meta && meta.content) title = meta.content;
      var media = [], playingMedia = [];
      try { media = JSON.parse(S.mediaUrls()); } catch (e) {}
      var els = S.mediaElements();
      for (var i = 0; i < els.length; i++) {
        var u = els[i].currentSrc || els[i].src || '';
        if (!els[i].paused && !els[i].ended && u.indexOf('http') === 0 && playingMedia.indexOf(u) < 0) playingMedia.push(u);
      }
      return JSON.stringify({ url: location.href, title: title,
                              time: v ? v.currentTime : -1, duration: v ? v.duration : -1,
                              paused: v ? v.paused : true, hasMedia: !!v, media: media, playingMedia: playingMedia });
    },

    pauseAll: function () {
      var els = S.mediaElements();
      for (var i = 0; i < els.length; i++) { try { els[i].pause(); } catch (e) {} }
      return true;
    },

    seek: function (t) {
      var v = S.current();
      if (v) { v.currentTime = t; return true; }
      return false;
    }
  };

  // ---- hooks into a window's audio APIs: the page's own, and each same-site frame's -----------
  function looksNative (fn, name) {
    try { fn.toString = function () { return 'function ' + name + '() { [native code] }'; }; } catch (e) {}
    return fn;
  }
  function snag () { return S; }

  S.install = function (win) {
    try {
      if (!win || win.__snagInstalled) return;
      Object.defineProperty(win, '__snagInstalled', { value: true });
    } catch (e) { return; }
    if (win !== window && S.wins.indexOf(win) < 0) S.wins.push(win);

    function isSpeakers (node) {
      return typeof win.AudioDestinationNode !== 'undefined' && node instanceof win.AudioDestinationNode;
    }

    try {
      var AN = win.AudioNode;
      if (AN && AN.prototype && !AN.prototype.__snagHooked) {
        Object.defineProperty(AN.prototype, '__snagHooked', { value: true });
        var connect = AN.prototype.connect, disconnect = AN.prototype.disconnect;

        AN.prototype.connect = looksNative(function (dest) {
          var result = connect.apply(this, arguments);
          try {
            var s = snag();
            if (s && isSpeakers(dest) && this.context !== s.ctx) {
              var t = s.pageTapFor(this.context, true);
              if (t && t.node) connect.call(this, t.node);
            }
          } catch (e) {}
          return result;
        }, 'connect');

        AN.prototype.disconnect = looksNative(function (dest) {
          try {
            var s = snag();
            if (s && arguments.length && isSpeakers(dest) && this.context !== s.ctx) {
              var t = s.pageTapFor(this.context, false);
              if (t && t.node) disconnect.call(this, t.node);
            }
          } catch (e) {}
          return disconnect.apply(this, arguments);
        }, 'disconnect');
      }

      // media the page plays through Web Audio is captured at the speakers instead
      // (createMediaElementSource lives on AudioContext, not BaseAudioContext)
      [win.AudioContext, win.webkitAudioContext].forEach(function (AC) {
        if (!AC || !AC.prototype || !AC.prototype.createMediaElementSource) return;
        if (Object.prototype.hasOwnProperty.call(AC.prototype, '__snagHooked')) return;
        Object.defineProperty(AC.prototype, '__snagHooked', { value: true });
        var cmes = AC.prototype.createMediaElementSource;
        AC.prototype.createMediaElementSource = looksNative(function (el) {
          var s = snag();
          if (s && el && this !== s.ctx) {
            try { el.__snagViaWebAudio = true; s.detach(el); } catch (e) {}
          }
          return cmes.apply(this, arguments);
        }, 'createMediaElementSource');
      });

      // media elements that are never added to the document (new Audio(), players' hidden elements)
      var HME = win.HTMLMediaElement;
      if (HME && HME.prototype && !HME.prototype.__snagHooked) {
        Object.defineProperty(HME.prototype, '__snagHooked', { value: true });
        var play = HME.prototype.play;
        HME.prototype.play = looksNative(function () {
          try {
            var s = snag();
            if (s) { s.remember(this); if (s.armed) { s.resume(); s.attach(this); } }
          } catch (e) {}
          return play.apply(this, arguments);
        }, 'play');
      }
    } catch (e) { S.err = 'hooks: ' + e; }

    try {
      var doc = win.document;
      // Any real user gesture on the page lets us start the AudioContext.
      ['pointerdown', 'mousedown', 'keydown', 'touchstart'].forEach(function (ev) {
        doc.addEventListener(ev, function () { var s = snag(); if (s && s.armed) { s.ensureCtx(); s.resume(); } }, true);
      });
      // Pick up media as soon as it starts playing.
      doc.addEventListener('play', function (e) { var s = snag(); if (s && s.armed) { s.resume(); s.attach(e.target); } }, true);
      doc.addEventListener('playing', function (e) { var s = snag(); if (s && s.armed) s.attach(e.target); }, true);
    } catch (e) {}
  };

  S.install(window);

  return 'ok';
})();
