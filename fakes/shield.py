#!/usr/bin/env python3
"""Behavioral fake for xr.mojom.Shield + the shield host protocol (P11-T2).

TWO surfaces in one file, by design (shield/host_protocol.md):

1. The FROZEN mojom surface (P5): methods ``Status`` / ``RecentEvents``
   with the ``{"ok": …}`` / ``{"error": "<ErrorCode>"}`` envelope of
   fakes/_base.py. The frozen fixture case (fakes/fixtures/shield-v1.json)
   and the deterministic fixed sample event stay BYTE-IDENTICAL to the P5
   freeze. ``ring`` is an accepted helper key (fakes/README.md test
   affordance) that replaces the fixed sample with a view over explicit
   state; absent ``ring``, the legacy behavior is preserved exactly.

2. The LIVING host surface (P11-T2): ``flag-status``, ``bundle-load``,
   ``bundle-check``, ``match``, ``posture``, ``apply`` — an INDEPENDENT
   Python implementation of xr-core/shield/host/shield_host.cc +
   shield/core (the second implementation the golden vectors
   cross-check). Byte-parity against the compiled C++ host is asserted by
   xr-browser tools/shield_vectors_check.py over
   docs/contracts/vectors/shield-v1.json.

Laws honored here: deterministic (no clock, no RNG, no environment, no
I/O — every state rides in the request), deny-on-unknown everywhere,
typed refusals with closed-vocabulary detail tokens, exit 0 ok/rejected /
1 typed error / 2 usage. Stdlib only. Python 3.12+.
"""
from __future__ import annotations

import hashlib
import json
import sys
from typing import Any

from _base import ERROR_CODES, err as frozen_err, ok as frozen_ok

SEP = "\x01"  # the "^" separator sentinel inside parsed filter segments
RING_CAPACITY = 256
CHUNK_BUDGET = 65536

ACTIONS = {"block", "allow", "redirect", "replace"}
KINDS = {"network", "cosmetic", "redirect", "resource"}
REQUEST_CLASSES = ["kNavigation", "kSubresource", "kScript", "kPermission",
                   "kStorage", "kNetwork"]
TAB_TYPES = {"normal", "incognito", "workspace"}
BLOCK_ACTIONS = ["kBlocked", "kAllowed", "kRedirected", "kUpgraded"]


def canonical(obj: Any) -> str:
    """Byte-stable canonical JSON: sorted keys, no spaces, \\uXXXX ASCII."""
    return json.dumps(obj, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True)


def is_int(v: Any) -> bool:
    # Python bools are ints — the C++ JSON model says they are NOT
    return isinstance(v, int) and not isinstance(v, bool)


def ascii_lower(s: str) -> str:
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in s)


class Refusal(Exception):
    """A typed outcome: .obj is the canonical output, .rc the exit code."""

    def __init__(self, obj: dict, rc: int) -> None:
        super().__init__(canonical(obj))
        self.obj = obj
        self.rc = rc


def fail(code: str, detail: str) -> Refusal:
    return Refusal({"detail": detail, "error": code}, 1)


def reject(reason: str) -> Refusal:
    return Refusal({"error": "kRejected", "reason": reason}, 0)


def frozen_fail(code: str) -> Refusal:
    assert code in ERROR_CODES
    return Refusal({"error": code}, 0)


def only_keys(obj: dict, allowed: list[str]) -> None:
    for k in obj:
        if k not in allowed:
            raise fail("kMalformedInput", f"unknown-field:{k}")


# --------------------------------------------------------------------------
# URL splitting (mirror of shield/core/context.cc SplitUrl)
# --------------------------------------------------------------------------

def split_url(url: str) -> dict | None:
    pos = url.find("://")
    if pos <= 0:
        return None
    scheme = ascii_lower(url[:pos])
    if scheme not in ("http", "https"):
        return None
    rest = url[pos + 3:]
    if not rest or "@" in rest:
        return None  # userinfo is refused, never parsed
    path_start = rest.find("/")
    host_end = len(rest)
    for ch in "/?#":  # the authority ends at the first of / ? #
        i = rest.find(ch)
        if i != -1:
            host_end = min(host_end, i)
    host_port = rest[:host_end]
    if not host_port:
        return None
    host = ascii_lower(host_port)
    colon = host.find(":")
    if colon != -1:
        port = host_port[colon + 1:]
        if not port or any(c not in "0123456789" for c in port):
            return None
        host = host[:colon]
    if not host:
        return None
    tail = rest[path_start:] if (path_start != -1 and path_start <= host_end) \
        else "/"
    cut = len(tail)
    for ch in "?#":
        i = tail.find(ch)
        if i != -1:
            cut = min(cut, i)
    path = tail[:cut]
    if not path.startswith("/"):
        path = "/" + path
    return {"host": host, "path": path, "scheme": scheme}


# --------------------------------------------------------------------------
# Request context (mirror of ParseRequestContext)
# --------------------------------------------------------------------------

def parse_context(args: Any) -> dict:
    """Returns the parsed context dict or raises Refusal (kMalformedInput)."""
    if not isinstance(args, dict):
        raise fail("kMalformedInput", "context-not-object")
    only_keys(args, ["identity", "origin", "url", "request_class",
                     "first_party", "tab_type", "workspace"])
    ident = args.get("identity")
    if not isinstance(ident, dict):
        raise fail("kMalformedInput", "identity-not-object")
    for k in ident:
        if k != "value":
            raise fail("kMalformedInput", f"unknown-field:identity.{k}")
    if "value" not in ident or not isinstance(ident["value"], str):
        raise fail("kMalformedInput", "missing-required-field:value")
    if ident["value"] == "":
        raise fail("kMalformedInput", "empty-identity")
    org = args.get("origin")
    if not isinstance(org, dict):
        raise fail("kMalformedInput", "origin-not-object")
    for k in org:
        if k not in ("scheme", "registrable_domain"):
            raise fail("kMalformedInput", f"unknown-field:origin.{k}")
    for key in ("scheme", "registrable_domain"):
        if key not in org or not isinstance(org[key], str):
            raise fail("kMalformedInput", f"missing-required-field:{key}")
    if org["scheme"] not in ("http", "https"):
        raise fail("kMalformedInput", "unknown-origin-scheme")
    if org["registrable_domain"] == "":
        raise fail("kMalformedInput", "empty-registrable-domain")
    url = args.get("url")
    if not isinstance(url, str):
        raise fail("kMalformedInput", "missing-required-field:url")
    parts = split_url(url)
    if parts is None:
        raise fail("kMalformedInput", "unparsable-url")
    rc = args.get("request_class")
    if not isinstance(rc, str):
        raise fail("kMalformedInput", "missing-required-field:request_class")
    if rc not in REQUEST_CLASSES:
        raise fail("kMalformedInput", "unknown-request-class")
    first_party = False
    if "first_party" in args:
        if not isinstance(args["first_party"], bool):
            raise fail("kMalformedInput", "field-not-bool:first_party")
        first_party = args["first_party"]
    tab_type = "normal"
    if "tab_type" in args:
        if not isinstance(args["tab_type"], str) or \
                args["tab_type"] not in TAB_TYPES:
            raise fail("kMalformedInput", "unknown-tab-type")
        tab_type = args["tab_type"]
    workspace = ""
    if "workspace" in args:
        if not isinstance(args["workspace"], str):
            raise fail("kMalformedInput", "field-not-string:workspace")
        workspace = args["workspace"]
    return {"first_party": first_party,
            "identity": {"value": ident["value"]},
            "origin": {"registrable_domain": org["registrable_domain"],
                       "scheme": org["scheme"]},
            "parts": parts,
            "request_class": rc,
            "tab_type": tab_type,
            "url": url,
            "workspace": workspace}


# --------------------------------------------------------------------------
# Filter grammar v1 (mirror of ParseFilter)
# --------------------------------------------------------------------------

def parse_filter(f: Any) -> dict:
    """Returns the parsed filter or raises Refusal (kRejected token)."""
    def bad(token: str) -> Refusal:
        return reject(token)

    if not isinstance(f, str) or f == "":
        raise bad("empty-filter")
    if f[0] == "!":
        raise bad("unsupported-directive:comment")
    if len(f) >= 2 and f[0] == "/" and f[-1] == "/":
        raise bad("unsupported-directive:regex")
    if "$" in f:
        raise bad("unsupported-directive:options-in-filter")
    if "#" in f:
        raise bad("unsupported-directive:cosmetic-hash")
    if "@" in f:
        raise bad("unsupported-directive:at-syntax")
    body = f
    domain_anchor = False
    left_anchor = False
    right_anchor = False
    if body.startswith("||"):
        domain_anchor = True
        body = body[2:]
    elif body.startswith("|"):
        left_anchor = True
        body = body[1:]
    if body.endswith("|") and body != "":
        right_anchor = True
        body = body[:-1]
    if "|" in body:
        raise bad("unsupported-directive:interior-pipe")
    if body == "":
        raise bad("empty-filter")
    segments: list[str] = []
    cur = ""
    for c in body:
        if c == "*":
            if cur != "" or not segments:
                segments.append(cur)
                cur = ""
            continue
        cur += SEP if c == "^" else c
    segments.append(cur)
    for i in range(1, len(segments) - 1):
        if segments[i] == "":
            raise bad("unsupported-directive:empty-segment")
    return {"domain_anchor": domain_anchor, "left_anchor": left_anchor,
            "right_anchor": right_anchor, "segments": segments}


# --------------------------------------------------------------------------
# Bundle (mirror of ParseBundle / ListCanonicalBytes / CheckAgainstManifest)
# --------------------------------------------------------------------------

def list_canonical_bytes(lst: dict) -> str:
    rules = []
    for r in lst["rules"]:
        ro: dict = {"action": r["action"], "filter": r["filter"],
                    "id": r["id"], "kind": r["kind"]}
        if r.get("resource"):
            ro["resource"] = r["resource"]
        if r.get("domains"):
            ro["domains"] = r["domains"]
        if r.get("exclude_domains"):
            ro["exclude_domains"] = r["exclude_domains"]
        rules.append(ro)
    return canonical({"attribution": lst["attribution"], "name": lst["name"],
                      "rules": rules})


def parse_bundle(raw: Any) -> dict:
    """Normalized bundle or Refusal. Malformed => typed error (exit 1);
    unsupported directive => rejection (exit 0) — the C++ split."""
    if not isinstance(raw, dict):
        raise fail("kMalformedInput", "bundle-not-object")
    only_keys(raw, ["schema", "schema_version", "name", "bundle_version",
                    "lists", "refusals"])
    if raw.get("schema") != "xr-list-bundle":
        raise fail("kMalformedInput", "wrong-schema")
    if not is_int(raw.get("schema_version")) or raw["schema_version"] != 1:
        raise fail("kMalformedInput", "wrong-schema-version")
    name = raw.get("name")
    if not isinstance(name, str):
        raise fail("kMalformedInput", "missing-or-not-string:name")
    if name == "":
        raise fail("kMalformedInput", "empty-name")
    bv = raw.get("bundle_version")
    if not is_int(bv) or bv <= 0:
        raise fail("kMalformedInput", "non-positive-bundle-version")
    if "lists" not in raw or not isinstance(raw["lists"], list):
        raise fail("kMalformedInput", "lists-not-array")
    lists = []
    for lv in raw["lists"]:
        if not isinstance(lv, dict):
            raise fail("kMalformedInput", "list-not-object")
        only_keys(lv, ["name", "attribution", "rules"])
        lname = lv.get("name")
        if not isinstance(lname, str):
            raise fail("kMalformedInput", "missing-or-not-string:name")
        if lname == "":
            raise fail("kMalformedInput", "empty-list-name")
        attr = lv.get("attribution")
        if not isinstance(attr, str):
            raise fail("kMalformedInput", "missing-or-not-string:attribution")
        if "rules" not in lv or not isinstance(lv["rules"], list):
            raise fail("kMalformedInput", "rules-not-array")
        rules = []
        for rv in lv["rules"]:
            if not isinstance(rv, dict):
                raise fail("kMalformedInput", "rule-not-object")
            only_keys(rv, ["id", "kind", "filter", "action", "resource",
                           "domains", "exclude_domains"])
            rid = rv.get("id")
            if not isinstance(rid, str):
                raise fail("kMalformedInput", "missing-or-not-string:id")
            if rid == "":
                raise fail("kMalformedInput", "empty-rule-id")
            kind = rv.get("kind")
            if not isinstance(kind, str):
                raise fail("kMalformedInput", "missing-or-not-string:kind")
            if kind not in KINDS:
                raise fail("kMalformedInput", "unknown-rule-kind")
            filt = rv.get("filter")
            if not isinstance(filt, str):
                raise fail("kMalformedInput", "missing-or-not-string:filter")
            try:
                parsed = parse_filter(filt)
            except Refusal as ref:  # grammar refusal => rejection token
                raise reject(ref.obj["reason"]) from None
            action = rv.get("action")
            if not isinstance(action, str):
                raise fail("kMalformedInput", "missing-or-not-string:action")
            if action not in ACTIONS:
                raise fail("kMalformedInput", "unknown-action")
            resource = ""
            if "resource" in rv:
                if not isinstance(rv["resource"], str) or rv["resource"] == "":
                    raise fail("kMalformedInput", "bad-resource")
                resource = rv["resource"]
            if action in ("redirect", "replace") and resource == "":
                raise fail("kMalformedInput", "redirect-without-resource")
            if action not in ("redirect", "replace") and resource != "":
                raise fail("kMalformedInput", "resource-without-redirect")
            doms: dict[str, list[str]] = {}
            for key in ("domains", "exclude_domains"):
                if key in rv:
                    dv = rv[key]
                    if not isinstance(dv, list):
                        raise fail("kMalformedInput",
                                   f"domains-not-array:{key}")
                    for d in dv:
                        if not isinstance(d, str) or d == "":
                            raise fail("kMalformedInput",
                                       f"bad-domain-entry:{key}")
                    doms[key] = list(dv)
            for prev in rules:
                if prev["id"] == rid:
                    raise fail("kMalformedInput", f"duplicate-rule-id:{rid}")
            rules.append({"action": action, "domains": doms.get("domains", []),
                          "exclude_domains": doms.get("exclude_domains", []),
                          "filter": filt, "id": rid, "kind": kind,
                          "parsed": parsed, "resource": resource})
        lists.append({"attribution": attr, "name": lname, "rules": rules})
    refusals = []
    if "refusals" in raw:
        refs = raw["refusals"]
        if not isinstance(refs, list):
            raise fail("kMalformedInput", "refusals-not-array")
        for fv in refs:
            if not isinstance(fv, dict):
                raise fail("kMalformedInput", "refusal-not-object")
            only_keys(fv, ["directive", "reason", "count"])
            for key in ("directive", "reason"):
                if not isinstance(fv.get(key), str):
                    raise fail("kMalformedInput",
                               f"missing-or-not-string:{key}")
            if not is_int(fv.get("count")) or fv["count"] < 0:
                raise fail("kMalformedInput", "bad-refusal-count")
            refusals.append({"count": fv["count"],
                             "directive": fv["directive"],
                             "reason": fv["reason"]})
    joined = "[" + ",".join(list_canonical_bytes(l) for l in lists) + "]"
    digest = hashlib.sha256(joined.encode("utf-8")).hexdigest()
    return {"bundle_version": bv, "digest": digest, "lists": lists,
            "name": name, "refusals": refusals}


def bundle_summary(b: dict) -> dict:
    return {"bundle_version": b["bundle_version"], "digest": b["digest"],
            "lists": [{"name": l["name"], "rules": len(l["rules"])}
                      for l in b["lists"]],
            "name": b["name"], "refusals": b["refusals"]}


def check_against_manifest(bundle: dict, man: list[dict]) -> str | None:
    """None when bound; the refusal token otherwise (mirror of the C++)."""
    if len(man) != len(bundle["lists"]):
        return "manifest-list-count"
    for entry, bl in zip(man, bundle["lists"]):
        if entry["name"] != bl["name"]:
            return f"manifest-list-name:{entry['name']}"
        if "attribution" in entry and \
                entry["attribution"] != bl["attribution"]:
            return f"manifest-attribution:{bl['name']}"
        if entry["rules"] != len(bl["rules"]):
            return f"manifest-rule-count:{bl['name']}"
        got = hashlib.sha256(
            list_canonical_bytes(bl).encode("utf-8")).hexdigest()
        if entry["sha256"] != got:
            return f"manifest-sha256:{bl['name']}"
    return None


def parse_manifest(man: Any) -> list[dict]:
    if not isinstance(man, dict):
        raise fail("kMalformedInput", "manifest-not-object")
    only_keys(man, ["schema_version", "bundle_id", "created_epoch", "lists",
                    "key_pin", "last_known_good_bundle_id"])
    lists = man.get("lists")
    if not isinstance(lists, list):
        raise fail("kMalformedInput", "manifest-lists-not-array")
    out = []
    for lv in lists:
        if not isinstance(lv, dict):
            raise fail("kMalformedInput", "manifest-entry-not-object")
        only_keys(lv, ["name", "sha256", "rules", "attribution"])
        if not isinstance(lv.get("name"), str) or lv["name"] == "" or \
                not isinstance(lv.get("sha256"), str) or \
                len(lv["sha256"]) != 64 or not is_int(lv.get("rules")) or \
                lv["rules"] < 0:
            raise fail("kMalformedInput", "bad-manifest-entry")
        attr = lv.get("attribution")
        if attr is not None and (not isinstance(attr, str) or attr == ""):
            raise fail("kMalformedInput", "bad-manifest-entry")
        entry = {"name": lv["name"], "rules": lv["rules"],
                 "sha256": lv["sha256"]}
        if attr is not None:
            entry["attribution"] = attr
        out.append(entry)
    return out


# --------------------------------------------------------------------------
# Posture (mirror of DecidePosture — the asymmetry law)
# --------------------------------------------------------------------------

def decide_posture(pin: dict) -> dict:
    if not pin["route_bound"]:
        return {"chip": "red", "mode": "fail-closed", "reason": "route-loss"}
    if not pin["engine_alive"]:
        return {"chip": "amber", "mode": "fail-open", "reason": "engine-dead"}
    if pin["engine_poisoned"]:
        return {"chip": "amber", "mode": "fail-open",
                "reason": "engine-poisoned"}
    if pin["kill_switch_on"]:
        return {"chip": "amber", "mode": "fail-open", "reason": "kill-switch"}
    return {"chip": "green", "mode": "normal", "reason": "normal"}


WHY_FROM_POSTURE = {"route-loss": "route-loss-fail-closed",
                    "engine-dead": "engine-dead-fail-open",
                    "engine-poisoned": "engine-poisoned-fail-open",
                    "kill-switch": "kill-switch"}


def parse_posture_args(args: dict) -> dict:
    pin = {"engine_alive": True, "engine_poisoned": False,
           "kill_switch_on": False, "route_bound": True}
    for key in pin:
        if key in args:
            if not isinstance(args[key], bool):
                raise fail("kMalformedInput", f"field-not-bool:{key}")
            pin[key] = args[key]
    return pin


# --------------------------------------------------------------------------
# Scopes (mirror of ParseScopeSet / Covers / SweepAsOf)
# --------------------------------------------------------------------------

def parse_scopes(sv: Any) -> list[dict]:
    if not isinstance(sv, list):
        raise fail("kMalformedInput", "scopes-not-array")
    out = []
    for s in sv:
        if not isinstance(s, dict):
            raise fail("kMalformedInput", "scope-not-object")
        only_keys(s, ["scope_id", "identity", "site", "workspace", "rule_id",
                      "list_id", "expiry_mono", "reason"])
        sid = s.get("scope_id")
        if not isinstance(sid, str) or sid == "":
            raise fail("kMalformedInput", "bad-scope-id")
        for prev in out:
            if prev["scope_id"] == sid:
                raise fail("kMalformedInput", f"duplicate-scope-id:{sid}")
        scope = {"expiry_mono": -1, "identity": "", "list_id": "",
                 "reason": "", "rule_id": "", "scope_id": sid, "site": "",
                 "workspace": ""}
        for key in ("identity", "site", "workspace", "rule_id", "list_id"):
            if key in s:
                if not isinstance(s[key], str):
                    raise fail("kMalformedInput", f"field-not-string:{key}")
                scope[key] = s[key]
        if "expiry_mono" in s:
            if not is_int(s["expiry_mono"]) or s["expiry_mono"] < -1:
                raise fail("kMalformedInput", "bad-expiry")
            scope["expiry_mono"] = s["expiry_mono"]
        reason = s.get("reason")
        if not isinstance(reason, str) or reason == "":
            raise fail("kMalformedInput", f"missing-reason:{sid}")
        scope["reason"] = reason
        out.append(scope)
    return out


def covers(scope: dict, ctx: dict, hit: dict, now_mono: int) -> bool:
    if scope["expiry_mono"] >= 0 and now_mono >= scope["expiry_mono"]:
        return False
    if scope["identity"] and scope["identity"] != ctx["identity"]["value"]:
        return False  # the coupling law: identity A != identity B
    if scope["site"] and \
            scope["site"] != ctx["origin"]["registrable_domain"]:
        return False
    if scope["workspace"] and scope["workspace"] != ctx["workspace"]:
        return False
    if scope["rule_id"] and scope["rule_id"] != hit["rule_id"]:
        return False
    if scope["list_id"] and scope["list_id"] != hit["list_id"]:
        return False
    return True


# --------------------------------------------------------------------------
# Apply state (mirror of ParseApplyState / CheckInvariants / ApplyBundle)
# --------------------------------------------------------------------------

def parse_slot(v: Any, name: str) -> dict:
    if not isinstance(v, dict):
        raise fail("kMalformedInput", f"slot-not-object:{name}")
    only_keys(v, ["present", "bundle_id", "digest", "version"])
    if not isinstance(v.get("present"), bool):
        raise fail("kMalformedInput", f"bad-present:{name}")
    slot = {"present": v["present"]}
    if not slot["present"]:
        return slot
    if not isinstance(v.get("bundle_id"), str) or v["bundle_id"] == "" or \
            not isinstance(v.get("digest"), str) or len(v["digest"]) != 64 or \
            not is_int(v.get("version")) or v["version"] < 1:
        raise fail("kMalformedInput", f"bad-slot-fields:{name}")
    slot.update({"bundle_id": v["bundle_id"], "digest": v["digest"],
                 "version": v["version"]})
    return slot


def parse_state(st: Any) -> dict:
    if not isinstance(st, dict):
        raise fail("kMalformedInput", "state-not-object")
    only_keys(st, ["active", "lkg", "pins", "last_apply_mono"])
    state = {"active": parse_slot(st.get("active"), "active"),
             "lkg": parse_slot(st.get("lkg"), "lkg")}
    if "pins" not in st or not isinstance(st["pins"], list):
        raise fail("kMalformedInput", "pins-not-array")
    state["pins"] = [parse_slot(pv, f"pins[{i}]")
                     for i, pv in enumerate(st["pins"])]
    lam = st.get("last_apply_mono")
    if not is_int(lam) or lam < -1:
        raise fail("kMalformedInput", "bad-last-apply-mono")
    state["last_apply_mono"] = lam
    return state


def check_invariants(state: dict) -> str | None:
    if len(state["pins"]) > 2:
        return "too-many-pins"
    if state["active"]["present"]:
        if not any(p["present"] and
                   p.get("bundle_id") == state["active"]["bundle_id"] and
                   p.get("version") == state["active"]["version"]
                   for p in state["pins"]):
            return "pins-missing-active"  # the hot-pin-out refusal
    if state["lkg"]["present"] and state["active"]["present"] and \
            state["lkg"].get("bundle_id") == state["active"].get("bundle_id") \
            and state["lkg"].get("version") == state["active"].get("version"):
        return "lkg-duplicates-active"
    return None


def slot_json(slot: dict) -> dict:
    if not slot["present"]:
        return {"present": False}
    return {"bundle_id": slot["bundle_id"], "digest": slot["digest"],
            "present": True, "version": slot["version"]}


def state_json(state: dict) -> dict:
    return {"active": slot_json(state["active"]),
            "last_apply_mono": state["last_apply_mono"],
            "lkg": slot_json(state["lkg"]),
            "pins": [slot_json(p) for p in state["pins"]]}


def apply_bundle(cand: dict, state: dict, now_mono: int) -> dict:
    if state["last_apply_mono"] >= 0 and now_mono < state["last_apply_mono"]:
        raise reject("stale-apply-clock")
    out = {"active": dict(state["active"]), "lkg": dict(state["lkg"]),
           "last_apply_mono": now_mono, "pins": []}
    if state["active"]["present"]:
        if cand["name"] == state["active"]["bundle_id"]:
            if cand["bundle_version"] < state["active"]["version"]:
                raise reject("bundle-version-downgrade")
            if cand["bundle_version"] == state["active"]["version"]:
                raise reject("bundle-version-equal-reoffer")
        out["lkg"] = dict(state["active"])
    ns = {"bundle_id": cand["name"], "digest": cand["digest"],
          "present": True, "version": cand["bundle_version"]}
    out["active"] = ns
    if out["lkg"]["present"]:
        out["pins"].append(out["lkg"])
    out["pins"].append(ns)
    return out


# --------------------------------------------------------------------------
# Events (mirror of events.cc — redaction AT CREATION, frozen k-spellings)
# --------------------------------------------------------------------------

def redact_target(parts: dict) -> str:
    return f"{parts['scheme']}://{parts['host']}{parts['path']}"


def event_json(e: dict) -> dict:
    return {"action": e["action"],
            "identity": {"value": e["identity"]},
            "list_provenance": e["list_provenance"],
            "origin": {"registrable_domain": e["origin"]["registrable_domain"],
                       "scheme": e["origin"]["scheme"]},
            "request_class": e["request_class"],
            "rule": e["rule"],
            "tab_id": e["tab_id"],
            "target": e["target"],
            "ts_millis": e["ts_millis"]}


def ring_append(ring: list[dict], event: dict) -> None:
    ring.append(event)
    del ring[: max(0, len(ring) - RING_CAPACITY)]  # FIFO eviction


def parse_ring(v: Any) -> list[dict]:
    if not isinstance(v, list):
        raise fail("kMalformedInput", "ring-not-array")
    ring: list[dict] = []
    for ev in v:
        if not isinstance(ev, dict):
            raise fail("kMalformedInput", "event-not-object")
        only_keys(ev, ["ts_millis", "identity", "tab_id", "origin", "target",
                       "rule", "list_provenance", "action", "request_class"])
        ident = ev.get("identity")
        org = ev.get("origin")
        if not is_int(ev.get("ts_millis")) or not is_int(ev.get("tab_id")) or \
                not isinstance(ident, dict) or \
                not isinstance(ev.get("target"), str) or \
                not isinstance(ev.get("rule"), str) or \
                not isinstance(ev.get("list_provenance"), str) or \
                not isinstance(ev.get("action"), str) or \
                not isinstance(ev.get("request_class"), str) or \
                not isinstance(org, dict):
            raise fail("kMalformedInput", "bad-event-field")
        if not isinstance(ident.get("value"), str) or \
                not isinstance(org.get("scheme"), str) or \
                not isinstance(org.get("registrable_domain"), str):
            raise fail("kMalformedInput", "bad-event-nested")
        if ev["action"] not in BLOCK_ACTIONS:
            raise fail("kMalformedInput",
                       f"unknown-block-action:{ev['action']}")
        if ev["request_class"] not in REQUEST_CLASSES:
            raise fail("kMalformedInput",
                       f"unknown-request-class:{ev['request_class']}")
        ring_append(ring, {
            "action": ev["action"], "identity": ident["value"],
            "list_provenance": ev["list_provenance"],
            "origin": {"registrable_domain": org["registrable_domain"],
                       "scheme": org["scheme"]},
            "request_class": ev["request_class"], "rule": ev["rule"],
            "tab_id": ev["tab_id"], "target": ev["target"],
            "ts_millis": ev["ts_millis"]})
    return ring


def ring_view(ring: list[dict], identity: str, max_events: int) -> list[dict]:
    out: list[dict] = []
    budget = 0
    cap = max(0, max_events)
    for e in reversed(ring):
        if len(out) >= cap:
            break
        if identity and e["identity"] != identity:
            continue
        j = event_json(e)
        sz = len(canonical(j))
        if out and budget + sz > CHUNK_BUDGET:
            break
        budget += sz
        out.append(j)
    return out


# --------------------------------------------------------------------------
# The v1 matcher (mirror of fake_engine.cc)
# --------------------------------------------------------------------------

def match_seg_at(t: str, seg: str, pos: int) -> int:
    p = pos
    for i, c in enumerate(seg):
        if c == SEP:
            if p == len(t):
                return p if i + 1 == len(seg) else -1
            if t[p] not in "/:":
                return -1
            p += 1
            continue
        if p >= len(t) or t[p] != c:
            return -1
        p += 1
    return p


def find_seg(t: str, seg: str, frm: int) -> int:
    for s in range(frm, len(t) + 1):
        e = match_seg_at(t, seg, s)
        if e >= 0:
            return e
    return -1


def match_segments(segs: list[str], target: str, left_anchor: bool,
                   right_anchor: bool) -> bool:
    if not segs:
        return False
    ra = right_anchor and segs[-1] != ""
    if ra:
        last = segs[-1]
        anchor_start = -1
        for s in range(len(target) + 1):
            if match_seg_at(target, last, s) == len(target):
                anchor_start = s
                break
        if anchor_start < 0:
            return False
        pos = 0
        for i, seg in enumerate(segs[:-1]):
            e = match_seg_at(target, seg, pos) if (i == 0 and left_anchor) \
                else find_seg(target, seg, pos)
            if e < 0 or e > anchor_start:
                return False
            pos = e
        return True
    pos = 0
    for i, seg in enumerate(segs):
        e = match_seg_at(target, seg, pos) if (i == 0 and left_anchor) \
            else find_seg(target, seg, pos)
        if e < 0:
            return False
        pos = e
    return True


def filter_match(pf: dict, parts: dict) -> bool:
    match_url = f"{parts['scheme']}://{parts['host']}{parts['path']}"
    if not pf["domain_anchor"]:
        return match_segments(pf["segments"], match_url, pf["left_anchor"],
                              pf["right_anchor"])
    seg0 = pf["segments"][0]
    cut = len(seg0)
    for i, c in enumerate(seg0):
        if c == SEP or c == "/":
            cut = i
            break
    d = seg0[:cut]
    host = parts["host"]
    host_ok = (d == "" or host == d or
               (len(host) > len(d) and host.endswith("." + d)))
    if not host_ok:
        return False
    rest = seg0[cut:]
    if pf["right_anchor"] and rest == "" and len(pf["segments"]) == 1:
        return parts["path"] == "/"  # "||d|" — the bare domain
    segs = [rest] + pf["segments"][1:]
    return match_segments(segs, parts["path"], True, pf["right_anchor"])


def table_match(bundle: dict, ctx: dict) -> dict | None:
    first: dict | None = None
    for lst in bundle["lists"]:
        for rule in lst["rules"]:
            if rule["kind"] == "cosmetic":
                continue
            rd = ctx["origin"]["registrable_domain"]
            host = ctx["parts"]["host"]
            if rule["domains"] and not any(d in (rd, host)
                                           for d in rule["domains"]):
                continue
            if rule["exclude_domains"] and any(d in (rd, host)
                                               for d in rule["exclude_domains"]):
                continue
            if not filter_match(rule["parsed"], ctx["parts"]):
                continue
            hit = {"action": rule["action"], "list_id": lst["name"],
                   "redirect_resource": rule["resource"], "rule_id": rule["id"]}
            if rule["action"] == "allow":
                return hit  # allow overrides block — earliest allow wins
            if first is None:
                first = hit
    return first


# --------------------------------------------------------------------------
# The decision pipeline (mirror of DecideMatch)
# --------------------------------------------------------------------------

def decide_match(ctx: dict, bundle: dict | None, scopes: list[dict],
                 now_mono: int, pin: dict) -> dict:
    posture = decide_posture(pin)
    bv = bundle["bundle_version"] if bundle else 0
    if posture["mode"] == "fail-closed":
        return {"fail_closed": True, "posture": posture,
                "verdict": {"action": "block", "bundle_version": bv,
                            "engine_decision": False, "list_id": "",
                            "rule_id": "",
                            "why_code": "route-loss-fail-closed"}}
    if posture["mode"] == "fail-open":
        return {"fail_closed": False, "posture": posture,
                "verdict": {"action": "allow", "bundle_version": bv,
                            "engine_decision": False, "list_id": "",
                            "rule_id": "",
                            "why_code": WHY_FROM_POSTURE[posture["reason"]]}}

    def allowed(why: str, engine: bool = False, rule: str = "",
                lst: str = "") -> dict:
        return {"fail_closed": False, "posture": posture,
                "verdict": {"action": "allow", "bundle_version": bv,
                            "engine_decision": engine, "list_id": lst,
                            "rule_id": rule, "why_code": why}}

    if bundle is None:
        return allowed("no-bundle")
    if not (pin["engine_alive"] and not pin["engine_poisoned"]):
        return allowed("engine-dead-fail-open")  # defense in depth
    hit = table_match(bundle, ctx)
    if hit is None:
        return allowed("no-match")
    if hit["action"] == "allow":
        return allowed("rule-allowed", True, hit["rule_id"], hit["list_id"])
    for scope in scopes:
        if covers(scope, ctx, hit, now_mono):
            return allowed("exception-scope", True, hit["rule_id"],
                           hit["list_id"])
    action = hit["action"]
    why = {"block": "rule-blocked", "redirect": "rule-redirected",
           "replace": "rule-replaced"}[action]
    return {"fail_closed": False, "posture": posture,
            "verdict": {"action": action, "bundle_version": bv,
                        "engine_decision": True, "list_id": hit["list_id"],
                        "rule_id": hit["rule_id"], "why_code": why}}


# --------------------------------------------------------------------------
# Methods
# --------------------------------------------------------------------------

def method_flag_status(args: dict, flag: str) -> tuple[dict, int]:
    only_keys(args, [])
    return {"xr_shield_v1": flag}, 0


def method_status(args: dict, flag: str) -> tuple[dict, int]:
    try:
        only_keys(args, ["identity", "origin", "ring"])
        ident = args.get("identity")
        if not isinstance(ident, dict) or \
                not isinstance(ident.get("value"), str) or \
                ident["value"] == "":
            raise frozen_fail("kUnknownIdentity")
        org = args.get("origin")
        scheme = org.get("scheme") if isinstance(org, dict) else None
        if scheme not in ("http", "https"):
            raise frozen_fail("kUnknownOrigin")
        ring: list[dict] = []
        if "ring" in args:
            ring = parse_ring(args["ring"])
        blocked = sum(1 for e in ring
                      if e["identity"] == ident["value"] and
                      e["action"] == "kBlocked")
        return frozen_ok({"blocked_count": blocked,
                          "enabled": flag == "on"}), 0
    except Refusal as ref:
        if "detail" in ref.obj:  # living typed error -> frozen envelope
            return frozen_fail("kMalformedInput").obj, 0
        return ref.obj, ref.rc


def method_recent_events(args: dict) -> tuple[dict, int]:
    try:
        only_keys(args, ["identity", "max_events", "ring"])
        ident = args.get("identity")
        if not isinstance(ident, dict) or \
                not isinstance(ident.get("value"), str) or \
                ident["value"] == "":
            raise frozen_fail("kMalformedInput")
        max_events = 0
        if "max_events" in args:
            if not is_int(args["max_events"]):
                raise frozen_fail("kMalformedInput")
            max_events = args["max_events"]
        if "ring" in args:
            ring = parse_ring(args["ring"])
            return frozen_ok(ring_view(ring, ident["value"], max_events)), 0
        # frozen affordance: the deterministic fixed sample (no clock),
        # identity echoed exactly as received
        n = max(0, min(max_events, 1))
        sample = {"action": "kBlocked", "identity": ident,
                  "list_provenance": "xr-default-list-v1",
                  "origin": {"registrable_domain": "tracker.example",
                             "scheme": "https"},
                  "request_class": "kScript", "rule": "||tracker.example^",
                  "tab_id": 1, "target": "https://tracker.example/a.js",
                  "ts_millis": 1000}
        return frozen_ok([sample] * n), 0
    except Refusal as ref:
        if "detail" in ref.obj:
            return frozen_fail("kMalformedInput").obj, 0
        return ref.obj, ref.rc


def method_posture(args: dict) -> tuple[dict, int]:
    only_keys(args, ["engine_alive", "engine_poisoned", "route_bound",
                     "kill_switch_on"])
    return decide_posture(parse_posture_args(args)), 0


def method_bundle_load(args: dict) -> tuple[dict, int]:
    only_keys(args, ["bundle"])
    if "bundle" not in args:
        raise fail("kMalformedInput", "missing-bundle")
    return bundle_summary(parse_bundle(args["bundle"])), 0


def method_bundle_check(args: dict) -> tuple[dict, int]:
    only_keys(args, ["bundle", "manifest"])
    if "bundle" not in args:
        raise fail("kMalformedInput", "missing-bundle")
    bundle = parse_bundle(args["bundle"])
    if "manifest" not in args:
        raise fail("kMalformedInput", "missing-manifest")
    man = parse_manifest(args["manifest"])
    token = check_against_manifest(bundle, man)
    if token is not None:
        raise reject(token)
    return {"bound": True, "bundle_version": bundle["bundle_version"],
            "digest": bundle["digest"], "list_count": len(man)}, 0


def method_match(args: dict) -> tuple[dict, int]:
    only_keys(args, ["context", "bundle", "scopes", "engine_alive",
                     "engine_poisoned", "route_bound", "kill_switch_on",
                     "now_mono"])
    if "context" not in args:
        raise fail("kMalformedInput", "missing-context")
    ctx = parse_context(args["context"])
    bundle = None
    if "bundle" in args:
        bundle = parse_bundle(args["bundle"])
    scopes = parse_scopes(args["scopes"]) if "scopes" in args else []
    pin = parse_posture_args(args)
    now_mono = 0
    if "now_mono" in args:
        if not is_int(args["now_mono"]) or args["now_mono"] < 0:
            raise fail("kMalformedInput", "bad-now-mono")
        now_mono = args["now_mono"]
    return decide_match(ctx, bundle, scopes, now_mono, pin), 0


def method_apply(args: dict) -> tuple[dict, int]:
    only_keys(args, ["bundle", "state", "now_mono"])
    if "bundle" not in args:
        raise fail("kMalformedInput", "missing-bundle")
    bundle = parse_bundle(args["bundle"])
    if "state" not in args:
        raise fail("kMalformedInput", "missing-state")
    state = parse_state(args["state"])
    token = check_invariants(state)
    if token is not None:
        raise reject(token)  # hot-pin-out and friends: refused, not repaired
    if "now_mono" not in args or not is_int(args["now_mono"]) or \
            args["now_mono"] < 0:
        raise fail("kMalformedInput", "missing-now-mono")
    return {"state": state_json(apply_bundle(bundle, state,
                                             args["now_mono"]))}, 0


# ---- P11-T4: the exception surface (scope.h toggle mechanics) --------------
# State rides in the request (v1 host is stateless): the scope set is an
# arg, the resulting set is the response. Per-site toggle =
# exception-add/remove of the canonical scope_id "site-toggle:<site>"
# (fixed ledger-friendly reason); dynamic rule add/remove =
# exception-add/remove of rule_id/list_id-scoped scopes; expiry sweep is
# the deterministic core job (sweep_as_of) as a method — the as-of is the
# now_mono ARG, never a wall clock. Refusal split: scope-document parse
# errors stay kMalformedInput (exit 1, T2 law untouched); CONTENT
# conflicts with the existing set are kRejected (exit 0) — re-presenting
# an equal offer is a refusal (equal-reoffer precedent).

SCOPE_KEYS = ("expiry_mono", "identity", "list_id", "reason", "rule_id",
              "scope_id", "site", "workspace")


def scope_to_json(s: dict) -> dict:
    """Canonical wire form of one scope (all 8 keys) — ScopeToJson mirror."""
    return {k: s[k] for k in SCOPE_KEYS}


def scopes_arg(args: dict) -> list[dict]:
    """Parse the optional `scopes` arg (absent = empty set), T2 grammar."""
    if "scopes" not in args:
        return []
    return parse_scopes(args["scopes"])


def scopes_out(scopes: list[dict]) -> list[dict]:
    return [scope_to_json(s) for s in scopes]


def method_exception_add(args: dict) -> tuple[dict, int]:
    only_keys(args, ["scopes", "scope"])
    scopes = scopes_arg(args)
    if "scope" not in args:
        raise fail("kMalformedInput", "missing-scope")
    one = parse_scopes([args["scope"]])
    for prev in scopes:
        if prev["scope_id"] == one[0]["scope_id"]:
            raise reject(f"duplicate-scope-id:{prev['scope_id']}")
    return {"scopes": scopes_out(scopes + one)}, 0


def method_exception_remove(args: dict) -> tuple[dict, int]:
    only_keys(args, ["scopes", "scope_id"])
    scopes = scopes_arg(args)
    sid = args.get("scope_id")
    if not isinstance(sid, str) or sid == "":
        raise fail("kMalformedInput", "bad-scope-id")
    kept = [s for s in scopes if s["scope_id"] != sid]
    if len(kept) == len(scopes):
        raise reject(f"unknown-scope-id:{sid}")
    return {"scopes": scopes_out(kept)}, 0


def method_exception_sweep(args: dict) -> tuple[dict, int]:
    only_keys(args, ["scopes", "now_mono"])
    scopes = scopes_arg(args)
    nm = args.get("now_mono")
    if not is_int(nm) or nm < 0:
        raise fail("kMalformedInput", "missing-now-mono")  # apply's token:
    kept, swept = [], []                                   # REQUIRED and >=0
    for s in scopes:
        if s["expiry_mono"] >= 0 and nm >= s["expiry_mono"]:
            swept.append(s["scope_id"])
        else:
            kept.append(s)
    return {"scopes": scopes_out(kept), "swept": swept}, 0


def method_site_toggle(args: dict) -> tuple[dict, int]:
    only_keys(args, ["scopes", "site", "on", "expiry_mono"])
    scopes = scopes_arg(args)
    site = args.get("site")
    if not isinstance(site, str) or site == "":
        raise fail("kMalformedInput", "bad-site")
    on = args.get("on")
    if not isinstance(on, bool):
        raise fail("kMalformedInput", "bad-toggle")
    expiry = -1
    if "expiry_mono" in args:
        if not is_int(args["expiry_mono"]) or args["expiry_mono"] < -1:
            raise fail("kMalformedInput", "bad-expiry")
        expiry = args["expiry_mono"]
    sid = f"site-toggle:{site}"
    found = any(s["scope_id"] == sid for s in scopes)
    if on:
        if found:
            raise reject(f"toggle-already-on:{site}")
        scopes = scopes + [{"expiry_mono": expiry, "identity": "",
                            "list_id": "", "reason": "user-site-toggle",
                            "rule_id": "", "scope_id": sid, "site": site,
                            "workspace": ""}]
    else:
        if not found:
            raise reject(f"toggle-already-off:{site}")
        scopes = [s for s in scopes if s["scope_id"] != sid]
    return {"scopes": scopes_out(scopes), "scope_id": sid,
            "toggled": "on" if on else "off"}, 0


# P11-T5: living block-event-v1 row vocabulary (host_protocol.md, verbatim)
ROW_ACTIONS = ("kBlocked", "kAllowed", "kRedirected", "kUpgraded")
WHY_CODES = ("rule-blocked", "rule-allowed", "rule-redirected",
             "rule-replaced", "no-match", "no-bundle", "exception-scope",
             "engine-dead-fail-open", "engine-poisoned-fail-open",
             "kill-switch", "route-loss-fail-closed")


def ledger_row(ctx: dict, seq: int, ts_millis: int, tab_id: int,
               bundle_version: int, action: str, why_code: str,
               strings: dict) -> dict:
    """Mirror of MakeLedgerRow (events.cc): canonical living row, redaction
    at creation (ctx["parts"] -> scheme://host/path, never query/fragment).
    """
    return {"action": action, "bundle_version": bundle_version,
            "contract_version": 1, "event": "block_event",
            "identity": ctx["identity"]["value"],
            "list_id": strings.get("list_id", ""),
            "origin": {"registrable_domain": ctx["origin"]["registrable_domain"],
                       "scheme": ctx["origin"]["scheme"]},
            "request_class": ctx["request_class"],
            "rule": strings.get("rule", ""),
            "rule_id": strings.get("rule_id", ""), "seq": seq,
            "tab_id": tab_id, "target": redact_target(ctx["parts"]),
            "ts_millis": ts_millis, "why_code": why_code}


def method_event_emit(args: dict) -> tuple[dict, int]:
    only_keys(args, ["context", "tab_id", "ts_millis", "seq", "action",
                     "rule_id", "rule", "list_id", "bundle_version",
                     "why_code"])
    if "context" not in args:
        raise fail("kMalformedInput", "missing-context")
    ctx = parse_context(args["context"])
    seq = args.get("seq")
    if seq is None or not is_int(seq) or seq < 0:
        raise fail("kMalformedInput", "missing-seq")
    ts_millis = args.get("ts_millis")
    if ts_millis is None or not is_int(ts_millis) or ts_millis < 0:
        raise fail("kMalformedInput", "missing-ts-millis")
    tab_id = 0
    if "tab_id" in args:
        if not is_int(args["tab_id"]) or args["tab_id"] < 0:
            raise fail("kMalformedInput", "bad-tab-id")
        tab_id = args["tab_id"]
    bundle_version = 0
    if "bundle_version" in args:
        if not is_int(args["bundle_version"]) or args["bundle_version"] < 0:
            raise fail("kMalformedInput", "bad-bundle-version")
        bundle_version = args["bundle_version"]
    if "action" not in args:
        raise fail("kMalformedInput", "missing-action")
    action = args["action"]
    if not isinstance(action, str):
        raise fail("kMalformedInput", "field-not-string:action")
    if action not in ROW_ACTIONS:
        raise fail("kMalformedInput", f"bad-action:{action}")
    if "why_code" not in args:
        raise fail("kMalformedInput", "missing-why-code")
    why_code = args["why_code"]
    if not isinstance(why_code, str):
        raise fail("kMalformedInput", "field-not-string:why_code")
    if why_code not in WHY_CODES:
        raise fail("kMalformedInput", f"bad-why-code:{why_code}")
    strings: dict = {}
    for key in ("rule_id", "rule", "list_id"):
        if key in args:
            if not isinstance(args[key], str):
                raise fail("kMalformedInput", f"field-not-string:{key}")
            strings[key] = args[key]
    return {"row": ledger_row(ctx, seq, ts_millis, tab_id, bundle_version,
                              action, why_code, strings)}, 0


METHODS = {"apply": method_apply, "bundle-check": method_bundle_check,
           "bundle-load": method_bundle_load,
           "event-emit": method_event_emit,
           "exception-add": method_exception_add,
           "exception-remove": method_exception_remove,
           "exception-sweep": method_exception_sweep, "flag-status": None,
           "match": method_match, "posture": method_posture,
           "site-toggle": method_site_toggle}

USAGE = ("usage: shield.py <method> ['<json-args>'] [options]\n"
         "       shield.py '<json-with-method>' [options]\n"
         "       shield.py [options]   # request JSON on stdin\n"
         "options: --flag xr_shield_v1=on|off   (default on)\n"
         "methods: flag-status Status RecentEvents bundle-load\n"
         "         bundle-check match posture apply\n"
         "exception-add exception-remove exception-sweep site-toggle\n"
         "event-emit\n")


def call(method: str, args: Any, flag: str = "on") -> tuple[dict, int]:
    # flag defaults to "on" — the CLI's documented default. The default
    # matters for xrctl's generic two-arg call path (xr-browser
    # docs/contracts/tests/test_all_interfaces_parity.py): without it the
    # shield fake raised TypeError there since T2 (hosted-CI debt,
    # root-caused and fixed in P11-T5).
    if method == "flag-status":
        return method_flag_status(args if isinstance(args, dict) else {}, flag)
    if method == "Status":
        return method_status(args if isinstance(args, dict) else {}, flag)
    if method == "RecentEvents":
        return method_recent_events(args if isinstance(args, dict) else {})
    if not isinstance(args, dict):
        raise fail("kMalformedInput", "args not an object")
    fn = METHODS.get(method)
    if fn is None:
        raise fail("kUnknownMethod", method)
    return fn(args)


def main(argv: list[str]) -> int:
    flag = "on"
    positional: list[str] = []
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--flag" and i + 1 < len(argv):
            i += 1
            kv = argv[i]
            if "=" not in kv:
                print("usage error: --flag k=v", file=sys.stderr)
                return 2
            k, v = kv.split("=", 1)
            if k != "xr_shield_v1":
                print(f"usage error: unknown flag {k}", file=sys.stderr)
                return 2
            flag = v
        elif a.startswith("--"):
            print(f"usage error: unknown option {a}", file=sys.stderr)
            return 2
        else:
            positional.append(a)
        i += 1
    if flag not in ("on", "off"):
        print("usage error: xr_shield_v1 must be on|off", file=sys.stderr)
        return 2

    method = ""
    args_json = "{}"
    if not positional:
        raw = sys.stdin.read()
        try:
            frame = json.loads(raw)
        except Exception:  # noqa: BLE001
            obj, rc = fail("kMalformedInput", "bad stdin frame").obj, 1
            print(canonical(obj))
            return rc
        m = frame.get("method") if isinstance(frame, dict) else None
        method = m if isinstance(m, str) else ""
        args_json = canonical(frame["args"]) \
            if isinstance(frame, dict) and "args" in frame else "{}"
    else:
        frame = None
        try:
            parsed = json.loads(positional[0])
            if isinstance(parsed, dict) and "method" in parsed:
                frame = parsed
        except Exception:  # noqa: BLE001
            frame = None
        if frame is not None:
            m = frame.get("method")
            method = m if isinstance(m, str) else ""
            args_json = canonical(frame["args"]) if "args" in frame else "{}"
        elif len(positional) in (1, 2):
            method = positional[0]
            if len(positional) == 2:
                args_json = positional[1]
        else:
            print(USAGE, end="", file=sys.stderr)
            return 2
    if method == "":
        print(USAGE, end="", file=sys.stderr)
        return 2

    try:
        args = json.loads(args_json)
    except Exception:  # noqa: BLE001
        args = None
    if not isinstance(args, dict):
        print(canonical({"detail": "args not an object",
                         "error": "kMalformedInput"}))
        return 1
    try:
        obj, rc = call(method, args, flag)
    except Refusal as ref:
        obj, rc = ref.obj, ref.rc
    print(canonical(obj))
    return rc


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
