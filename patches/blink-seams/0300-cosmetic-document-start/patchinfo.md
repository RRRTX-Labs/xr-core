# patchinfo — 0300-cosmetic-document-start

- **id:** 0300-cosmetic-document-start
- **title:** XR cosmetic filtering v1 renderer seam — guarded document-start hook in `Document::WillInsertBody` installing the per-frame cosmetic key set before first layout
- **owner:** @xr/platform
- **category:** blink_seams
- **files:** third_party/blink/renderer/core/BUILD.gn · third_party/blink/renderer/core/dom/document.cc (upstream hooks) + third_party/blink/renderer/core/xr/BUILD.gn · third_party/blink/renderer/core/xr/xr_cosmetic_seam.h · third_party/blink/renderer/core/xr/xr_cosmetic_seam.cc (XR-owned payload) — 5 files. Budget note: the blink_seams cap of 25 counts PATCH ENTRIES, not files (`build/patching/apply.py lint` and `build/farm/budget_meter.py` both do `counts[cat] += 1` per row), so this row spends 1 of 25.
- **upstream-bug-if-any:** none (embedder feature seam; no upstream defect involved, and nothing here changes upstream behaviour when `ENABLE_XR_COSMETIC` is undefined)
- **retirement plan:** superseded when the renderer cosmetic engine lands as a component-level integration rather than a seam payload (Plan §1.2 intake order: components before patches — `core/xr/` is the component seed, exactly as `services/network/xr/` was for P11); obsolete at the milestone where a document-start cosmetic hook upstreams. Retirement goes through `xr-patch retire` (the only sanctioned removal path, P3-T7), never by deleting the directory.
- **rebase-notes:** both transforms are anchored to lines live-read at pin d04cdb24d67b081f6cf80200ffc5233f44b61109 (Chromium 152.0.7977.82): the `void Document::WillInsertBody() {` definition head (document.cc:4112), the `#include "third_party/blink/renderer/core/dom/document.h"` line (document.cc:30), and the `component("core")` deps-list tail `"//ui/strings",` + `]` (core/BUILD.gn:446). Every anchor is asserted to occur exactly once. `build/webui/cosmetic_seam_roundtrip.py` re-fetches and re-proves them on every governance-lane run, so drift reddens the gate instead of being guessed at. `document.cc` is a ~10.4k-line high-churn file: if `WillInsertBody` renames, or the deps tail moves because a new `//ui/...` dep lands, the round-trip reports `anchor not found at the pin` — a hard error, never a fuzzy apply — and the rebaser re-reads the seam live. One invariant a rebaser must preserve: `defines = [ "ENABLE_XR_COSMETIC=1" ]` must stay INSIDE the `component("core")` scope, because that is the target `document.cc` compiles into; hoisting it out silently makes the `#if` at the call site permanently dead and the feature unswitchable, with no build error to say so.

## Why this patch exists

P12's objective is to hide-and-fix the ads the network layer cannot — injected
content, soft walls, element-level junk. XR Shield v1 (P11) blocks requests
before a renderer exists, and that is the right place for anything addressable
by URL. What it structurally cannot reach is content that is *in the document*:
a `<div>` the page builds itself, a soft wall's overlay, a widget whose only
identifying property is its selector. Those need a renderer-side element-hiding
set installed before anything paints.

A patch is the chosen mechanism because the seam is one guarded call plus its
payload — no upstream behaviour changes when `ENABLE_XR_COSMETIC` is undefined,
and the payload is five files of which three are XR-owned. The decision core
lives in xr-core (`renderer/cosmetic/core`, std-only C++20, zero Chromium
includes) behind the `cosmetic-blob-v1` contract; what xr-core cannot own is the
call site inside Blink, and the one place the set has to be installed is before
first layout.

## The hook: `Document::WillInsertBody` (document.cc:4112 at the pin)

Three properties, each verifiable in the pinned file rather than remembered:

- **It is real, at the pin.** This matters more than it looks. The first draft
  of this patch hooked `Document::ParseRootElementBeforeChildren`, which is
  **not a function in Chromium 152 at any line**. A hand-written diff reports
  nothing about that, and `tools/blink_guard_lint.py` sees a syntactically fine
  hunk and passes it. `build/webui/cosmetic_seam_roundtrip.py` re-derives this
  patch from the pinned upstream bytes and refuses to emit it unless every
  anchor occurs exactly once there; it reported `anchor not found at the pin`
  and the hook moved. The patch in this directory is that tool's output, so its
  hunk headers, context lines and line numbers are upstream's.
- **The root element already exists.** `<html>` is parsed before `<body>`, so
  the frame's site and identity class are available for the scope key
  (`renderer/cosmetic/core/scope_key.h`) and neither is provisional.
- **Nothing has painted.** Upstream's own `Document::ShouldScheduleLayout()`
  (document.cc:4729) refuses to schedule layout until "we have a body element,
  or parsing has finished and we don't have a body element". No layout can
  precede the body existing, and this hook fires immediately before it does.

Rejected alternative: a commit-time hook (`DidCommitProvisionalLoad` and
friends). Those fire before the origin is committed, which would make the scope
key depend on a provisional value — the cross-frame leak the scope key exists to
prevent.

## What "document-start" does NOT mean here

The honest limits of this seam, stated because the phrase is load-bearing:

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
  `docs/contracts/vectors/cosmetic-v1.json` carry no embedder parameter at all.

## Guard law

The call site sits inside `#if defined(ENABLE_XR_COSMETIC)`.
`tools/blink_guard_lint.py` (xr-browser) parses the hunk text and fails on an
added call whose hunk has no XR-owned `#if`. A bare `if (xr_shield_cosmetic_v1)`
at the call site is deliberately **not** accepted: the runtime flag gates the
call, but only the preprocessor gate stops the code from existing in an off
build, and the off state is specified as byte-identical to today.

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

## Upstream drift risk

`document.cc` is ~10.4k lines and churns constantly. The two contracts this
patch relies on are the `WillInsertBody()` definition head and the
`core/BUILD.gn` `component("core")` deps tail. If either moves, the round-trip
reddens at the pin with `anchor not found` — a hard error, never a fuzzy apply —
and the rebaser re-reads the seam live rather than guessing. Detection is
`build/webui/cosmetic_seam_roundtrip.py` in `run_checks.sh` and the governance
workflow (real fetch through `build/upstream/fetch.py`, no fixtures).

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
from the environment this was written in, so the payload has never been compiled
against Blink and GN's acceptance of the two `BUILD.gn` files is unproven.
Nothing here observed a page. The page-level assertions — "no blank page", "the
element is actually gone" — are **HG-31** and run in the nightly build.

What *is* proven, mechanically, all re-runnable:

| Check | Result |
| --- | --- |
| `build/webui/cosmetic_seam_roundtrip.py` | apply PASS · 5/5 markers · revert byte-exact · perturbed anchor FAILS to apply · 5 files · never-list PASS |
| `tools/blink_guard_lint.py` | `hooks: 1, guarded: 1, unguarded: 0` |
| `tools/patch_manifest_check.py` | declared `files:` equals the patch diff, both directions |
| `build/patching/apply.py lint` | `PASS: xr-patch` — mandatory patchinfo fields, category, path policy |
| `tools/tests/test_blink_guard_lint.py` | 20 passed, incl. the P11 patch as a regression fixture |

The round-trip fetches real upstream bytes at the pin and proves the patch is
bound to them. It does not run gn. Static and patch-level, not a page render.
