"""Behavioral reference fake for the C++ permissions host (permissions-host-protocol v1).

BEHAVIORAL mirror of xr-core/permissions/host/permissions_host + permissions/core,
NOT a binding (the P7/P14 precedent): dependent tracks and the no-C++ farm can
drive the permission overlay through the SAME stdio JSON protocol, and the
byte-parity test (xr-browser tools/tests/test_p15c_permissions_host.py) proves
the fake is BYTE-IDENTICAL to the compiled host on the deterministic surface.

One documented asymmetry, by design: the host OWNS the clock (it reads it once
per call when --as-of is absent); this fake reads NO clock (the fakes/_base.py
law), so a mutating call without --as-of is a usage error here (exit 2). Every
parity case passes --as-of, so the surfaces compared are identical.

Store law mirrored exactly (permissions/core/store.cc): a store is accepted only
when it parses, its bytes ARE the canonical serialization of what it parses to,
and every key set is exact; anything else is the fully-denying corrupt store,
which every mutation refuses and nothing ever rewrites.

Stdlib only. Exit 0 typed result / 1 typed error / 2 usage. Canonical JSON:
json.dumps(sort_keys=True, separators=(',',':'), ensure_ascii=True).
"""
from __future__ import annotations

import json
import os
import sys
from typing import Any

STORE_FILE = "permissions-store.json"
CLOCK_FILE = "permissions-clock.json"
MAX_REQUEST = 1 << 20
MAX_DEPTH = 64
SEVEN_DAYS = 7 * 86400000
I64_MIN, I64_MAX = -(1 << 63), (1 << 63) - 1
CAPS = ("geolocation", "camera", "microphone", "notifications")   # enum order
STATES = ("kDeny", "kAsk", "kAllow")
SCOPES = ("once", "session", "7d")
EXTRAS = ("clipboard-read-write", "sensors", "midi", "pics")
NOTE = ("ts_millis is caller-supplied; the core reads no clock; "
        "identical inputs give identical bytes")

# method -> (mutates, [(arg, type, required)]) — the host's Specs() order.
SPECS: dict[str, tuple[bool, list[tuple[str, type, bool]]]] = {
    "view": (False, []),
    "set-default": (True, [("identity", str, True), ("capability", str, True),
                           ("state", str, True)]),
    "grant": (True, [("identity", str, True), ("domain", str, True),
                     ("capability", str, True), ("scope", str, True),
                     ("session_id", str, False)]),
    "set-denied": (True, [("identity", str, True), ("denied", bool, True)]),
    "consume-once": (True, [("grant_id", str, True)]),
    "sweep": (True, [("session_id", str, True)]),
    "revoke-site": (True, [("identity", str, True), ("domain", str, True)]),
    "revoke-all": (True, [("identity", str, True)]),
}


class ParseError(Exception):
    pass


def canonical(obj: Any) -> str:
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def _int_or_double(s: str) -> Any:
    v = int(s)
    return v if I64_MIN <= v <= I64_MAX else float(s)   # overflow -> double (C++)


def _no_constants(s: str) -> Any:
    raise ParseError(f"non-JSON constant {s}")


def _depth_and_surrogates(v: Any, depth: int) -> None:
    if depth > MAX_DEPTH:
        raise ParseError("nesting depth exceeds 64")
    if isinstance(v, str):
        if any(0xD800 <= ord(c) <= 0xDFFF for c in v):
            raise ParseError("lone surrogate")
    elif isinstance(v, list):
        for x in v:
            _depth_and_surrogates(x, depth + 1)
    elif isinstance(v, dict):
        for k, x in v.items():
            _depth_and_surrogates(k, depth)
            _depth_and_surrogates(x, depth + 1)


def parse(text: str) -> Any:
    """The common/core/json_parse.cc contract: strict, keep-last duplicates."""
    try:
        v = json.loads(text, parse_int=_int_or_double, parse_constant=_no_constants)
    except (ValueError, ParseError, RecursionError) as exc:
        raise ParseError(str(exc)) from exc
    _depth_and_surrogates(v, 0)
    return v


def is_int(v: Any) -> bool:
    return isinstance(v, int) and not isinstance(v, bool)


# ---------------------------------------------------------------------------
# store (permissions/core/store.cc)
# ---------------------------------------------------------------------------

def empty_store() -> dict[str, Any]:
    return {"corrupt": False, "next_seq": 1, "identities": {}}


CORRUPT = {"corrupt": True, "next_seq": 1, "identities": {}}
GRANT_KEYS = {"id", "identity", "domain", "capability", "scope", "created_at",
              "expires_at", "session_id", "remaining_uses", "revoked", "revoked_at"}


def _grant_ok(g: Any, key: str, ids: set[str]) -> bool:
    if not isinstance(g, dict) or set(g) != GRANT_KEYS:
        return False
    strs = ("id", "identity", "domain", "capability", "scope", "session_id")
    ints = ("created_at", "expires_at", "remaining_uses", "revoked_at")
    if not all(isinstance(g[k], str) for k in strs) or not all(is_int(g[k]) for k in ints):
        return False
    if not isinstance(g["revoked"], bool) or not g["id"] or g["id"] in ids:
        return False
    ids.add(g["id"])
    if g["identity"] != key or not g["domain"] or g["capability"] not in CAPS:
        return False
    sc = g["scope"]
    if sc == "once":
        if g["expires_at"] != 0 or g["session_id"] or not 0 <= g["remaining_uses"] <= 1:
            return False
    elif sc == "session":
        if g["expires_at"] != 0 or g["remaining_uses"] != 0 or not g["session_id"]:
            return False
    elif sc == "7d":
        if g["remaining_uses"] != 0 or g["session_id"]:
            return False
        if g["expires_at"] != g["created_at"] + SEVEN_DAYS:
            return False
    else:
        return False
    return g["revoked"] or g["revoked_at"] == 0


def _identity_ok(rec: Any, key: str, ids: set[str]) -> bool:
    if not isinstance(rec, dict) or set(rec) != {"denied", "defaults", "extras", "grants"}:
        return False
    if not isinstance(rec["denied"], bool):
        return False
    d, e, gs = rec["defaults"], rec["extras"], rec["grants"]
    if not isinstance(d, dict) or not all(k in CAPS and v in STATES for k, v in d.items()):
        return False
    if not isinstance(e, dict) or not all(k in EXTRAS and v in ("kDeny", "kAsk")
                                          for k, v in e.items()):
        return False
    return isinstance(gs, list) and all(_grant_ok(g, key, ids) for g in gs)


def load_store(text: str) -> dict[str, Any]:
    try:
        root = parse(text)
    except ParseError:
        return dict(CORRUPT)
    if canonical(root) != text:
        return dict(CORRUPT)
    if not isinstance(root, dict) or set(root) != {"contract", "contract_version",
                                                   "identities", "next_seq"}:
        return dict(CORRUPT)
    if root["contract"] != "permission-store" or not is_int(root["contract_version"]) \
            or root["contract_version"] != 1:
        return dict(CORRUPT)
    if not is_int(root["next_seq"]) or root["next_seq"] < 1 \
            or not isinstance(root["identities"], dict):
        return dict(CORRUPT)
    ids: set[str] = set()
    for key, rec in sorted(root["identities"].items()):
        if not key or not _identity_ok(rec, key, ids):
            return dict(CORRUPT)
    return {"corrupt": False, "next_seq": root["next_seq"], "identities": root["identities"]}


def serialize_store(s: dict[str, Any]) -> str:
    if s["corrupt"]:
        return '{"contract":"permission-store","corrupt":true}'
    return canonical({"contract": "permission-store", "contract_version": 1,
                      "next_seq": s["next_seq"], "identities": s["identities"]})


def project_view(s: dict[str, Any]) -> dict[str, Any]:
    if s["corrupt"]:
        return {"contract_version": 1, "corrupt": True}
    denied, defaults, grants = [], [], []
    for ident in sorted(s["identities"]):
        rec = s["identities"][ident]
        if rec["denied"]:
            denied.append(ident)
        for cap in CAPS:
            if cap in rec["defaults"]:
                defaults.append({"identity": ident, "capability": cap,
                                 "state": rec["defaults"][cap]})
        for g in rec["grants"]:
            if g["revoked"] or (g["scope"] == "once" and g["remaining_uses"] <= 0):
                continue
            o = {k: g[k] for k in ("identity", "domain", "capability", "scope")}
            field = {"once": "remaining_uses", "session": "session_id"}.get(g["scope"],
                                                                         "expires_at")
            o[field] = g[field]
            grants.append(o)
    return {"contract_version": 1, "denied_identities": denied, "defaults": defaults,
            "grants": grants}


# ---------------------------------------------------------------------------
# ops (permissions/core/ops.cc) — each returns (error, store, rows)
# ---------------------------------------------------------------------------

def _label_ok(lab: str) -> bool:
    return (0 < len(lab) <= 63 and lab[0] != "-" and lab[-1] != "-"
            and all(c in "abcdefghijklmnopqrstuvwxyz0123456789-" for c in lab))


def redact_origin(d: str) -> str | None:
    if not d or len(d.encode("utf-8")) > 253:
        return None
    return d if all(_label_ok(lab) for lab in d.split(".")) else None


def row(event: str, ident: str, cap: str, origin: str, scope: str, ts: int, ttl: int,
        reason: str, layer: str) -> dict[str, Any]:
    return {"contract": "permission-audit-event", "contract_version": 1, "event": event,
            "identity": ident, "capability": cap, "origin": origin, "scope": scope,
            "ts_millis": ts, "ttl_millis": ttl, "reason": reason, "deciding_layer": layer,
            "determinism_note": NOTE}


def _rec(s: dict[str, Any], ident: str) -> dict[str, Any]:
    return s["identities"].setdefault(ident, {"denied": False, "defaults": {},
                                              "extras": {}, "grants": []})


def _after(rec: dict[str, Any] | None, cap: str) -> str:
    return "identity_overlay" if rec is not None and cap in rec["defaults"] else "global_fallback"


def _ttl(scope: str) -> int:
    return SEVEN_DAYS if scope == "7d" else 0


def apply(m: str, s0: dict[str, Any], a: dict[str, Any], now: int):
    if s0["corrupt"]:
        return "corrupt-store", s0, []
    s = json.loads(json.dumps(s0))   # a COPY: refusals never half-apply
    rows: list[dict[str, Any]] = []
    ident = a.get("identity", "")
    if m in ("set-default", "grant", "set-denied", "revoke-site", "revoke-all") and not ident:
        return "bad-identity", s0, []
    if m == "set-default":
        cap, st = a["capability"], a["state"]
        if cap not in CAPS:
            return "bad-capability", s0, []
        if st not in STATES:
            return "bad-state", s0, []
        cur = s["identities"].get(ident)
        if cur is not None and cur["defaults"].get(cap) == st:
            return "", s, []
        _rec(s, ident)["defaults"][cap] = st
        rows.append(row("grant" if st == "kAllow" else "revoke", ident, cap, "",
                        "identity_default", now, 0, "user-default", "identity_overlay"))
    elif m == "grant":
        origin = redact_origin(a["domain"])
        cap, sc, sid = a["capability"], a["scope"], a.get("session_id", "")
        if origin is None:
            return "bad-domain", s0, []
        if cap not in CAPS:
            return "bad-capability", s0, []
        if sc not in SCOPES:
            return "bad-scope", s0, []
        if (sc == "session") != bool(sid):
            return "bad-session", s0, []
        if sc == "7d" and now > I64_MAX - SEVEN_DAYS:
            return "bad-time", s0, []
        taken = {g["id"] for r in s["identities"].values() for g in r["grants"]}
        while True:
            gid = f"g{s['next_seq']}"
            s["next_seq"] += 1
            if gid not in taken:
                break
        _rec(s, ident)["grants"].append({
            "id": gid, "identity": ident, "domain": origin, "capability": cap, "scope": sc,
            "created_at": now, "expires_at": now + SEVEN_DAYS if sc == "7d" else 0,
            "session_id": sid, "remaining_uses": 1 if sc == "once" else 0,
            "revoked": False, "revoked_at": 0})
        rows.append(row("grant", ident, cap, origin, sc, now, _ttl(sc), "user-grant",
                        "identity_overlay"))
    elif m == "set-denied":
        cur = s["identities"].get(ident)
        if bool(cur is not None and cur["denied"]) == a["denied"]:
            return "", s, []
        _rec(s, ident)["denied"] = a["denied"]
        for cap in CAPS:
            rows.append(row("revoke" if a["denied"] else "grant", ident, cap, "",
                            "identity_deny_list", now, 0, "fortress-deny-list",
                            "identity_overlay"))
    elif m == "consume-once":
        for key in sorted(s["identities"]):
            rec = s["identities"][key]
            for i, g in enumerate(rec["grants"]):
                if g["revoked"] or g["id"] != a["grant_id"]:
                    continue
                if g["scope"] != "once" or g["remaining_uses"] <= 0:
                    return "unknown-grant", s0, []
                r = row("expiry", key, g["capability"], g["domain"], "once", now, 0, "used",
                        _after(rec, g["capability"]))
                del rec["grants"][i]
                return "", s, [r]
        return "unknown-grant", s0, []
    elif m == "sweep":
        for key in sorted(s["identities"]):
            rec, kept = s["identities"][key], []
            for g in rec["grants"]:
                reason = None
                if not g["revoked"]:
                    if g["scope"] == "once" and g["remaining_uses"] <= 0:
                        reason = "used"
                    elif g["scope"] == "7d" and g["expires_at"] <= now:
                        reason = "ttl"
                    elif g["scope"] == "session" and g["session_id"] != a["session_id"]:
                        reason = "session_end"
                if reason is None:
                    kept.append(g)
                    continue
                rows.append(row("expiry", key, g["capability"], g["domain"], g["scope"], now,
                                _ttl(g["scope"]), reason, _after(rec, g["capability"])))
            rec["grants"] = kept
    elif m == "revoke-site":
        origin = redact_origin(a["domain"])
        if origin is None:
            return "bad-domain", s0, []
        rec = s["identities"].get(ident)
        for g in (rec["grants"] if rec else []):
            if g["revoked"] or g["domain"] != origin:
                continue
            g["revoked"], g["revoked_at"] = True, now
            rows.append(row("revoke", ident, g["capability"], origin, g["scope"], now,
                            _ttl(g["scope"]), "revoke_site", _after(rec, g["capability"])))
    else:  # revoke-all
        rec = s["identities"].get(ident)
        if rec is not None:
            for cap in CAPS:
                if cap in rec["defaults"]:
                    rows.append(row("revoke", ident, cap, "", "identity_default", now, 0,
                                    "revoke_all", "global_fallback"))
            rec["defaults"] = {}
            for g in rec["grants"]:
                if g["revoked"]:
                    continue
                g["revoked"], g["revoked_at"] = True, now
                rows.append(row("revoke", ident, g["capability"], g["domain"], g["scope"],
                                now, _ttl(g["scope"]), "revoke_all", "global_fallback"))
    return "", s, rows


# ---------------------------------------------------------------------------
# the host shell (permissions/host/permissions_host.cc)
# ---------------------------------------------------------------------------

def emit(obj: Any) -> int:
    sys.stdout.write(canonical(obj) + "\n")
    return 0


def emit_err(code: str, detail: str) -> int:
    emit({"error": code, "detail": detail})
    return 1


def usage(why: str) -> int:
    sys.stderr.write(f"usage error: {why}\n")
    return 2


def check_args(spec: list[tuple[str, type, bool]], args: dict[str, Any],
               disposable: bool) -> str:
    names = [n for n, _, _ in spec]
    for key in sorted(args, key=lambda k: k.encode("utf-8", "surrogatepass")):
        if key == "store":
            if not disposable:
                return "arg 'store' is disposable-mode only"
            if not isinstance(args[key], str):
                return "arg 'store' must be a string"
            continue
        if key not in names:
            return f"unknown arg '{key}'"
    for name, typ, required in spec:
        if name not in args:
            if required:
                return f"missing arg '{name}'"
            continue
        v = args[name]
        ok = isinstance(v, bool) if typ is bool else isinstance(v, str)
        if not ok:
            return f"arg '{name}' must be a {'boolean' if typ is bool else 'string'}"
    return ""


def _read(path: str) -> str | None:
    try:
        with open(path, "rb") as fh:
            raw = fh.read()
    except FileNotFoundError:
        return None
    return raw.decode("utf-8", "surrogateescape")


def _atomic_write(path: str, text: str) -> None:
    tmp = path + ".tmp"
    with open(tmp, "wb") as fh:
        fh.write(text.encode("utf-8"))
        fh.flush()
        os.fsync(fh.fileno())
    os.replace(tmp, path)


def run(method: str, args: dict[str, Any], store_dir: str, as_of: int | None) -> int:
    if method not in SPECS:
        return emit_err("kUnknownMethod", f"unknown method '{method}'")
    mutates, spec = SPECS[method]
    disposable = not store_dir
    bad = check_args(spec, args, disposable)
    if bad:
        return emit_err("kMalformedInput", bad)
    last_seen = 0
    if disposable:
        text = args.get("store")
    else:
        text = _read(os.path.join(store_dir, STORE_FILE))
        clock = _read(os.path.join(store_dir, CLOCK_FILE))
        if clock is not None:
            try:
                c = parse(clock)
            except ParseError:
                c = None
            ls = c.get("last_seen_ms") if isinstance(c, dict) else None
            if not is_int(ls) or ls < 0 or len(c) != 1 or canonical(c) != clock:
                return emit_err("kFailClosed", "corrupt clock file: refused, left untouched")
            last_seen = ls
    store = load_store(text) if text is not None else empty_store()
    if not mutates:
        return emit({"ok": {"view": project_view(store)}})
    if as_of is None:
        return usage("the fake reads no clock: --as-of is required for mutating methods")
    now = max(last_seen, as_of)
    error, out, rows = apply(method, store, args, now)
    if error:
        return emit_err("kRefused", error)
    new_text = serialize_store(out)
    if not disposable:
        try:
            if text is None or new_text != text:
                _atomic_write(os.path.join(store_dir, STORE_FILE), new_text)
        except OSError:
            return emit_err("kIoError", "atomic store write failed")
        try:
            if now > last_seen:
                _atomic_write(os.path.join(store_dir, CLOCK_FILE),
                              canonical({"last_seen_ms": now}))
        except OSError:
            return emit_err("kIoError", "atomic clock write failed")
    return emit({"ok": {"now_ms": now, "rows": rows, "store": new_text}})


def _size(s: str) -> int:
    return len(s.encode("utf-8", "surrogateescape"))


def main(argv: list[str]) -> int:
    store_dir, as_of, pos = "", None, []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--store-dir", "--as-of"):
            if i + 1 >= len(argv):
                return usage("option needs a value")
            v = argv[i + 1]
            i += 1
            if a == "--store-dir":
                if not v:
                    return usage("--store-dir must be non-empty")
                store_dir = v
            else:
                if not v or len(v) > 18 or not all(c in "0123456789" for c in v):
                    return usage("--as-of must be a non-negative integer (ms)")
                as_of = int(v)
        elif a.startswith("--"):
            return usage("unknown option")
        else:
            pos.append(a)
        i += 1
    if len(pos) > 2:
        return usage("too many arguments")
    args: Any = {}
    if not pos or pos[0].startswith("{"):
        if len(pos) == 2:
            return usage("a JSON request takes no second argument")
        if pos:
            req = pos[0]
        else:
            raw = sys.stdin.buffer.read(MAX_REQUEST + 8192)
            req = raw.decode("utf-8", "surrogateescape")
        if _size(req) > MAX_REQUEST:
            return emit_err("kMalformedInput", "request exceeds 1 MiB")
        try:
            doc = parse(req)
        except ParseError:
            doc = None
        if not isinstance(doc, dict):
            return emit_err("kMalformedInput", "request must be a JSON object")
        if not isinstance(doc.get("method"), str):
            return emit_err("kMalformedInput", "request missing 'method'")
        method = doc["method"]
        if "args" in doc:
            args = doc["args"]
    else:
        method = pos[0]
        if len(pos) == 2:
            if _size(pos[1]) > MAX_REQUEST:
                return emit_err("kMalformedInput", "request exceeds 1 MiB")
            try:
                args = parse(pos[1])
            except ParseError:
                return emit_err("kMalformedInput", "args must be a JSON object")
    if not isinstance(args, dict):
        return emit_err("kMalformedInput", "args must be a JSON object")
    return run(method, args, store_dir, as_of)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
