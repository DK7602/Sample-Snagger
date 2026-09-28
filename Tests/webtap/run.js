// Checks that webtap.js hears every way a page can play sound, at the right level (no silence,
// no double capture). Needs Node + Playwright:
//   npm i -D playwright && npx playwright install chromium && node Tests/webtap/run.js
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const cases = {
  wa:   { what: 'Web Audio player (oscillator -> gain -> speakers)', peak: 0.5 },
  det:  { what: 'audio element never added to the page (new Audio())', peak: 0.366 },
  mes:  { what: 'audio element played through the page\'s Web Audio', peak: 0.366 },
  dom:  { what: '<audio> element in the page', peak: 0.366 },
  none: { what: 'nothing playing', peak: 0 },
};

(async () => {
  const tap = fs.readFileSync(path.join(__dirname, '../../Resources/Scripts/webtap.js'), 'utf8');
  const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] });
  let failed = 0;
  for (const [mode, c] of Object.entries(cases)) {
    for (let run = 0; run < (mode === 'mes' ? 4 : 1); run++) {
      const page = await browser.newPage();
      page.on('pageerror', e => { console.log('PAGE ERROR', mode, e.message); failed++; });
      await page.addInitScript(tap);   // like WebView2's AddScriptToExecuteOnDocumentCreated
      await page.goto('file://' + path.join(__dirname, 'page.html'));
      await page.evaluate(() => window.__snag.arm(true));
      if (mode !== 'none') await page.click('#' + mode);
      await page.waitForTimeout(400);
      await page.evaluate(() => window.__snag.drain());   // discard warm-up
      await page.waitForTimeout(1500);
      const r = JSON.parse(await page.evaluate(() => window.__snag.drain()));
      const buf = Buffer.from(r.b64, 'base64');
      const f = new Float32Array(buf.buffer, buf.byteOffset, buf.length / 4);
      let pk = 0; for (let i = 0; i < f.length; i++) pk = Math.max(pk, Math.abs(f[i]));
      const ok = c.peak === 0 ? pk < 0.001 : Math.abs(pk - c.peak) < 0.06;
      if (!ok) failed++;
      console.log(`${ok ? 'ok  ' : 'FAIL'}  ${c.what}: peak ${pk.toFixed(3)} (expected ${c.peak})${r.err ? '  err: ' + r.err : ''}`);
      await page.close();
    }
  }
  await browser.close();
  console.log(failed ? `${failed} failed` : 'all passed');
  process.exit(failed ? 1 : 0);
})();
