#!/usr/bin/env python3
"""Behavioral reference fake for the C++ update host (update-host-protocol v1).

BEHAVIORAL mirror of xr-core/update/host/update_host.cc + update/core, NOT a
binding — the P6/P7/P8/P9 pattern: dependent tracks drive updates through
the SAME stdio JSON protocol without a C++ toolchain, and the byte-parity
harness (xr-browser tools/vectors machinery + golden vectors
docs/contracts/vectors/update-v1.json) asserts this fake is BYTE-IDENTICAL
to the compiled C++ host on the deterministic surface.

The verification POLICY here is an INDEPENDENT implementation (the second
implementation the vectors cross-check): strict deny-on-unknown envelope
parse (the frozen update-manifest-31 subset + the living
xr-update-envelope-v1 wrapper), canonical-version law, epoch kill switch,
monotonicity, replay, and the TLS-independence law (the transport echo is
accepted and ignored — by construction no transport datum reaches the
decision). The signature PRIMITIVE is the same deterministic TEST-ONLY stub
as the C++ host ("sig:" + first 16 hex of sha256(canonical response)); it
is a fixture for policy testing, never an authenticity mechanism, and it
refuses release channels exactly like the C++ side.

Stdlib only. Exit 0 typed result / 1 typed error / 2 usage. Canonical JSON:
json.dumps(sort_keys=True, separators=(',',':'), ensure_ascii=True).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

STATES = ["idle", "checking", "available", "downloading", "ready",
          "failed", "refused"]

REASON = {
    "ok": "ok", "malformed": "malformed-manifest", "unknown": "unknown-field",
    "protocol": "wrong-protocol", "version": "non-canonical-version",
    "oversize": "oversize", "url": "insecure-url", "digest": "bad-digest",
    "app": "unknown-appid", "nooffer": "no-update-offered",
    "downgrade": "downgrade-refused", "equal": "equal-version-reoffer",
    "replay": "replay-refused", "epoch": "epoch-revoked",
    "epochkey": "unknown-epoch-key", "sigkey": "unknown-signing-key",
    "nosig": "signature-missing", "badsig": "signature-invalid",
    "testrefuse": "test-verifier-refused", "seenpersist": "seen-persist-failed",
}

MAX_DOC = 64 * 1024
APP_ID = "labs.rrrtx.xr"


def canonical(obj: Any) -> str:
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def _is_hex64(s: str) -> bool:
    return len(s) == 64 and all(c in "0123456789abcdef" for c in s)


def parse_version(text: str) -> list[int] | None:
    parts = text.split(".")
    if len(parts) != 4:
        return None
    out = []
    for p in parts:
        if not p.isdigit():
            return None
        if len(p) > 1 and p[0] == "0":
            return None  # leading zero
        v = int(p)
        if v > 0xFFFFFFFF:
            return None
        out.append(v)
    return out


def _closed(obj: dict[str, Any], allowed: set[str]) -> bool:
    return set(obj.keys()) <= allowed


def parse_envelope(raw: str) -> tuple[dict[str, Any] | None, str]:
    """Returns (envelope_view, reason). Mirrors ParseEnvelope exactly."""
    if len(raw.encode("utf-8")) > MAX_DOC:
        return None, REASON["oversize"]
    body = raw
    if body.startswith(")]}'\n"):
        body = body[4:]
    try:
        doc = json.loads(body)
    except Exception:
        return None, REASON["malformed"]
    if not isinstance(doc, dict) or not _closed(
            doc, {"schema", "schema_version", "response", "epoch", "signature"}):
        return None, REASON["unknown"] if isinstance(doc, dict) else REASON["malformed"]
    if doc.get("schema") != "xr-update-envelope" or doc.get("schema_version") != 1:
        return None, REASON["malformed"]
    resp = doc.get("response")
    if not isinstance(resp, dict) or not _closed(
            resp, {"protocol", "server", "daystart", "app"}):
        return None, REASON["unknown"] if isinstance(resp, dict) else REASON["malformed"]
    if resp.get("protocol") != "3.1":
        return None, REASON["protocol"]
    daystart = resp.get("daystart")
    if daystart is not None and (not isinstance(daystart, dict) or not _closed(
            daystart, {"elapsed_days", "elapsed_seconds"})):
        return None, REASON["unknown"]
    apps = resp.get("app")
    if not isinstance(apps, list) or len(apps) != 1:
        return None, REASON["malformed"]
    app0 = apps[0]
    if not isinstance(app0, dict) or not _closed(
            app0, {"appid", "status", "updatecheck"}):
        return None, REASON["unknown"]
    uc = app0.get("updatecheck")
    if not isinstance(uc, dict) or not _closed(uc, {"status", "urls", "manifest"}):
        return None, REASON["unknown"]
    ucs = uc.get("status")
    if not isinstance(ucs, str) or not (ucs in ("ok", "noupdate") or
                                        ucs.startswith("error-")):
        return None, REASON["malformed"]

    if not isinstance(app0.get("appid"), str) or not app0.get("appid"):
        return None, REASON["malformed"]
    env: dict[str, Any] = {
        "app_id": app0.get("appid"),
        "app_status": app0.get("status"),
        "updatecheck_status": ucs,
        "canonical_response": canonical(resp),
        "manifest_id": hashlib.sha256(canonical(resp).encode()).hexdigest(),
    }
    epoch = doc.get("epoch")
    if not isinstance(epoch, dict) or not _closed(epoch, {"epoch_id", "key_id", "seq"}):
        return None, REASON["unknown"]
    if not isinstance(epoch.get("epoch_id"), str) or not epoch["epoch_id"]:
        return None, REASON["malformed"]
    if not isinstance(epoch.get("key_id"), str) or not epoch["key_id"]:
        return None, REASON["malformed"]
    seq = epoch.get("seq")
    if not isinstance(seq, int) or isinstance(seq, bool) or seq < 0:
        return None, REASON["malformed"]
    env["epoch_id"], env["epoch_key_id"], env["epoch_seq"] = (
        epoch["epoch_id"], epoch["key_id"], seq)
    sig = doc.get("signature")
    if not isinstance(sig, dict) or not _closed(sig, {"alg", "key_id", "sig"}):
        return None, REASON["unknown"]
    if sig.get("alg") != "minisign-ed25519":
        return None, REASON["malformed"]
    if not isinstance(sig.get("key_id"), str) or not sig["key_id"]:
        return None, REASON["malformed"]
    if not isinstance(sig.get("sig"), str) or not sig["sig"]:
        return None, REASON["malformed"]
    env["sig_key_id"], env["sig_value"] = sig["key_id"], sig["sig"]

    if ucs == "ok":
        manifest = uc.get("manifest")
        urls = uc.get("urls")
        if not isinstance(manifest, dict) or not isinstance(urls, dict):
            return None, REASON["malformed"]
        if not _closed(urls, {"url", "hash_sha256"}):
            return None, REASON["unknown"]
        if not _closed(manifest, {"version", "packages", "run"}):
            return None, REASON["unknown"]
        version = parse_version(manifest.get("version", ""))
        if version is None:
            return None, REASON["version"]
        env["version"] = version
        packages = manifest.get("packages")
        if not isinstance(packages, dict) or not _closed(packages, {"package"}):
            return None, REASON["unknown"]
        pkg_list = packages.get("package")
        if not isinstance(pkg_list, list) or len(pkg_list) != 1:
            return None, REASON["malformed"]
        pkg = pkg_list[0]
        if not isinstance(pkg, dict) or not _closed(
                pkg, {"name", "size", "hash_sha256", "fp"}):
            return None, REASON["unknown"]
        size = pkg.get("size")
        if (not isinstance(pkg.get("name"), str) or not pkg["name"] or
                not isinstance(size, int) or isinstance(size, bool) or size < 0 or
                not isinstance(pkg.get("fp"), str) or
                "hash_sha256" not in pkg):
            return None, REASON["malformed"]
        if not _is_hex64(pkg["hash_sha256"]):
            return None, REASON["digest"]
        env["package"] = (pkg["name"], size, pkg["hash_sha256"], pkg["fp"])
        url_list = urls.get("url")
        if not isinstance(url_list, list) or not url_list:
            return None, REASON["malformed"]
        for u in url_list:
            if not isinstance(u, dict) or not _closed(u, {"codebase"}):
                return None, REASON["unknown"]
            cb = u.get("codebase")
            if not isinstance(cb, str):
                return None, REASON["malformed"]
            if not cb.lower().startswith("https://"):
                return None, REASON["url"]
        env["codebase"] = url_list[0]["codebase"]
    return env, "ok"


def epoch_acceptable(state: dict[str, Any], env: dict[str, Any]) -> bool:
    if state.get("manual_path") or state.get("revoked"):
        return False
    seq = env["epoch_seq"]
    if seq < 0:
        return False
    if state.get("seq", -1) >= 0 and seq < state["seq"]:
        return False
    if state.get("epoch_id") and env["epoch_id"] != state["epoch_id"]:
        return False
    # key_id is NOT compared here: a manifest claiming our epoch under a
    # different key is a key-substitution attempt — unknown-signing-key,
    # checked by the caller (parity with verify_policy.cc).
    return True


def verify(args: dict[str, Any]) -> dict[str, Any]:
    channel = args.get("channel")
    raw = args.get("envelope")
    current = args.get("current_version")
    keys = args.get("keys")
    if (not isinstance(raw, str) or not raw or not isinstance(channel, str) or
            not channel or not isinstance(current, str) or
            not isinstance(keys, list) or not keys):
        return {"error": "kMalformedInput",
                "detail": "verify needs envelope, channel, current_version, keys"}
    if not all(isinstance(k, dict) and isinstance(k.get("key_id"), str) and
               isinstance(k.get("public_key"), str) for k in keys):
        return {"error": "kMalformedInput", "detail": "bad keys[]"}
    pinned = {k["key_id"]: k["public_key"] for k in keys}
    for k in args:
        if k not in ("envelope", "channel", "current_version", "epoch",
                     "keys", "seen", "transport"):
            return {"error": "kMalformedInput",
                    "detail": "unknown verify arg " + str(k)}

    # TEST-ONLY primitive refuses release channels FIRST — even a malformed
    # envelope on a release channel is a test-verifier refusal (C++ parity).
    if channel not in ("dev", "nightly-test"):
        return _deny(REASON["testrefuse"])
    env, reason = parse_envelope(raw)
    if env is None:
        return _deny(reason)
    if env["app_id"] != APP_ID:
        return _deny(REASON["app"])
    if env["updatecheck_status"] != "ok":
        return _deny(REASON["nooffer"])
    state = args.get("epoch") or {}
    if not isinstance(state, dict):
        return {"error": "kMalformedInput", "detail": "bad epoch"}
    if not epoch_acceptable(state, env):
        return _deny(REASON["epoch"], bool(state.get("manual_path")))
    if state.get("key_id") and env["epoch_key_id"] != state["key_id"]:
        return _deny(REASON["sigkey"])
    if env["sig_key_id"] != env["epoch_key_id"] or env["sig_key_id"] not in pinned:
        return _deny(REASON["sigkey"])
    material = pinned.get(env["sig_key_id"], "")
    expect = "sig:" + hashlib.sha256(
        (material + "|" + env["canonical_response"]).encode()).hexdigest()[:16]
    if not env["sig_value"]:
        return _deny(REASON["nosig"])
    if env["sig_value"] != expect:
        return _deny(REASON["badsig"])
    cur = parse_version(current or "0.0.0.0")
    if cur is None:
        return _deny(REASON["version"])
    if env["version"] == cur:
        return _deny(REASON["equal"])
    if env["version"] < cur:
        return _deny(REASON["downgrade"])
    seen = args.get("seen") or []
    if not isinstance(seen, list):
        return {"error": "kMalformedInput", "detail": "bad seen[]"}
    if env["manifest_id"] in seen:
        return _deny(REASON["replay"], False, len(set(seen)))
    return {
        "verdict": "accept",
        "reason": "ok",
        "manifest_id": env["manifest_id"],
        "manual_path": False,
        "seen_size": len(set(seen)) + 1,
        "verifier": "test-only: XR UPDATE HOST IS A TEST FIXTURE, "
                    "NOT A RELEASE SIGNING PATH",
    }


def _deny(reason: str, manual: bool = False, seen_size: int = 0) -> dict[str, Any]:
    return {"verdict": "deny", "reason": reason, "manual_path": manual,
            "seen_size": seen_size,
            "verifier": "test-only: XR UPDATE HOST IS A TEST FIXTURE, "
                        "NOT A RELEASE SIGNING PATH"}


def epoch_apply(args: dict[str, Any]) -> dict[str, Any]:
    import hashlib as _h
    notice_raw = args.get("notice")
    keys = args.get("keys")
    if not isinstance(notice_raw, str) or not isinstance(keys, list):
        return {"error": "kMalformedInput",
                "detail": "epoch-apply needs notice, keys"}
    pinned = {k["key_id"]: k["public_key"] for k in keys
              if isinstance(k, dict) and isinstance(k.get("key_id"), str)}
    try:
        doc = json.loads(notice_raw)
    except Exception:
        return {"error": "kMalformedInput", "detail": "bad notice (malformed)"}
    if not isinstance(doc, dict) or not _closed(
            doc, {"schema", "schema_version", "epoch_id", "key_id", "seq",
                  "reason", "body", "signature"}):
        return {"error": "kMalformedInput", "detail": "bad notice (malformed)"}
    if not _closed(doc, {"body", "signature"}):
        return {"error": "kMalformedInput", "detail": "bad notice (unknown-field)"}
    body = doc.get("body", doc)
    if not isinstance(body, dict) or not _closed(
            body, {"schema", "schema_version", "epoch_id", "key_id", "seq",
                   "reason"}):
        return {"error": "kMalformedInput", "detail": "bad notice (unknown-field)"}
    if body.get("schema") != "xr-epoch-revocation" or \
            body.get("schema_version") != 1:
        return {"error": "kMalformedInput", "detail": "bad notice (malformed)"}
    if not isinstance(body.get("epoch_id"), str) or not body["epoch_id"]:
        return {"error": "kMalformedInput", "detail": "bad notice (malformed)"}
    if not isinstance(body.get("key_id"), str) or not body["key_id"]:
        return {"error": "kMalformedInput", "detail": "bad notice (malformed)"}
    seq = body.get("seq")
    if not isinstance(seq, int) or isinstance(seq, bool) or seq < 0:
        return {"error": "kMalformedInput", "detail": "bad notice (malformed)"}
    if not isinstance(body.get("reason"), str):
        return {"error": "kMalformedInput", "detail": "bad notice (malformed)"}
    sig = doc.get("signature")
    sig_value = sig.get("sig") if isinstance(sig, dict) else None
    signed = canonical(body)
    material = pinned.get(body["key_id"], "")
    expect = "sig:" + _h.sha256((material + "|" + signed).encode()).hexdigest()[:16]
    kid = body["key_id"]
    if not sig_value or kid not in pinned or sig_value != expect:
        return {"error": "kRejected", "reason": "signature-invalid"}
    state = dict(args.get("epoch") or {})
    stale = (state.get("seq", -1) >= 0 and seq <= state["seq"]
             and not state.get("revoked"))
    applied = not stale
    if applied:
        state.update({"epoch_id": body["epoch_id"], "key_id": kid, "seq": seq,
                      "revoked": True, "manual_path": True})
    return {"applied": applied, "epoch": state}


def cohort(args: dict[str, Any]) -> dict[str, Any]:
    iid = args.get("install_id")
    ch = args.get("channel")
    buckets = args.get("buckets")
    if (not isinstance(iid, str) or not iid or not isinstance(ch, str) or
            not ch or not isinstance(buckets, int) or isinstance(buckets, bool)
            or buckets <= 0 or buckets > 100000):
        return {"error": "kMalformedInput",
                "detail": "cohort needs install_id, channel, buckets"}
    mixed = (iid + "\x1f" + ch).encode()
    acc = int.from_bytes(hashlib.sha256(mixed).digest()[:8], "big")
    return {"bucket": acc % buckets}


def _backoff_from(state: dict[str, Any]) -> dict[str, Any]:
    return {"last_check_mono": state.get("last_check_mono", -1),
            "next_allowed_mono": state.get("next_allowed_mono", 0),
            "fail_streak": state.get("fail_streak", 0)}


def backoff(args: dict[str, Any]) -> dict[str, Any]:
    st = args.get("state")
    now = args.get("now_mono")
    if not isinstance(st, dict) or not isinstance(now, int) or isinstance(now, bool):
        return {"error": "kMalformedInput", "detail": "backoff needs state, now_mono"}
    bs = _backoff_from(st)
    outcome = args.get("outcome")
    if isinstance(outcome, str):
        bs["last_check_mono"] = now
        if outcome == "success":
            bs["fail_streak"] = 0
            bs["next_allowed_mono"] = now + 6 * 60 * 60
        else:
            bs["fail_streak"] = bs["fail_streak"] + 1
            s = 30 * 60
            for _ in range(bs["fail_streak"] - 1):
                s = min(s * 2, 48 * 60 * 60)
            bs["next_allowed_mono"] = now + max(6 * 60 * 60, s)
    return {"may_check_now": now >= bs["next_allowed_mono"],
            "seconds_until_next": max(0, bs["next_allowed_mono"] - now),
            "state": bs}


def step_about(state: str, event: str) -> tuple[str, str] | None:
    if event == "epoch-revoked":
        return "refused", "epoch revoked: manual download required"
    if state == "refused":
        return "refused", "epoch revoked: manual download required"
    if event == "check" and state in ("idle", "failed"):
        return "checking", ""
    if event == "offer" and state == "checking":
        return "available", ""
    if event == "noupdate" and state == "checking":
        return "idle", "up to date"
    if event == "download-start" and state == "available":
        return "downloading", ""
    if event == "download-done" and state == "downloading":
        return "ready", ""
    if event == "error" and state in ("checking", "downloading", "available"):
        return ("failed",
                "update failed: reason shown with a manual-download path")
    if event == "install" and state == "ready":
        return "idle", ""
    if event == "reset":
        return "idle", ""
    return None


def about_state(args: dict[str, Any]) -> dict[str, Any]:
    state = args.get("state")
    event = args.get("event")
    if not isinstance(state, str) or not state or not isinstance(event, str) or not event:
        return {"error": "kMalformedInput", "detail": "about-state needs state, event"}
    step = step_about(state, event)
    if step is None:
        return {"error": "kRejected", "reason": "unknown transition"}
    nxt, reason = step
    o: dict[str, Any] = {"state": nxt}
    if reason:
        o["reason"] = reason
    if nxt == "failed":
        o["manual_download"] = {"path": "xr://about -> manual download"}
        o["check_again"] = True
    if nxt == "refused":
        o["manual_download"] = {"path": "xr://about -> manual download"}
    return o


def main() -> int:
    ap = argparse.ArgumentParser(prog="fakes/update.py")
    ap.add_argument("request", nargs="?")
    ap.add_argument("--store-dir", default="")
    a = ap.parse_args()
    raw = a.request if a.request else sys.stdin.read()
    try:
        frame = json.loads(raw)
    except Exception:
        print(canonical({"error": "kMalformedInput", "detail": "bad stdin frame"}))
        return 1
    method = frame.get("method") if isinstance(frame, dict) else None
    args = frame.get("args", {}) if isinstance(frame, dict) else {}
    if not isinstance(args, dict):
        args = {}
    if method == "flag-status":
        print('{"xr_updater_v0":"on"}')
        return 0
    if method == "about-states":
        print(canonical({"states": STATES}))
        return 0
    if method in ("verify", "epoch-apply", "backoff"):
        pass  # flag law is host-CLI-level; the fake's flag is always on
    table = {"verify": verify, "epoch-apply": epoch_apply, "cohort": cohort,
             "backoff": backoff, "about-state": about_state}
    fn = table.get(method)
    if fn is None:
        print(canonical({"error": "kUnknownMethod", "detail": str(method)}))
        return 1
    out = fn(args)
    print(canonical(out))
    # C++ host parity: typed malformed-input refusals exit 1; everything
    # else (including typed kRejected verdicts) exits 0.
    return 1 if isinstance(out, dict) and out.get("error") == "kMalformedInput" else 0


if __name__ == "__main__":
    sys.exit(main())


def call(method: str, args: dict[str, Any]) -> dict[str, Any]:
    """Module dispatch for vectors_check.py and other tooling drivers."""
    table = {"verify": verify, "epoch-apply": epoch_apply, "cohort": cohort,
             "backoff": backoff, "about-state": about_state}
    fn = table.get(method)
    if fn is None:
        return {"error": "kUnknownMethod", "detail": str(method)}
    return fn(args)
