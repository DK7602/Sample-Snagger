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
(function () {
  var VERSION = 4;
  if (window.__snag && window.__snag.version === VERSION) return 'ok';

  var S = window.__snag = {
    version: VERSION,
    armed: false,
    ctx: null,
    proc: null,
    sink: null,
    taps: [],            // media element taps: { el, src, mode, stream, track }
    known: [],           // media elements seen via play(), including ones not in the document
    pageTaps: [],        // Web Audio taps: { ctx, node, src }
    q: [],               // queued Float32Array chunks (interleaved stereo)
    qFrames: 0,
    maxFrames: 48000 * 10,
    level: 0,
    err: '',

    ensureCtx: function () {
      if (S.ctx) return S.ctx;
      var AC = window.AudioContext || window.webkitAudioContext;
      if (!AC) { S.err = 'Web Audio not supported'; return null; }
      try { S.ctx = new AC({ latencyHint: 'playback' }); } catch (e) { S.ctx = new AC(); }
      S.proc = S.ctx.createScriptProcessor(4096, 2, 2);
      S.proc.onaudioprocess = function (e) {
        var ib = e.inputBuffer, n = ib.length;
        var out = e.outputBuffer;
        for (var c = 0; c < out.numberOfChannels; c++) out.getChannelData(c).fill(0);
        if (!S.armed) return;
        var l = ib.getChannelData(0), r = ib.numberOfChannels > 1 ? ib.getChannelData(1) : l;
        var il = new Float32Array(n * 2), pk = 0;
        for (var i = 0; i < n; i++) {
          var a = l[i], b = r[i];
          il[2 * i] = a; il[2 * i + 1] = b;
          var m = a < 0 ? -a : a; if (m > pk) pk = m;
          m = b < 0 ? -b : b; if (m > pk) pk = m;
        }
        S.level = Math.max(pk, S.level * 0.85);
        S.q.push(il); S.qFrames += n;
        while (S.qFrames > S.maxFrames && S.q.length) { S.qFrames -= S.q[0].length / 2; S.q.shift(); }
      };
      // keep the processor pulling audio without making any sound of its own
      S.sink = S.ctx.createGain();
      S.sink.gain.value = 0;
      S.proc.connect(S.sink);
      S.sink.connect(S.ctx.destination);
      S.ctx.onstatechange = function () { if (S.ctx.state === 'running') S.scan(); };
      return S.ctx;
    },

    resume: function () {
      if (S.ctx && S.ctx.state !== 'running') { try { S.ctx.resume(); } catch (e) {} }
    },

    sameOrigin: function (el) {
      var src = el.currentSrc || el.src || '';
      if (!src || src.indexOf('blob:') === 0 || src.indexOf('data:') === 0 || src.indexOf('mediastream:') === 0) return true;
      if (el.crossOrigin) return true;
      try { return new URL(src, location.href).origin === location.origin; } catch (e) { return false; }
    },

    remember: function (el) {
      if (!el || S.known.indexOf(el) >= 0) return;
      S.known.push(el);
      if (S.known.length > 64) S.known.shift();
    },

    mediaElements: function () {
      var list = [];
      try { list = Array.prototype.slice.call(document.querySelectorAll('video, audio')); } catch (e) {}
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
          return;
        } catch (e) { S.err = 'captureStream: ' + e; }
      }

      // WebKit path: re-route the element through our context, only when it's safe.
      if (ctx.state !== 'running' || !S.sameOrigin(el)) return;
      try {
        var src = ctx.createMediaElementSource(el);
        src.connect(ctx.destination);   // keep it audible
        src.connect(S.proc);
        S.taps.push({ el: el, src: src, mode: 'element', track: null });
      } catch (e) { S.err = 'mediaElementSource: ' + e; }
    },

    refreshStreams: function () {
      for (var i = 0; i < S.taps.length; i++) {
        var t = S.taps[i];
        if (t.mode !== 'stream') continue;
        var tr = t.stream.getAudioTracks()[0] || null;
        if (tr !== t.track || (tr && tr.readyState === 'ended')) {
          try { if (t.src) t.src.disconnect(); } catch (e) {}
          t.src = null; t.track = null;
          try {
            var fresh = (t.el.captureStream || t.el.mozCaptureStream).call(t.el);
            t.stream = fresh;
            t.track = fresh.getAudioTracks()[0] || null;
            if (t.track) { t.src = S.ctx.createMediaStreamSource(new MediaStream([t.track])); t.src.connect(S.proc); }
          } catch (e) { S.err = 'refresh: ' + e; }
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

    scan: function () {
      if (!S.armed) return 0;
      var els = S.mediaElements();
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
                  st: S.ctx ? S.ctx.state : 'none', taps: S.activeTaps(), playing: S.playing(), err: S.err };
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
      return JSON.stringify({ url: location.href, title: title,
                              time: v ? v.currentTime : -1, duration: v ? v.duration : -1,
                              paused: v ? v.paused : true, hasMedia: !!v });
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

  // ---- hooks into the page's audio APIs (installed once per document) -----------------------
  function looksNative (fn, name) {
    try { fn.toString = function () { return 'function ' + name + '() { [native code] }'; }; } catch (e) {}
    return fn;
  }
  function snag () { return window.__snag; }
  function isSpeakers (node) {
    return typeof AudioDestinationNode !== 'undefined' && node instanceof AudioDestinationNode;
  }

  try {
    var AN = window.AudioNode;
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
    [window.AudioContext, window.webkitAudioContext].forEach(function (AC) {
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
    var HME = window.HTMLMediaElement;
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

  // Any real user gesture on the page lets us start the AudioContext.
  ['pointerdown', 'mousedown', 'keydown', 'touchstart'].forEach(function (ev) {
    document.addEventListener(ev, function () { var s = snag(); if (s && s.armed) { s.ensureCtx(); s.resume(); } }, true);
  });
  // Pick up media as soon as it starts playing.
  document.addEventListener('play', function (e) { var s = snag(); if (s && s.armed) { s.resume(); s.attach(e.target); } }, true);
  document.addEventListener('playing', function (e) { var s = snag(); if (s && s.armed) s.attach(e.target); }, true);

  return 'ok';
})();
