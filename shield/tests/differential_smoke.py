#!/usr/bin/env python3
"""Differential smoke: C++ shield_host vs Python fakes/shield.py.

Runs a battery of request frames (every method, every refusal family, the
frozen fixture surface, edge inputs) through BOTH backends and requires
byte-identical stdout AND identical exit codes. This is the dev-loop
precursor of tools/shield_vectors_check.py (xr-browser), which replays the
committed golden vectors the same way.
"""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

XR_CORE = Path(__file__).resolve().parent.parent.parent  # xr-core repo root
HOST = XR_CORE / "shield/tests/build/shield_host"
FAKE = XR_CORE / "fakes/shield.py"

BUNDLE = {
    "schema": "xr-list-bundle", "schema_version": 1, "name": "xr-default",
    "bundle_version": 3,
    "lists": [{
        "name": "l1", "attribution": "CC-BY-3.0",
        "rules": [
            {"id": "r1", "kind": "network", "filter": "||tracker.example^",
             "action": "block"},
            {"id": "r2", "kind": "network",
             "filter": "||tracker.example^ok.js", "action": "allow"},
            {"id": "r3", "kind": "redirect", "filter": "||ads.example/banner",
             "action": "redirect", "resource": "1x1.gif"},
            {"id": "r4", "kind": "network", "filter": "||scoped.example^",
             "action": "block", "domains": ["site.example"]},
            {"id": "r5", "kind": "network", "filter": "||wild.example*/ad_*.js",
             "action": "block"},
            {"id": "r6", "kind": "network", "filter": "|https://anchor.example/x",
             "action": "block"},
            {"id": "r7", "kind": "network", "filter": "||exact.example/e|",
             "action": "block"},
            {"id": "r8", "kind": "network", "filter": "plain-literal.js",
             "action": "block", "exclude_domains": ["safe.example"]},
            {"id": "r9", "kind": "cosmetic", "filter": "cosmetic.example",
             "action": "block"},
        ],
    }],
    "refusals": [{"directive": "##.ad", "reason":
                  "unsupported-directive:cosmetic-hash", "count": 2}],
}

CTX = {"identity": {"value": "xr:a"},
       "origin": {"scheme": "https", "registrable_domain": "tracker.example"},
       "url": "https://tracker.example/a.js", "request_class": "kScript"}


def ctx(url, domain="tracker.example", identity="xr:a", **kw):
    c = {"identity": {"value": identity},
         "origin": {"scheme": "https", "registrable_domain": domain},
         "url": url, "request_class": "kSubresource"}
    c.update(kw)
    return c


SAMPLE_EVENT = {
    "ts_millis": 1000, "identity": {"value": "xr:a"}, "tab_id": 7,
    "origin": {"scheme": "https", "registrable_domain": "tracker.example"},
    "target": "https://tracker.example/a.js", "rule": "||tracker.example^",
    "list_provenance": "l1", "action": "kBlocked", "request_class": "kScript"}

S0 = {"active": {"present": False}, "lkg": {"present": False}, "pins": [],
      "last_apply_mono": -1}

CASES: list[tuple[str, str, list[str]]] = []  # (frame_json, note, flags)


def C(method, args, flags=None, raw=None):
    CASES.append((raw if raw is not None else
                  json.dumps({"method": method, "args": args}, sort_keys=True),
                  f"{method}:{len(CASES)}", flags or []))


# frozen surface
C("Status", {"identity": {"value": "xr:...-001"},
             "origin": {"scheme": "https", "registrable_domain":
                        "example.com"}})
C("Status", {"identity": {"value": ""}, "origin": {"scheme": "https"}})
C("Status", {"identity": {}, "origin": {"scheme": "https"}})
C("Status", {"origin": {"scheme": "https"}})
C("Status", {"identity": {"value": "x"}, "origin": {"scheme": "ftp"}})
C("Status", {"identity": {"value": "x"}, "origin": {}})
C("Status", {"identity": {"value": "x"}, "origin": {"scheme": "https"},
             "ring": [SAMPLE_EVENT]})
C("Status", {"identity": {"value": "xr:a"}, "origin": {"scheme": "https"},
             "ring": [SAMPLE_EVENT, {**SAMPLE_EVENT, "action": "kAllowed"}]})
C("Status", {"identity": {"value": "x"}, "origin": {"scheme": "https"},
             "ring": "notarray"})
C("Status", {"identity": {"value": "x"}, "origin": {"scheme": "https"},
             "extra": 1})
C("Status", CTX, flags=["--flag", "xr_shield_v1=off"])
C("RecentEvents", {"identity": {"value": "xr:...-001"}, "max_events": 5})
C("RecentEvents", {"identity": {"value": "xr:a"}, "max_events": 0})
C("RecentEvents", {"identity": {"value": "xr:a"}})
C("RecentEvents", {"identity": {"value": "xr:a"}, "max_events": -3})
C("RecentEvents", {"identity": {"value": "xr:a"}, "max_events": "x"})
C("RecentEvents", {"identity": {"value": "xr:a"}, "max_events": 2,
                   "ring": [SAMPLE_EVENT] * 3})
C("RecentEvents", {"identity": {"value": "xr:b"}, "max_events": 9,
                   "ring": [SAMPLE_EVENT,
                            {**SAMPLE_EVENT, "identity": {"value": "xr:b"}}]})
C("RecentEvents", {"identity": "x", "max_events": 1})
C("RecentEvents", {"identity": {"value": "xr:a"}, "max_events": 1,
                   "ring": [{"bad": 1}]})

# flag-status / posture
C("flag-status", {})
C("flag-status", {}, flags=["--flag", "xr_shield_v1=off"])
C("flag-status", {"x": 1})
for mask in range(16):
    C("posture", {"engine_alive": bool(mask & 1),
                  "engine_poisoned": bool(mask & 2),
                  "route_bound": bool(mask & 4),
                  "kill_switch_on": bool(mask & 8)})
C("posture", {})
C("posture", {"engine_alive": "yes"})
C("posture", {"switch": True})

# match: the URL/grammar battery
for url, dom in [
        ("https://tracker.example/a.js", "tracker.example"),
        ("https://sub.tracker.example/a", "tracker.example"),
        ("https://tracker.example.com/a", "example.com"),
        ("https://tracker.example/ok.js", "tracker.example"),
        ("https://tracker.example/OK.js?x=1", "tracker.example"),
        ("https://ads.example/banner", "ads.example"),
        ("https://x.com/ads.example/banner", "x.com"),
        ("https://scoped.example/x", "site.example"),
        ("https://scoped.example/x", "other.example"),
        ("https://wild.example/deep/ad_min.js", "wild.example"),
        ("https://wild.example/ad_min.css", "wild.example"),
        ("https://anchor.example/x", "anchor.example"),
        ("https://anchor.example/xy", "anchor.example"),
        ("http://anchor.example/x", "anchor.example"),
        ("https://exact.example/e", "exact.example"),
        ("https://exact.example/e/more", "exact.example"),
        ("https://exact.example:8443/e", "exact.example"),
        ("https://any.example/js/plain-literal.js", "any.example"),
        ("https://plain-literal.js.example/", "example"),
        ("https://safe.example/plain-literal.js", "safe.example"),
        ("https://cosmetic.example/", "cosmetic.example"),
        ("https://clean.example/", "clean.example"),
        ("https://tracker.example/a.js?uid=SECRET#frag", "tracker.example"),
        ("https://user:pw@tracker.example/a.js", "tracker.example"),
        ("ftp://tracker.example/a.js", "tracker.example"),
        ("https://tracker.example", "tracker.example"),
]:
    C("match", {"context": ctx(url, dom), "bundle": BUNDLE})
# match: posture + scopes + no-bundle
C("match", {"context": CTX})
C("match", {"context": CTX, "bundle": BUNDLE, "engine_alive": False})
C("match", {"context": CTX, "bundle": BUNDLE, "engine_poisoned": True})
C("match", {"context": CTX, "bundle": BUNDLE, "kill_switch_on": True})
C("match", {"context": CTX, "bundle": BUNDLE, "route_bound": False})
C("match", {"context": CTX, "bundle": BUNDLE, "route_bound": False,
            "kill_switch_on": True, "engine_alive": False})
C("match", {"context": CTX, "bundle": BUNDLE,
            "scopes": [{"scope_id": "s1", "site": "tracker.example",
                        "rule_id": "r1", "reason": "user"}]})
C("match", {"context": CTX, "bundle": BUNDLE,
            "scopes": [{"scope_id": "s1", "identity": "xr:other",
                        "reason": "user"}]})
C("match", {"context": ctx("https://ads.example/banner", "ads.example"),
            "bundle": BUNDLE,
            "scopes": [{"scope_id": "s1", "site": "ads.example",
                        "reason": "user"}]})
C("match", {"context": CTX, "bundle": BUNDLE, "now_mono": 500,
            "scopes": [{"scope_id": "s1", "expiry_mono": 500,
                        "reason": "temp"}]})
C("match", {"context": CTX, "bundle": BUNDLE, "now_mono": 499,
            "scopes": [{"scope_id": "s1", "expiry_mono": 500,
                        "reason": "temp"}]})
C("match", {"context": CTX, "bundle": BUNDLE,
            "scopes": [{"scope_id": "s1", "workspace": "ws1",
                        "reason": "w"}]})
C("match", {"context": ctx("https://tracker.example/a.js", "tracker.example",
                           workspace="ws1"),
            "bundle": BUNDLE,
            "scopes": [{"scope_id": "s1", "workspace": "ws1",
                        "reason": "w"}]})
# match refusals
C("match", {})
C("match", {"context": {"identity": {"value": ""}}})
C("match", {"context": {**CTX, "tab_type": "guest"}})
C("match", {"context": {**CTX, "request_class": "kFetch"}})
C("match", {"context": {**CTX, "incognito": True}})
C("match", {"context": CTX, "bundle": {"schema": "wrong"}})
C("match", {"context": CTX, "bundle": {**BUNDLE, "lists": [
    {**BUNDLE["lists"][0],
     "rules": [{**BUNDLE["lists"][0]["rules"][0], "filter": "##.ad"}]}]}})
C("match", {"context": CTX, "bundle": BUNDLE, "scopes": [{"scope_id": "s"}]})
C("match", {"context": CTX, "bundle": BUNDLE, "now_mono": -1})
C("match", {"context": CTX, "bundle": BUNDLE, "scopes": "x"})

# bundle-load / bundle-check
C("bundle-load", {"bundle": BUNDLE})
C("bundle-load", {"bundle": {**BUNDLE, "bundle_version": 0}})
C("bundle-load", {"bundle": {**BUNDLE, "signed_by": "x"}})
C("bundle-load", {"bundle": {**BUNDLE, "lists": [
    {**BUNDLE["lists"][0],
     "rules": [{**BUNDLE["lists"][0]["rules"][0], "filter": "/re/"}]}]}})
C("bundle-load", {"bundle": {**BUNDLE, "lists": [
    {**BUNDLE["lists"][0],
     "rules": [{**BUNDLE["lists"][0]["rules"][0], "filter": "a|b"}]}]}})
C("bundle-load", {"bundle": {**BUNDLE, "lists": [
    {**BUNDLE["lists"][0], "rules": [
        {**BUNDLE["lists"][0]["rules"][0], "action": "redirect"}]}]}})
C("bundle-load", {"bundle": {**BUNDLE, "lists": [
    {**BUNDLE["lists"][0], "rules": [
        BUNDLE["lists"][0]["rules"][0], BUNDLE["lists"][0]["rules"][0]]}]}})
C("bundle-load", {})
import hashlib  # noqa: E402


def lbytes(lst):
    rules = []
    for r in lst["rules"]:
        ro = {"action": r["action"], "filter": r["filter"], "id": r["id"],
              "kind": r["kind"]}
        if r.get("resource"):
            ro["resource"] = r["resource"]
        if r.get("domains"):
            ro["domains"] = r["domains"]
        if r.get("exclude_domains"):
            ro["exclude_domains"] = r["exclude_domains"]
        rules.append(ro)
    return json.dumps({"attribution": lst["attribution"], "name": lst["name"],
                       "rules": rules}, sort_keys=True,
                      separators=(",", ":"), ensure_ascii=True)


GOOD_MAN = {"schema_version": 1, "bundle_id": "b-1", "created_epoch": 100,
            "lists": [{"name": "l1", "sha256": hashlib.sha256(
                lbytes(BUNDLE["lists"][0]).encode()).hexdigest(),
                "rules": len(BUNDLE["lists"][0]["rules"])}],
            "key_pin": "ed25519:test"}
C("bundle-check", {"bundle": BUNDLE, "manifest": GOOD_MAN})
C("bundle-check", {"bundle": BUNDLE, "manifest": {**GOOD_MAN, "lists": [
    {**GOOD_MAN["lists"][0], "rules": 1}]}})
C("bundle-check", {"bundle": BUNDLE, "manifest": {**GOOD_MAN, "lists": [
    {**GOOD_MAN["lists"][0], "sha256": "0" * 64}]}})
C("bundle-check", {"bundle": BUNDLE, "manifest": {**GOOD_MAN, "lists": [
    {**GOOD_MAN["lists"][0], "name": "other"}]}})
C("bundle-check", {"bundle": BUNDLE, "manifest": {**GOOD_MAN, "lists": []}})
C("bundle-check", {"bundle": BUNDLE, "manifest": {**GOOD_MAN, "extra": 1}})
C("bundle-check", {"bundle": BUNDLE, "manifest": {**GOOD_MAN, "lists": [
    {**GOOD_MAN["lists"][0], "license": "MIT"}]}})
C("bundle-check", {"bundle": BUNDLE})
C("bundle-check", {"bundle": BUNDLE, "manifest": {"lists": "x"}})

# apply
C("apply", {"bundle": BUNDLE, "state": S0, "now_mono": 100})
S1 = {"active": {"present": True, "bundle_id": "xr-default",
                 "digest": "a" * 64, "version": 3},
      "lkg": {"present": False},
      "pins": [{"present": True, "bundle_id": "xr-default",
                "digest": "a" * 64, "version": 3}],
      "last_apply_mono": 100}
C("apply", {"bundle": BUNDLE, "state": S1, "now_mono": 200})
C("apply", {"bundle": {**BUNDLE, "bundle_version": 2}, "state": S1,
            "now_mono": 200})
C("apply", {"bundle": {**BUNDLE, "bundle_version": 4}, "state": S1,
            "now_mono": 200})
C("apply", {"bundle": BUNDLE, "state": S1, "now_mono": 50})
C("apply", {"bundle": BUNDLE, "state": {**S1, "pins": []}, "now_mono": 300})
C("apply", {"bundle": BUNDLE, "state": {**S1, "pins": S1["pins"] * 3},
            "now_mono": 300})
C("apply", {"bundle": BUNDLE, "state": {**S1, "lkg": S1["active"]},
            "now_mono": 300})
C("apply", {"bundle": BUNDLE, "state": S0})
C("apply", {"bundle": BUNDLE, "state": {"active": {"present": True},
                                        "lkg": {"present": False},
                                        "pins": [], "last_apply_mono": -1},
            "now_mono": 1})
C("apply", {"bundle": BUNDLE, "state": {**S0, "x": 1}, "now_mono": 1})
C("apply", {"bundle": BUNDLE, "now_mono": 1})
C("apply", {"state": S0, "now_mono": 1})

# protocol-level
C("bogus", {})
C("match", CTX, raw="not json at all")
C("match", CTX, raw=json.dumps(["array", "frame"]))
C("match", CTX, raw=json.dumps({"method": "match", "args": 5}))
C("match", CTX, raw=json.dumps({"nomethod": 1}))

FAILURES = 0


def run(cmd, frame, flags):
    r = subprocess.run(cmd + flags, input=frame, capture_output=True,
                       text=True, timeout=60)
    return r.stdout.strip("\n"), r.returncode


def main() -> int:
    global FAILURES
    if not HOST.exists():
        print(f"missing host binary: {HOST} (run make -C shield/tests build)")
        return 2
    for frame, note, flags in CASES:
        h_out, h_rc = run([str(HOST)], frame, flags)
        f_out, f_rc = run([sys.executable, str(FAKE)], frame, flags)
        if h_out != f_out or h_rc != f_rc:
            FAILURES += 1
            print(f"MISMATCH {note} flags={flags}\n  frame: {frame[:200]}\n"
                  f"  host(rc={h_rc}): {h_out[:300]}\n"
                  f"  fake(rc={f_rc}): {f_out[:300]}")
    print(f"differential smoke: {len(CASES)} cases, {FAILURES} mismatches")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
