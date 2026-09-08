// Copyright 2026 RRRTX Labs — MPL-2.0. See LICENSE.
//
// CSP lint on the BUILT bundle (P7). The output must survive
// `script-src 'self'; object-src 'none'; base-uri 'none'` — i.e. NO inline JS,
// no eval/dynamic-Function, no runtime network, no sourceMappingURL. A hit is a
// BUILD-FAIL, not advice (security req). Source-level patterns are also caught
// by xr-browser tools/csp_lint.py; this catches what the minifier emits.
import { readFileSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const dist = join(here, 'dist');
let fails = [];

const bundle = join(dist, 'bundle.js');
const html = join(dist, 'index.html');

if (!existsSync(bundle)) fails.push('dist/bundle.js missing (run build first)');
if (!existsSync(html)) fails.push('dist/index.html missing (run build first)');

if (existsSync(bundle)) {
  const js = readFileSync(bundle, 'utf8');
  if (/\beval\s*\(/.test(js)) fails.push('bundle.js: eval( — forbidden (CSP script-src)');
  if (/\bnew\s+Function\s*\(/.test(js)) fails.push('bundle.js: new Function( — forbidden');
  if (/sourceMappingURL/.test(js)) fails.push('bundle.js: sourceMappingURL present (must be off in prod)');
  if (/\bfetch\s*\(|\bXMLHttpRequest\b|\bWebSocket\b/.test(js))
    fails.push('bundle.js: runtime network API (fetch/XHR/WebSocket) — no runtime egress');
}
if (existsSync(html)) {
  // Strip HTML comments first — a `<script>` mentioned in a comment is not
  // executable and must not be flagged (false-positive guard).
  const h = readFileSync(html, 'utf8').replace(/<!--[\s\S]*?-->/g, '');
  // An inline <script> is any <script ...> that is NOT purely <script src=...></script>.
  if (/<script(?![^>]*\bsrc=)[^>]*>[^<\s]/.test(h))
    fails.push('index.html: inline <script> body — forbidden (CSP)');
  if (/\bon[a-z]+\s*=\s*['"]/.test(h))
    fails.push('index.html: inline event handler attribute (on*="...") — forbidden');
  if (!/<script[^>]*\bsrc=["']bundle\.js["']/.test(h))
    fails.push('index.html: bundle.js is not loaded as an external script');
}

if (fails.length) {
  for (const f of fails) console.error('FAIL ' + f);
  console.error(`check-bundle: ${fails.length} violation(s) (FAIL)`);
  process.exit(1);
}
console.log('check-bundle: PASS (CSP-strict: no inline JS/eval, no runtime network, no sourcemap)');
