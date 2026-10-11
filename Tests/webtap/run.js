// Checks that webtap.js hears every way a page can play sound, at the right level (no silence,
// no double capture), and that sounds it may not hear (cross-origin files) are reported with a
// direct link HQ SNAG can use instead. Needs Node + Playwright:
//   npm i -D playwright && npx playwright install chromium && node Tests/webtap/run.js
const { chromium } = require('playwright');
const fs = require('fs');
const http = require('http');
const path = require('path');

function toneWav (freq, secs) {
  const sr = 44100, n = sr * secs, b = Buffer.alloc(44 + n * 2);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 2, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16);
  b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22); b.writeUInt32LE(sr, 24); b.writeUInt32LE(sr * 2, 28);
  b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 2, 40);
  for (let i = 0; i < n; i++) b.writeInt16LE(Math.round(Math.sin(2 * Math.PI * freq * i / sr) * 12000), 44 + i * 2);
  return b;
}

const cases = {
  wa:   { what: 'Web Audio player (oscillator -> gain -> speakers)', peak: 0.5 },
  frame:   { what: 'audio element in a frame of the same site', peak: 0.366, frames: 1 },
  framewa: { what: 'Web Audio player in a frame of the same site', peak: 0.5, frames: 1 },
  xframe:  { what: 'player in a frame from another site: left alone', peak: 0, frames: 0 },
  det:  { what: 'audio element never added to the page (new Audio())', peak: 0.366 },
  mes:  { what: 'audio element played through the page\'s Web Audio', peak: 0.366 },
  dom:  { what: '<audio> element in the page', peak: 0.366 },
  xo:   { what: 'file from another server (ZapSplat style): reported + direct link', peak: 0, blocked: true },
  none: { what: 'nothing playing', peak: 0 },
};

(async () => {
  const page1 = fs.readFileSync(path.join(__dirname, 'page.html'));
  const frame1 = fs.readFileSync(path.join(__dirname, 'frame.html'));
  const tone = toneWav(440, 4);
  // site A: the page (also at /strict, with a security policy that allows no scripts from a blob:) and its frame
  const siteA = http.createServer((q, r) => {
    const headers = { 'Content-Type': 'text/html' };
    if (q.url.indexOf('/strict') === 0) headers['Content-Security-Policy'] = "script-src 'self' 'unsafe-inline'";
    r.writeHead(200, headers);
    r.end(q.url.indexOf('/frame.html') === 0 ? frame1 : page1);
  }).listen(18731);
  // site B: another site - a sound file, and a frame with a player in it
  const siteB = http.createServer((q, r) => {
    if (q.url.indexOf('/frame.html') === 0) { r.writeHead(200, { 'Content-Type': 'text/html' }); r.end(frame1); return; }
    r.writeHead(200, { 'Content-Type': 'audio/wav' }); r.end(tone);
  }).listen(18732);

  const tap = fs.readFileSync(path.join(__dirname, '../../Resources/Scripts/webtap.js'), 'utf8');
  const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] });
  let failed = 0;
  for (const [mode, c] of Object.entries(cases)) {
    for (let run = 0; run < (mode === 'mes' ? 4 : 1); run++) {
      const page = await browser.newPage();
      page.on('pageerror', e => { console.log('PAGE ERROR', mode, e.message); failed++; });
      await page.addInitScript(tap);   // like WebView2's AddScriptToExecuteOnDocumentCreated
      await page.goto('http://localhost:18731/');
      await page.evaluate(() => window.__snag.arm(true));
      if (mode !== 'none') await page.click('#' + mode);
      await page.waitForTimeout(400);
      await page.evaluate(() => window.__snag.drain());   // discard warm-up
      await page.waitForTimeout(1500);
      const r = JSON.parse(await page.evaluate(() => window.__snag.drain()));
      const info = JSON.parse(await page.evaluate(() => window.__snag.info()));
      const buf = Buffer.from(r.b64, 'base64');
      const f = new Float32Array(buf.buffer, buf.byteOffset, buf.length / 4);
      let pk = 0; for (let i = 0; i < f.length; i++) pk = Math.max(pk, Math.abs(f[i]));
      let ok = c.peak === 0 ? pk < 0.001 : Math.abs(pk - c.peak) < 0.06;
      let extra = '';
      if (c.blocked) {
        const link = (info.media || []).find(u => u.indexOf(':18732/tone.wav') > 0);
        ok = ok && r.blocked === 1 && !!link;
        extra = `  blocked ${r.blocked}, link ${link ? 'found' : 'MISSING'}`;
      } else if (r.blocked) { ok = false; extra = `  unexpected blocked ${r.blocked}`; }
      if (c.frames !== undefined) { ok = ok && r.frames === c.frames; extra += `  (${r.frames} frame${r.frames === 1 ? '' : 's'} joined)`; }
      if (r.via !== 'worklet') { ok = false; extra += `  read via ${r.via}, not the worklet`; }
      if (!ok) failed++;
      console.log(`${ok ? 'ok  ' : 'FAIL'}  ${c.what}: peak ${pk.toFixed(3)} (expected ${c.peak})${extra}`);
      await page.close();
    }
  }
  // A page that keeps making and dropping players (a feed, ads, track after track) must not leave
  // a tap behind for each of them.
  {
    const page = await browser.newPage();
    page.on('pageerror', e => { console.log('PAGE ERROR feed', e.message); failed++; });
    await page.addInitScript(tap);
    await page.goto('http://localhost:18731/');
    await page.evaluate(() => window.__snag.arm(true));
    await page.click('#feed');
    let most = 0;
    for (let i = 0; i < 400 && !(await page.evaluate(() => !!window.feedDone)); i++) {
      most = Math.max(most, JSON.parse(await page.evaluate(() => window.__snag.drain())).taps);   // polled, like the plug-in does
      await page.waitForTimeout(125);
    }
    await page.waitForTimeout(300);
    const r = JSON.parse(await page.evaluate(() => window.__snag.drain()));
    const ok = most > 0 && r.taps <= 70;
    if (!ok) failed++;
    console.log(`${ok ? 'ok  ' : 'FAIL'}  150 players come and go: ${r.taps} taps left (at most 70), ${most} at the busiest`);
    await page.close();
  }

  // What comes back for one way of loading the page: its peak, how much audio, and how smooth it is.
  async function listen (label, { url = 'http://localhost:18731/', everyFrame = true, click, seconds = 1.5 }) {
    const page = await browser.newPage();
    page.on('pageerror', e => { console.log('PAGE ERROR', label, e.message); failed++; });
    if (everyFrame) await page.addInitScript(tap);
    await page.goto(url);
    if (!everyFrame) await page.evaluate(tap);   // WebKit: the script only runs in the page itself, and after it loaded
    await page.evaluate(() => window.__snag.arm(true));
    if (click) await page.click('#' + click);
    await page.waitForTimeout(600);
    await page.evaluate(() => window.__snag.drain());   // discard warm-up
    const t0 = Date.now();
    await page.waitForTimeout(seconds * 1000);
    const r = JSON.parse(await page.evaluate(() => window.__snag.drain()));
    const elapsed = (Date.now() - t0) / 1000;
    const buf = Buffer.from(r.b64, 'base64');
    const f = new Float32Array(buf.buffer, buf.byteOffset, buf.length / 4);
    let pk = 0, jumps = 0;
    for (let i = 0; i < f.length; i++) pk = Math.max(pk, Math.abs(f[i]));
    // A sine this slow never moves this far in one step: a jump means audio went missing. (Not
    // counted: going into or out of plain silence - that is the player starting, or running dry.)
    for (let i = 4; i + 2 < f.length; i += 2) {
      const silentBefore = f[i - 2] === 0 && f[i - 4] === 0, silentAfter = f[i] === 0 && f[i + 2] === 0;
      if (Math.abs(f[i] - f[i - 2]) > 0.1 && !silentBefore && !silentAfter) jumps++;
    }
    await page.close();
    return { r, pk, jumps, heard: r.n / r.sr / elapsed };
  }

  // WebKit runs the script in the page only: a same-site frame is found and joined from there.
  {
    const x = await listen('page-only', { everyFrame: false, click: 'frame' });
    const ok = Math.abs(x.pk - 0.366) < 0.06 && x.r.frames === 1;
    if (!ok) failed++;
    console.log(`${ok ? 'ok  ' : 'FAIL'}  frame of the same site, script in the page only (WebKit): peak ${x.pk.toFixed(3)} (expected 0.366), ${x.r.frames} frame joined`);
  }
  // A page whose security policy keeps the worklet out still gets heard, the old way.
  {
    const x = await listen('strict', { url: 'http://localhost:18731/strict', click: 'dom' });
    const ok = Math.abs(x.pk - 0.366) < 0.06 && x.r.via === 'script';
    if (!ok) failed++;
    console.log(`${ok ? 'ok  ' : 'FAIL'}  page with a strict security policy: peak ${x.pk.toFixed(3)} (expected 0.366), read via ${x.r.via}`);
  }
  // A page that keeps its own thread busy while it plays: nothing may be lost.
  {
    const x = await listen('busy', { click: 'busy', seconds: 3 });
    const old = await listen('busy-strict', { url: 'http://localhost:18731/strict', click: 'busy', seconds: 3 });
    const ok = x.r.via === 'worklet' && x.heard > 0.85 && x.heard < 1.25 && x.jumps === 0 && Math.abs(x.pk - 0.366) < 0.06;
    if (!ok) failed++;
    console.log(`${ok ? 'ok  ' : 'FAIL'}  busy page: ${(x.heard * 100).toFixed(0)}% of the audio arrived with ${x.jumps} breaks in it`
                + ` (the old way: ${(old.heard * 100).toFixed(0)}% with ${old.jumps} breaks)`);
  }

  await browser.close();
  siteA.close(); siteB.close();
  console.log(failed ? `${failed} failed` : 'all passed');
  process.exit(failed ? 1 : 0);
})();
