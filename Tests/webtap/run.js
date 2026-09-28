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
  det:  { what: 'audio element never added to the page (new Audio())', peak: 0.366 },
  mes:  { what: 'audio element played through the page\'s Web Audio', peak: 0.366 },
  dom:  { what: '<audio> element in the page', peak: 0.366 },
  xo:   { what: 'file from another server (ZapSplat style): reported + direct link', peak: 0, blocked: true },
  none: { what: 'nothing playing', peak: 0 },
};

(async () => {
  const page1 = fs.readFileSync(path.join(__dirname, 'page.html'));
  const tone = toneWav(440, 4);
  const siteA = http.createServer((q, r) => { r.writeHead(200, { 'Content-Type': 'text/html' }); r.end(page1); }).listen(18731);
  const siteB = http.createServer((q, r) => { r.writeHead(200, { 'Content-Type': 'audio/wav' }); r.end(tone); }).listen(18732);

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
      if (!ok) failed++;
      console.log(`${ok ? 'ok  ' : 'FAIL'}  ${c.what}: peak ${pk.toFixed(3)} (expected ${c.peak})${extra}`);
      await page.close();
    }
  }
  await browser.close();
  siteA.close(); siteB.close();
  console.log(failed ? `${failed} failed` : 'all passed');
  process.exit(failed ? 1 : 0);
})();
