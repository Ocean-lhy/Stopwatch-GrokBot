import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import puppeteer from 'puppeteer-core';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../..');
const framesRoot = path.join(root, 'build/grokbot-frames');
const recipes = JSON.parse(fs.readFileSync(path.join(root, 'third_party/grokbot/grok-recipes.json'), 'utf8'));
const states = recipes.lifecycleGroups.flatMap(group => group.states);
const fps = 30;
const size = 400;
const chrome = process.env.CHROME_BIN || '/usr/bin/google-chrome';
const mime = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
};

const server = http.createServer((req, res) => {
  const pathname = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
  const file = path.resolve(root, `.${pathname === '/' ? '/tools/grokbot/render.html' : pathname}`);
  if (!file.startsWith(root)) {
    res.writeHead(403).end();
    return;
  }
  fs.readFile(file, (error, data) => {
    if (error) {
      res.writeHead(404).end();
      return;
    }
    res.writeHead(200, { 'Content-Type': mime[path.extname(file)] || 'application/octet-stream' });
    res.end(data);
  });
});

await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const port = server.address().port;
fs.rmSync(framesRoot, { recursive: true, force: true });
fs.mkdirSync(framesRoot, { recursive: true });

const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ['--no-sandbox', '--disable-gpu'],
});
const page = await browser.newPage();
await page.setViewport({ width: size, height: size, deviceScaleFactor: 1 });
const stateSummary = [];

try {
  for (const state of states) {
    const out = path.join(framesRoot, state);
    fs.mkdirSync(out, { recursive: true });
    await page.goto(`http://127.0.0.1:${port}/tools/grokbot/render.html?state=${state}&size=${size}&seed=stopwatch`, {
      waitUntil: 'networkidle0',
    });
    await page.waitForFunction('window.bakerReady === true');
    const snapshot = await page.evaluate(() => window.bot.debugFrame());
    const count = Math.max(1, Math.round(snapshot.duration * fps));
    for (let i = 0; i < count; ++i) {
      const png = await page.screenshot({
        format: 'png',
        omitBackground: true,
        clip: { x: 0, y: 0, width: size, height: size },
      });
      fs.writeFileSync(path.join(out, `${String(i).padStart(4, '0')}.png`), png);
      if (i + 1 < count) await page.evaluate(() => window.bot.step(2));
    }
    const item = { state, clip: snapshot.clip, duration: snapshot.duration, count, fps };
    stateSummary.push(item);
    console.log(`${state.padEnd(10)} ${snapshot.clip.padEnd(12)} ${count} frames`);
  }


} finally {
  await browser.close();
  server.close();
}

fs.writeFileSync(path.join(framesRoot, 'summary.json'), JSON.stringify({ fps, states: stateSummary }, null, 2));
const result = spawnSync('python3', [
  path.join(here, 'encode_atlas.py'),
  framesRoot,
  path.join(root, 'main/grokbot.atlas'),
], { stdio: 'inherit' });
if (result.status !== 0) process.exit(result.status ?? 1);
