# 0300-cosmetic-document-start

**Phase:** P12 — cosmetic filtering + scriptlet injection (renderer seam)
**Owner:** `@xr/platform` · **Category:** `blink_seams` (cap 25) · **Files:** 5
**Guard:** `ENABLE_XR_COSMETIC` · **Compiled:** NOT-RUN (gn/ninja absent)

## What it does

Two upstream hooks and three XR-owned payload sources.

| File | Kind | Change |
| --- | --- | --- |
| `core/BUILD.gn` | upstream hook | `if (xr_shield_cosmetic_v1) { defines = [ "ENABLE_XR_COSMETIC=1" ]; deps += [ …/xr:xr_cosmetic_seam" ] }` inside `component("core")` |
| `core/dom/document.cc` | upstream hook | the seam include, plus one guarded call at the top of `Document::WillInsertBody()` |
| `core/xr/BUILD.gn` | payload | the `source_set`, gated on the same arg |
| `core/xr/xr_cosmetic_seam.h` | payload | the three entry points the upstream file may call |
| `core/xr/xr_cosmetic_seam.cc` | payload | the seam body: derive the scope key, consult the blob, install |

`defines` lives in `component("core")` because that is the target `document.cc`
compiles into; without it the `#if` at the call site never becomes live and the
feature cannot be switched on at all. Default off
(`build/gn/argsets/flags.yaml`: `xr_shield_cosmetic_v1`, `kind: xr`, `false`), so
the off state neither defines the macro nor links the payload — the flag gates
the CODE, not merely the call.

## Why `Document::WillInsertBody`

The objective is to hide element-level junk **at document-start**, so the set
has to be installed before anything paints. Three properties, each verifiable in
the pinned file (`d04cdb24`, Chromium 152.0.7977.82):

- **It is real, at the pin.** `document.cc:4112`. This matters more than it
  looks: the first draft of this patch hooked
  `Document::ParseRootElementBeforeChildren`, which is **not a function in
  Chromium 152 at any line**. A hand-written diff reports nothing about that.
  `build/webui/cosmetic_seam_roundtrip.py` re-derives this patch from the pinned
  upstream bytes and refuses to emit it unless every anchor occurs exactly once
  there; it reported `anchor not found at the pin` and the hook moved. The patch
  in this directory is that tool's output, so its hunk headers and context lines
  are upstream's, not anyone's recollection.
- **The root element already exists.** `<html>` is parsed before `<body>`, so
  the frame's site and identity class are available for the scope key
  (`renderer/cosmetic/core/scope_key.h`) and neither is provisional.
- **Nothing has painted.** Upstream's own `Document::ShouldScheduleLayout()`
  (`document.cc:4729`) refuses to schedule layout until "we have a body element,
  or parsing has finished and we don't have a body element". So no layout can
  precede the body existing, and this hook fires immediately before it does.

Rejected alternative: a commit-time hook (`DidCommitProvisionalLoad` and
friends). Those fire before the origin is committed, which would make the scope
key depend on a provisional value — the cross-frame leak the scope key exists to
prevent.

## What "document-start" does NOT mean here

This is the honest limit of the seam, and it is why scriptlet injection is not
wired to it.

- **`<head>` scripts have already run** by the time this fires. Injecting a page
  script here would run it *after* the head-phase scripts it exists to pre-empt,
  which defeats the purpose. Element hiding is unaffected — head content is not
  renderable — and scriptlet EXECUTION is off in P12 anyway
  (`xr_shield_scriptlets` false, `main_world_permitted: false` in
  `renderer/cosmetic/scriptlets/registry.yaml`), so the gap is not yet
  consequential. Closing it is a separate seam, not a change to this one.
- **A document with no `<body>` never reaches the hook** (SVG, XML). The
  cosmetic set is never installed there and the document renders unstyled. That
  is fail-open, and upstream's own branch for "parsing has finished and we don't
  have a body element" confirms such documents exist rather than being an error.
- **It is per-frame, not per-tab.** An OOPIF child inserts its own body, so the
  child derives its own key from its own scope. That is the structural reason
  the scope key never needs the embedder's site, and why the scope-key cases in
  `docs/contracts/vectors/cosmetic-v1.json` have no embedder parameter at all.

## Guard law

The call site sits inside `#if defined(ENABLE_XR_COSMETIC)`.
`tools/blink_guard_lint.py` (xr-browser) parses the hunk text and fails on an
added call whose hunk has no XR-owned `#if`. A bare
`if (xr_shield_cosmetic_v1)` at the call site is deliberately **not** accepted:
the runtime flag gates the call, but only the preprocessor gate stops the code
from existing in an off build, and the off state is specified as byte-identical
to today.

Two notes on that lint, both earned by running it against a patch its author did
not write:

- The guard must be an **XR-owned macro**, meaning the name carries an `XR_`
  component — not that it *starts* with one. Chromium's feature-gate convention
  is `ENABLE_<FEATURE>`, and the shipped P11 seam is guarded by
  `ENABLE_XR_SHIELD`. A rule anchored at `^XR_` rejected that already-reviewed
  patch; `tools/tests/test_blink_guard_lint.py` now pins the rule against patch
  0200 so it cannot narrow again.
- The call pattern is matched **anywhere** on an added line. P12 calls a free
  function (`blink::xr::OnDocumentStart(this);`); P11 calls a static member
  inside a condition (`if (network::xr::XrShieldGate::ShouldBlock(*req)) {`). An
  anchored pattern counts zero hooks for the second shape, and "hooks: 0, PASS"
  certifies nothing — worse than a false alarm.

`#if BUILDFLAG(…)` is not credited as a guard: an upstream flag has nothing to
do with `xr_shield_cosmetic_v1`, and crediting it would compile the seam into
builds where the feature is off.

## Fail-open

`OnDocumentStart` returns `void` and has no output parameter, so the caller
cannot branch on it. Every refusal inside the seam — unknown scheme, missing
blob, digest mismatch, malformed ABPF, oversized key set, any parse failure —
installs nothing and the page renders unstyled. Refusing to hide an ad must
never refuse to render a page. The 14 conditions and 7 outcomes live in
`renderer/cosmetic/core/degrade.{h,cc}`; only `kRuleTooExpensive` drops a single
rule, every parse or pseudo failure drops the whole blob.

## What is NOT claimed

**No Chromium build, and no page-level behaviour.** `gn` and `ninja` are absent
from the environment this was written in, so the payload has never been
compiled against Blink and GN's acceptance of the two `BUILD.gn` files is
unproven. Nothing here observed a page. The page-level assertions — "no blank
page", "the element is actually gone" — are **HG-31** and run in the nightly
build.

What *is* proven, mechanically, all re-runnable:

| Check | Result |
| --- | --- |
| `build/webui/cosmetic_seam_roundtrip.py` | apply PASS · 5/5 markers · revert byte-exact · perturbed anchor FAILS to apply · 5 files vs cap 25 · never-list PASS |
| `tools/blink_guard_lint.py` | `hooks: 1, guarded: 1, unguarded: 0` |
| `tools/patch_manifest_check.py` | `patches: 4, files: 21, categories: 4` — declared `files:` equals the paths the patch touches |
| `tools/tests/test_blink_guard_lint.py` | 20 passed, incl. the P11 patch as a regression fixture |

The round-trip fetches real upstream bytes at the pin and proves the patch is
bound to them. It does not run gn. Static and patch-level, not a page render.
