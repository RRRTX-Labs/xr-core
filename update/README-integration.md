# update/ — the XR update verifier core & //chrome/updater integration

Status: **real-in-ci (off-tree)** · verifier core std-only C++20 · host +
tests + golden vectors live here · production signing = human-gated.

## What this directory is

`update/core/` is the decision core the browser uses to decide whether an
update-server response may be applied: strict manifest parse (the frozen
`update-manifest` 3.1 subset — consumed, never redefined), signature/epoch
policy, replay memory, cohort bucketing, and adaptive backoff. It is
**std-only C++20**: no Chromium headers, no sockets, no TLS — verification
is transport-independent by construction (`core/**` has no transport datum;
the TLS-independence matrix is a test suite: `test_verify_policy`).

`update/host/update_host.cc` is the stdio-JSON façade (same protocol
conventions as the commands/settings/themes hosts: one canonical line,
sorted keys, non-ASCII `\uXXXX`, exit 0/1/2), with 7 methods:
`flag-status`, `verify`, `epoch-apply`, `cohort`, `backoff`,
`about-states`, `about-state`. `update/tests/` builds 9 suites via a bare
`make` (clean-clone safe) and ends `ALL C++ UPDATE TESTS PASSED`.

## The signature law (what this code does NOT do)

There is **no crypto in this tree**. `core/verifier.h` is an injected
interface:

```cpp
class SignatureVerifier { public: virtual ~SignatureVerifier() = default;
  virtual bool Verify(const std::string& public_key,
                      const std::string& canonical_message,
                      const std::string& signature) = 0; };
```

| Binding      | Where                                                    | Status |
|--------------|----------------------------------------------------------|--------|
| TEST-ONLY    | `update/tests/test_verifier_testonly.h` (stub: `sig:` + first 16 hex of `sha256(public_key + "|" + message)`), banner in every accept output | real-in-ci, here |
| PRODUCTION   | minisign-ed25519 artifact signatures, keys per `release/keys/`, ceremony per `docs/release/key-ceremony.md` | human-gated (HG-36) |

The host prints a `verifier: test-only …` banner on every verify result —
a release build must bind the production verifier and the banner must go.

## Integrating //chrome/updater (farm glue — the honest boundary)

1. **Flag**: build with `xr_updater_v0 = true` (default; `build/gn/
   argsets/flags.yaml`, wired in `xr_common.gni`). Flag off ⇒ the host
   refuses with the typed reason and stock chrome update behavior stands.
2. **App identity**: the updater must present `appid == labs.rrrtx.xr`
   (the core denies anything else — `unknown-appid`).
3. **Update URL**: the response must come from the XR update server
   (`release/server/`); the core ignores transport — a good signature over
   the canonical response bytes decides, bad TLS + good signature ⇒ accept,
   good TLS + bad signature ⇒ deny (tested, not asserted).
4. **Registration**: `//chrome/updater` config (app id, install PerUser vs
   PerMachine, `--updater` entry points) is **farm glue (HG-31)** — it
   requires a real `//chrome/updater` build at the pin
   (`docs/release/updater-integration.md` carries the exact targets). No
   Chromium patch entry exists or may exist for About/update registration
   (P10 failure condition 6); the About view (`xr-core/ui/about/`) consumes
   the host's `about-state` method only.
5. **Epoch**: the embedder persists epoch state and passes it in every
   `verify` call; `epoch-apply` consumes signed revocation notices
   (`{body, signature}` wrapper, minisign key material pinned per epoch).

## Tests (this tree)

```
make -C update/tests test     # 9 suites, ends: ALL C++ UPDATE TESTS PASSED
XR_FUZZ_SECONDS=600 make -C update/tests test test_update_fuzz
```

Golden vectors: `xr-browser/docs/contracts/vectors/update-v1.json` (73
cases) — byte-parity across BOTH backends (`update_host` C++ and
`fakes/update.py`) via `xr-browser/tools/update_vectors_check.py`.
Never-accept fuzz oracle: no mutation of a response pool ever yields an
accept (seeded, ≥600 s campaigns, min-iters enforced).
