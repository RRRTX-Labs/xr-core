// Copyright 2026 RRRTX Labs — MPL-2.0. See LICENSE.
//
// Reproducible WebUI bundle (P7). Determinism rules:
//   * fixed esbuild version (package-lock + integrity pin);
//   * no sourcemap in the prod bundle (no source paths, no timestamps);
//   * no banner/define that embeds a clock;
//   * esbuild output is a pure function of (inputs, config, version) — two
//     builds of the same tree are byte-identical (R1 rung preview; asserted by
//     xr-browser build/webui/repro-check.sh).
//
// The bundle is CSP-strict output: minified ESM, no eval, no dynamic URL
// imports, no sourceMappingURL. check-bundle.js re-scans the OUTPUT.
import * as esbuild from 'esbuild';
import { cpSync, mkdirSync, rmSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const ui = join(here, '..');
const dist = join(here, 'dist');

rmSync(dist, { recursive: true, force: true });
mkdirSync(dist, { recursive: true });

await esbuild.build({
  entryPoints: [join(ui, 'shell.ts')],
  bundle: true,
  outfile: join(dist, 'bundle.js'),
  format: 'esm',
  target: 'es2022',
  platform: 'browser',
  minify: true,
  sourcemap: false,          // prod: no source paths / no timestamps
  legalComments: 'none',     // keep the output a pure function of inputs
  define: { 'process.env.NODE_ENV': '"production"' },
  // The npm project root is ui/toolchain (sources live one level up in ui/),
  // so point esbuild at this node_modules explicitly.
  nodePaths: [join(here, 'node_modules')],
  logLevel: 'silent',
});

// Static assets ship alongside the bundle (integrity-pinned by the host glue).
cpSync(join(ui, 'index.html'), join(dist, 'index.html'));
cpSync(join(ui, 'tokens.css'), join(dist, 'tokens.css'));

console.log('built dist/ (bundle.js + index.html + tokens.css)');
