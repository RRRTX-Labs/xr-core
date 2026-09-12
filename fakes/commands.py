"""Behavioral reference fake for the C++ command host (command-host-protocol v1).

This is a BEHAVIORAL mirror of xr-core/commands/host/commands_host +
commands/core, NOT a binding. It exists so dependent tracks (P8/P13) can
develop against the command host over the SAME stdio JSON protocol without a
C++ toolchain, and so `xrctl commands --backend fake|cpp` drives either
interchangeably. The parity harness (xr-browser
tools/tests/test_p7_commands_tools.py) asserts this fake is BYTE-IDENTICAL to
the compiled C++ host over the full method surface (P6 pattern).

Contract laws honored here (mirror the C++):
  * dispatch is the security edge: source-tag allowlist {ui-chrome,palette,
    menu,shortcut,test}; page-originated => reject + ledger; unknown id never
    reaches a handler; destructive => confirmation gate.
  * availability predicates are pure reads of the PINNED snapshot (denial-
    default for unknown predicates); dials write the P6 trust-bindings doc.
  * deterministic: no clock (ledger seq is a counter), no RNG, no I/O beyond
    the store dir. Canonical JSON is sorted-keys, compact, raw UTF-8.

Stdlib only. Python 3.12+. Exit: 0 typed result, 1 error, 2 usage.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Any

CONTRACT_VERSION = 1
FLAG_NAME = "xr_command_registry_v1"

# --- constants (mirrors commands/core; tests assert the frozen enums) -------
ATTENTION_TIERS = ["tier0", "tier1", "tier2"]
DANGER_CLASSES = ["safe", "caution", "destructive"]
REQUIRED_FIELDS = ["id", "title", "attention_tier", "danger_class", "surface",
                   "handler"]
ALLOWED_SOURCES = ["ui-chrome", "palette", "menu", "shortcut", "test"]
MAX_TIER1 = 9
BROWSER_RESERVED = ["F11", "CTRL+SHIFT+I", "F12"]
SYSTEM_RESERVED = ["ALT+F4", "CTRL+ESC"]
REGISTERED_PREDICATES = ["always", "policy.trust-dial-writable",
                         "tor.engine-ready", "identity.active",
                         "build.channel-dev"]


def canonical(obj: Any) -> str:
    """Byte-stable canonical JSON: sorted keys, compact, ASCII-escaped.

    ensure_ascii=True (the default) matches the C++ JsonValue::Canonical()
    byte-for-byte: both emit non-ASCII as \\uXXXX escapes, so the two
    backends are byte-identical over the whole protocol surface."""
    return json.dumps(obj, sort_keys=True, separators=(",", ":"))


def ok(v: Any) -> str:
    return canonical({"ok": v})


def err(code: str, detail: str = "") -> str:
    o: dict[str, Any] = {"error": code}
    if detail:
        o["detail"] = detail
    return canonical(o)


# --- ASCII-faithful case (mirror std::tolower/toupper in the "C" locale) ----
def lower_ascii(s: str) -> str:
    return "".join(chr(ord(c) + 32) if 65 <= ord(c) <= 90 else c for c in s)


def upper_ascii(s: str) -> str:
    return "".join(chr(ord(c) - 32) if 97 <= ord(c) <= 122 else c for c in s)


def scope_from_string(s: str) -> str:
    if s in ("window", "identity", "site"):
        return s
    return "global"  # unknown => conservative default (not a reject here)


def valid_scope(s: str) -> bool:
    return s in ("global", "window", "identity", "site")


# --- matcher (exact port of commands/core/matcher.cc) ------------------------
K_CONSECUTIVE = 5
K_BOUNDARY = 10
K_KIND_SCALE = 1000000


def _is_separator(c: str) -> bool:
    return c in (" ", "-", "_", "/", ".", "\t")


def _subseq_positions(q: str, t: str) -> list[int]:
    pos: list[int] = []
    ti = 0
    for qc in q:
        if ti >= len(t):
            return []
        found = False
        while ti < len(t):
            if t[ti] == qc:
                pos.append(ti)
                ti += 1
                found = True
                break
            ti += 1
        if not found:
            return []
    return pos


def _fzf_score(q: str, t: str, pos: list[int]) -> int:
    s = 0
    for i, p in enumerate(pos):
        s += 1
        if i > 0 and pos[i - 1] == p - 1:
            s += K_CONSECUTIVE
        if p == 0:
            s += K_BOUNDARY
        else:
            prev = t[p - 1]
            if _is_separator(prev):
                s += K_BOUNDARY
            elif "a" <= prev <= "z" and "A" <= t[p] <= "Z":
                s += K_BOUNDARY
    s -= (len(t) - len(q)) // 2
    return s if s >= 1 else 1


def match_query(query: str, commands: list[dict[str, Any]]) -> list[dict[str, Any]]:
    q = lower_ascii(query)
    out: list[dict[str, Any]] = []
    for c in commands:
        m: dict[str, Any] = {"id": c["id"], "order": c["order"], "title": c["title"]}
        if q == "":
            m["kind"] = 1
            m["score"] = K_KIND_SCALE
            out.append(m)
            continue
        t = lower_ascii(c["title"])
        pos = _subseq_positions(q, t)
        if pos:
            m["kind"] = 2 if q in t else 1
            m["score"] = K_KIND_SCALE * m["kind"] + _fzf_score(q, t, pos)
        else:
            k = lower_ascii(c["id"])
            for kw in c["keywords"]:
                k += " " + lower_ascii(kw)
            posk = _subseq_positions(q, k)
            if not posk:
                continue
            m["kind"] = 0
            m["score"] = K_KIND_SCALE * 0 + _fzf_score(q, k, posk)
        out.append(m)
    out.sort(key=lambda m: (-m["score"], m["order"]))
    return out


# --- availability (exact port of commands/core/availability.cc) -------------
def evaluate(predicate_id: str, snap: dict[str, Any]) -> tuple[bool, str]:
    if predicate_id == "always":
        return True, ""
    if predicate_id == "policy.trust-dial-writable":
        if snap.get("dial_writable") is True:
            return True, ""
        return False, "trust dial is not writable for this scope (resolver snapshot)"
    if predicate_id == "tor.engine-ready":
        caps = snap.get("capabilities")
        if isinstance(caps, list):
            for c in caps:
                if isinstance(c, str) and c == "tor.engine":
                    return True, ""
        return False, "Tor engine not wired (P31) \u2014 placeholder, disabled"
    if predicate_id == "identity.active":
        ai = snap.get("active_identity")
        if isinstance(ai, str) and ai != "":
            return True, ""
        return False, "no active identity"
    if predicate_id == "build.channel-dev":
        caps = snap.get("capabilities")
        if isinstance(caps, list):
            for c in caps:
                if isinstance(c, str) and c == "build.channel-dev":
                    return True, ""
        return False, "build channel is not dev (capabilities snapshot)"
    return False, "unknown predicate '" + predicate_id + "' (deny-default)"


def known_predicate(predicate_id: str) -> bool:
    return predicate_id in REGISTERED_PREDICATES


# --- descriptor validation (exact port of commands/core/descriptor.cc) ------
def validate_descriptor(obj: Any) -> tuple[dict[str, str] | None, str]:
    if not isinstance(obj, dict):
        return None, "descriptor must be a JSON object (frozen schema: type=object)"
    for k in obj:
        if k not in REQUIRED_FIELDS:
            return None, ("unknown field '" + k +
                          "' rejected: frozen command-descriptor-v1 has "
                          "additionalProperties:false (new fields register via "
                          "registry-post-freeze.md, not here)")
    for field in REQUIRED_FIELDS:
        if field not in obj:
            return None, "missing required field '" + field + "'"
        v = obj[field]
        if not isinstance(v, str) or v == "":
            return None, "field '" + field + "' must be a non-empty string"
    f = {k: obj[k] for k in REQUIRED_FIELDS}
    if f["attention_tier"] not in ATTENTION_TIERS:
        return None, ("attention_tier '" + f["attention_tier"] +
                      "' not in frozen enum {tier0,tier1,tier2}")
    if f["danger_class"] not in DANGER_CLASSES:
        return None, ("danger_class '" + f["danger_class"] +
                      "' not in frozen enum {safe,caution,destructive}")
    return f, ""


# --- registry (exact port of commands/core/registry.cc) ---------------------
class Registry:
    def __init__(self) -> None:
        self.by_id: dict[str, dict[str, Any]] = {}
        self.order: list[str] = []
        self.tier1 = 0

    def register(self, cmd: dict[str, Any]) -> tuple[bool, int, str]:
        for f in ("id", "title", "attention_tier", "danger_class", "surface",
                  "handler"):
            if cmd.get(f, "") == "":
                return False, 0, ("rejected: a frozen descriptor field is empty "
                                  "(command-descriptor-v1: all six fields required)")
        if cmd["danger_class"] not in DANGER_CLASSES:
            return False, 0, ("rejected: danger_class '" + cmd["danger_class"] +
                              "' invalid (must be safe|caution|destructive)")
        if cmd["attention_tier"] not in ATTENTION_TIERS:
            return False, 0, ("rejected: attention_tier '" + cmd["attention_tier"] +
                              "' invalid (must be tier0|tier1|tier2)")
        if cmd["id"] in self.by_id:
            return False, 0, ("rejected: duplicate id '" + cmd["id"] +
                              "' (registry ids are unique; re-register is an "
                              "update, not a dup)")
        if cmd["attention_tier"] == "tier1" and self.tier1 >= MAX_TIER1:
            return False, 0, ("rejected: id '" + cmd["id"] +
                              "' would be the " + str(self.tier1 + 1) +
                              "th tier1 control \u2014 'Tier-1 always-visible <=9 "
                              "controls' (Plan \u00a71.10 Chrome tiers / Attention "
                              "Budget). Demote it to tier2 (Palette/Panel one "
                              "interaction) to register.")
        c = dict(cmd)
        c["order"] = len(self.order)
        self.by_id[c["id"]] = c
        self.order.append(c["id"])
        if c["attention_tier"] == "tier1":
            self.tier1 += 1
        return True, c["order"], ""

    def known_id(self, id: str) -> bool:
        return id in self.by_id

    def find(self, id: str) -> dict[str, Any] | None:
        return self.by_id.get(id)

    def list(self) -> list[dict[str, Any]]:
        return [self.by_id[i] for i in self.order]

    def tier1_count(self) -> int:
        return self.tier1

    def to_json(self) -> dict[str, Any]:
        cmds = []
        for c in self.list():
            desc = {"id": c["id"], "title": c["title"],
                    "attention_tier": c["attention_tier"],
                    "danger_class": c["danger_class"], "surface": c["surface"],
                    "handler": c["handler"]}
            meta = {"keywords": list(c["keywords"]), "group": c["group"],
                    "scope": c["scope"], "predicate_id": c["predicate_id"]}
            cmds.append({"descriptor": desc, "registry": meta})
        return {"schema": "xr-commands-registry", "schema_version": 1,
                "commands": cmds}

    def from_json(self, doc: Any) -> str:
        if not isinstance(doc, dict):
            return "registry doc must be an object"
        schema = doc.get("schema") if isinstance(doc.get("schema"), str) else ""
        if schema != "xr-commands-registry":
            return "bad schema id '" + schema + "' (want xr-commands-registry)"
        sv = doc.get("schema_version")
        if not isinstance(sv, int) or sv != 1:
            return "unsupported schema_version (want 1)"
        arr = doc.get("commands")
        if not isinstance(arr, list):
            return "missing commands[]"
        for entry in arr:
            if not isinstance(entry, dict):
                return "entry missing descriptor/registry"
            desc = entry.get("descriptor")
            meta = entry.get("registry")
            if not isinstance(desc, dict) or not isinstance(meta, dict):
                return "entry missing descriptor/registry"
            f, derr = validate_descriptor(desc)
            if f is None:
                return "bad descriptor: " + derr
            c = dict(f)
            kw = meta.get("keywords")
            c["keywords"] = [x for x in kw if isinstance(x, str)] if isinstance(kw, list) else []
            g = meta.get("group")
            c["group"] = g if isinstance(g, str) else ""
            sc = meta.get("scope")
            if sc is not None:
                if not valid_scope(sc):
                    return "bad scope '" + sc + "'"
                c["scope"] = scope_from_string(sc)
            else:
                c["scope"] = "global"
            pid = meta.get("predicate_id")
            c["predicate_id"] = pid if (isinstance(pid, str) and pid != "") else "always"
            rok, _order, rerr = self.register(c)
            if not rok:
                return "re-register failed: " + rerr
        return ""


# --- shortcuts (exact port of commands/core/shortcuts.cc) -------------------
class ShortcutStore:
    def __init__(self, dir: str) -> None:
        self.dir = dir
        self.bindings: list[dict[str, str]] = []  # {"command_id","accelerator"}

    @staticmethod
    def normalize(s: str) -> str:
        out = []
        for c in s:
            if c in (" ", "\t"):
                continue
            out.append(upper_ascii(c))
        return "".join(out)

    def path(self) -> str:
        return self.dir + "/shortcuts.json"

    def load(self) -> tuple[bool, bool, str]:
        # returns (ok, preserved, error)
        if self.dir == "":
            return True, False, ""
        p = self.path()
        if not os.path.exists(p):
            return True, False, ""
        with open(p, "rb") as fh:
            raw = fh.read()
        if len(raw) == 0:
            return False, True, "shortcuts.json is empty (truncated write?) \u2014 preserved as-is"
        try:
            d = json.loads(raw.decode("utf-8"))
        except Exception as e:  # noqa: BLE001 — deny-preserve on any parse failure
            return False, True, "shortcuts.json is not strict JSON: " + str(e) + " \u2014 preserved"
        if not isinstance(d, dict):
            return False, True, "shortcuts.json is not strict JSON: not an object \u2014 preserved"
        if d.get("schema") != "xr-shortcuts" or d.get("schema_version") != 1:
            return False, True, "shortcuts.json has an unrecognized schema \u2014 preserved"
        arr = d.get("bindings")
        if not isinstance(arr, list):
            return False, True, "shortcuts.json missing bindings[] \u2014 preserved"
        for b in arr:
            if not isinstance(b, dict):
                continue
            cid = b.get("command_id")
            acc = b.get("accelerator")
            if not isinstance(cid, str) or not isinstance(acc, str):
                continue
            self.bindings.append({"command_id": cid,
                                  "accelerator": self.normalize(acc)})
        return True, False, ""

    def bind(self, command_id: str, accelerator: str) -> dict[str, Any]:
        r: dict[str, Any] = {"ok": False, "conflict": "none",
                             "conflicting_command": "", "error": ""}
        acc = self.normalize(accelerator)
        if acc == "":
            r["error"] = "empty accelerator"
            return r
        if acc in SYSTEM_RESERVED:
            r["conflict"] = "system-reserved"
            r["error"] = "accelerator " + acc + " is system-reserved (deny-by-default)"
            return r
        if acc in BROWSER_RESERVED:
            r["conflict"] = "browser-reserved"
            r["error"] = "accelerator " + acc + " is reserved by the browser (deny-by-default)"
            return r
        for b in self.bindings:
            if b["accelerator"] == acc and b["command_id"] != command_id:
                r["conflict"] = "duplicate"
                r["conflicting_command"] = b["command_id"]
                r["error"] = "accelerator " + acc + " is already bound to " + b["command_id"]
                return r
        self.bindings = [b for b in self.bindings if b["command_id"] != command_id]
        self.bindings.append({"command_id": command_id, "accelerator": acc})
        r["ok"] = True
        r["conflict"] = "none"
        return r

    def unbind(self, command_id: str) -> bool:
        before = len(self.bindings)
        self.bindings = [b for b in self.bindings if b["command_id"] != command_id]
        return len(self.bindings) != before

    def save(self) -> bool:
        if self.dir == "":
            return True
        target = self.path()
        tmp = target + ".tmp"
        doc = {"schema": "xr-shortcuts", "schema_version": 1,
               "bindings": [{"accelerator": b["accelerator"],
                             "command_id": b["command_id"]} for b in self.bindings]}
        with open(tmp, "wb") as fh:
            fh.write((canonical(doc) + "\n").encode("utf-8"))
            fh.flush()
            os.fsync(fh.fileno())
        os.replace(tmp, target)
        return True


# --- policy state (exact port of commands/core/policy_state.cc) -------------
class PolicyState:
    def __init__(self, dir: str) -> None:
        self.dir = dir
        self.mem_bindings: list[dict[str, Any]] = []

    def _trust_path(self) -> str:
        return self.dir + "/trust_bindings.json"

    def _snapshot_path(self) -> str:
        return self.dir + "/snapshot.json"

    @staticmethod
    def _is_tier(t: str) -> bool:
        return t in ("kStandard", "kShield", "kFortress")

    @staticmethod
    def default_snapshot() -> dict[str, Any]:
        return {"version": 1, "dial_writable": True, "capabilities": [],
                "active_identity": "xr:main-001"}

    def snapshot(self) -> dict[str, Any]:
        p = self._snapshot_path()
        if os.path.exists(p) and os.path.getsize(p) > 0:
            try:
                d = json.loads(open(p, "r", encoding="utf-8").read())
                if isinstance(d, dict):
                    return d
            except Exception:  # noqa: BLE001
                pass
        return self.default_snapshot()

    def _load_bindings(self) -> list[dict[str, Any]]:
        if self.dir == "":
            return self.mem_bindings
        p = self._trust_path()
        if not os.path.exists(p) or os.path.getsize(p) == 0:
            return []
        try:
            v = json.loads(open(p, "r", encoding="utf-8").read())
        except Exception:  # noqa: BLE001
            return []
        if not isinstance(v, dict):
            return []
        d = v.get("data")
        if not isinstance(d, dict):
            return []
        bs = d.get("bindings")
        return bs if isinstance(bs, list) else []

    def _save_bindings(self, bindings: list[dict[str, Any]]) -> tuple[bool, str]:
        if self.dir == "":
            self.mem_bindings = bindings
            return True, ""
        doc = {"schema": "xr-trust-bindings", "schema_version": 1, "created_at": 0,
               "data": {"bindings": bindings}}
        p = self._trust_path()
        tmp = p + ".tmp"
        with open(tmp, "wb") as fh:
            fh.write((canonical(doc) + "\n").encode("utf-8"))
            fh.flush()
            os.fsync(fh.fileno())
        os.replace(tmp, p)
        return True, ""

    def _current_trust(self, domain: str, identity: str) -> str:
        for e in self._load_bindings():
            if not isinstance(e, dict):
                continue
            d = e.get("domain")
            i = e.get("identity")
            t = e.get("trust")
            if isinstance(d, str) and d == domain and \
               (i is None or (isinstance(i, str) and i == identity)) and \
               isinstance(t, str):
                return t
        return ""

    def set_trust(self, domain: str, identity: str, trust: str) -> tuple[str, str]:
        if not self._is_tier(trust):
            return "", "illegal trust tier '" + trust + "'"
        prev = self._current_trust(domain, identity)
        out: list[dict[str, Any]] = []
        for e in self._load_bindings():
            if not isinstance(e, dict):
                out.append(e)
                continue
            d = e.get("domain")
            i = e.get("identity")
            match = isinstance(d, str) and d == domain and \
                (i is None or (isinstance(i, str) and i == identity))
            if not match:
                out.append(e)
        out.append({"domain": domain, "identity": identity, "trust": trust})
        saved, serr = self._save_bindings(out)
        if not saved:
            return "", serr
        return prev, ""

    def remove_trust(self, domain: str, identity: str) -> tuple[str, str]:
        prev = self._current_trust(domain, identity)
        out: list[dict[str, Any]] = []
        for e in self._load_bindings():
            if not isinstance(e, dict):
                out.append(e)
                continue
            d = e.get("domain")
            i = e.get("identity")
            match = isinstance(d, str) and d == domain and \
                (i is None or (isinstance(i, str) and i == identity))
            if not match:
                out.append(e)
        saved, serr = self._save_bindings(out)
        if not saved:
            return "", serr
        return prev, ""


# --- dispatch (exact port of commands/core/dispatch.cc) ---------------------
class Dispatcher:
    def __init__(self, registry: Registry) -> None:
        self.registry = registry
        self.seq = 0
        self.ledger: list[str] = []

    def _record(self, event: str, reason: str, id: str, source: str) -> None:
        row = {"event": event, "id": id, "reason": reason, "seq": self.seq,
               "source": source}
        self.seq += 1
        self.ledger.append(canonical(row))

    def invoke(self, id: str, source: str, confirmed: bool) -> dict[str, Any]:
        out: dict[str, Any] = {"id": id, "source": source}
        if source not in ALLOWED_SOURCES:
            out["status"] = "rejected"
            out["reason"] = ("page-originated invocations are rejected "
                             "(cross-process origin check; the palette never "
                             "executes page-originated commands)"
                             if source == "page"
                             else "source tag '" + source + "' is not whitelisted")
            self._record("invoke-reject", out["reason"], id, source)
            out["ledger_rows"] = [self.ledger[-1]]
            return out
        cmd = self.registry.find(id)
        if cmd is None:
            out["status"] = "rejected"
            out["reason"] = "unknown command id '" + id + "' (registry id whitelist)"
            self._record("invoke-reject", out["reason"], id, source)
            out["ledger_rows"] = [self.ledger[-1]]
            return out
        out["handler"] = cmd["handler"]
        out["danger_class"] = cmd["danger_class"]
        if cmd["danger_class"] == "destructive" and not confirmed:
            out["status"] = "confirmation-required"
            out["reason"] = ("destructive command '" + id +
                             "' requires explicit confirmation (L7: friction "
                             "is a feature)")
            self._record("invoke-confirm-required", "destructive", id, source)
            out["ledger_rows"] = [self.ledger[-1]]
            return out
        out["authorized"] = True
        out["status"] = "authorized"
        self._record("invoke-allow", "ok", id, source)
        out["ledger_rows"] = [self.ledger[-1]]
        return out


# --- host protocol (exact port of commands/host/protocol.cc) ----------------
class Context:
    def __init__(self, store_dir: str, roster: str) -> None:
        self.store_dir = store_dir
        self.roster = roster
        self.flag_registry = "on"
        self.registry_path = (store_dir + "/registry.json") if store_dir else ""
        self.registry = Registry()
        self.shortcuts = ShortcutStore(store_dir)
        self.policy = PolicyState(store_dir)

    @property
    def flag_on(self) -> bool:
        return self.flag_registry != "off"


def load_context(ctx: Context) -> str:
    reg_path = ctx.registry_path
    if reg_path and os.path.exists(reg_path) and os.path.getsize(reg_path) > 0:
        try:
            doc = json.loads(open(reg_path, "r", encoding="utf-8").read())
        except Exception as e:  # noqa: BLE001
            return "registry.json not valid: " + str(e)
        e = ctx.registry.from_json(doc)
        if e:
            return e
    else:
        if not os.path.exists(ctx.roster):
            return "no registry.json and cannot read roster " + ctx.roster
        try:
            doc = json.loads(open(ctx.roster, "r", encoding="utf-8").read())
        except Exception as e:  # noqa: BLE001
            return "roster not valid: " + str(e)
        e = ctx.registry.from_json(doc)
        if e:
            return e
        if ctx.store_dir != "":
            with open(reg_path, "wb") as fh:
                fh.write((canonical(ctx.registry.to_json()) + "\n").encode("utf-8"))
    ok, _pres, serr = ctx.shortcuts.load()
    if not ok:
        return serr
    return ""


def _arg_str(args: dict[str, Any], k: str, dflt: str) -> str:
    v = args.get(k)
    return v if isinstance(v, str) else dflt


def _arg_bool(args: dict[str, Any], k: str) -> bool:
    v = args.get(k)
    return v is True


def _command_entry(c: dict[str, Any]) -> dict[str, Any]:
    desc = {"attention_tier": c["attention_tier"], "danger_class": c["danger_class"],
            "handler": c["handler"], "id": c["id"], "surface": c["surface"],
            "title": c["title"]}
    meta = {"group": c["group"], "keywords": list(c["keywords"]),
            "predicate_id": c["predicate_id"], "scope": c["scope"]}
    return {"descriptor": desc, "registry": meta}


def m_flag_status(args: dict[str, Any], ctx: Context) -> str:
    return ok({FLAG_NAME: ctx.flag_registry})


def m_list(args: dict[str, Any], ctx: Context) -> str:
    group = _arg_str(args, "group", "")
    cmds = []
    for c in ctx.registry.list():
        if group != "" and c["group"] != group:
            continue
        if ctx.flag_on:
            cmds.append(_command_entry(c))
    return ok({"commands": cmds, "count": len(cmds)})


def m_query(args: dict[str, Any], ctx: Context) -> str:
    results = []
    if ctx.flag_on:
        q = _arg_str(args, "query", "")
        ranked = match_query(q, ctx.registry.list())
        snap = ctx.policy.snapshot()
        for m in ranked:
            c = ctx.registry.find(m["id"])
            available, reason = evaluate(c["predicate_id"], snap)
            results.append({"available": available, "id": m["id"],
                            "kind": m["kind"], "reason": reason,
                            "score": m["score"], "title": m["title"]})
    return ok({"count": len(results), "results": results})


def m_invoke(args: dict[str, Any], ctx: Context) -> str:
    id = _arg_str(args, "id", "")
    source = _arg_str(args, "source", "")
    confirmed = _arg_bool(args, "confirmed")
    site = _arg_str(args, "site", "*")
    if not ctx.flag_on:
        return ok({"id": id, "reason":
                   "feature flag " + FLAG_NAME + " is off (stock chrome)",
                   "source": source, "status": "rejected"})
    d = Dispatcher(ctx.registry)
    out = d.invoke(id, source, confirmed)
    effect: dict[str, Any] = {}
    if out.get("authorized"):
        handler = out["handler"]
        terr = ""
        if handler in ("action.dial.standard", "action.dial.shield",
                       "action.dial.fortress"):
            tier = {"action.dial.standard": "kStandard",
                    "action.dial.shield": "kShield",
                    "action.dial.fortress": "kFortress"}[handler]
            prev, terr = ctx.policy.set_trust(site, "", tier)
            effect["prev"] = prev
            effect["site"] = site
            effect["trust"] = tier
        elif handler == "action.dial.reset":
            prev, terr = ctx.policy.remove_trust(site, "")
            effect["prev"] = prev
            effect["removed"] = True
            effect["site"] = site
        if terr != "":
            return ok({"id": id, "reason": "policy state write failed: " + terr,
                       "source": source, "status": "rejected"})
    o: dict[str, Any] = {"id": out["id"], "status": out["status"],
                         "source": out["source"]}
    if out.get("handler"):
        o["handler"] = out["handler"]
    if out.get("danger_class"):
        o["danger_class"] = out["danger_class"]
    if out.get("reason"):
        o["reason"] = out["reason"]
    o["effect"] = effect
    ledger = out.get("ledger_rows", [])
    o["ledger"] = list(ledger)
    return ok(o)


def m_bindings_set(args: dict[str, Any], ctx: Context) -> str:
    cid = _arg_str(args, "command_id", "")
    acc = _arg_str(args, "accelerator", "")
    if not ctx.registry.known_id(cid):
        return ok({"bound": False, "command_id": cid, "conflict": "none",
                   "error": "unknown command id '" + cid + "'"})
    r = ctx.shortcuts.bind(cid, acc)
    persisted = False
    if r["ok"]:
        persisted = ctx.shortcuts.save()
    o: dict[str, Any] = {"bound": r["ok"], "command_id": cid,
                         "conflict": r["conflict"], "persisted": persisted}
    if r["conflict"] == "duplicate":
        o["conflicting_command"] = r["conflicting_command"]
    if r["error"] != "":
        o["error"] = r["error"]
    return ok(o)


def m_bindings_list(args: dict[str, Any], ctx: Context) -> str:
    binds = sorted(ctx.shortcuts.bindings,
                   key=lambda b: (b["accelerator"], b["command_id"]))
    arr = [{"accelerator": b["accelerator"], "command_id": b["command_id"]}
           for b in binds]
    return ok({"bindings": arr, "count": len(arr)})


def m_bindings_clear(args: dict[str, Any], ctx: Context) -> str:
    cid = _arg_str(args, "command_id", "")
    persisted = False
    if cid != "":
        removed = ctx.shortcuts.unbind(cid)
        if removed:
            persisted = ctx.shortcuts.save()
        return ok({"cleared": cid if removed else None, "persisted": persisted})
    for i in [b["command_id"] for b in ctx.shortcuts.bindings]:
        ctx.shortcuts.unbind(i)
    persisted = ctx.shortcuts.save()
    return ok({"cleared_all": True, "persisted": persisted})


def m_menu_model(args: dict[str, Any], ctx: Context) -> str:
    tier1_items = []
    menu_items = []
    if ctx.flag_on:
        snap = ctx.policy.snapshot()
        for c in ctx.registry.list():
            available, reason = evaluate(c["predicate_id"], snap)
            item = {"available": available, "danger_class": c["danger_class"],
                    "group": c["group"], "id": c["id"], "reason": reason,
                    "tier": c["attention_tier"], "title": c["title"]}
            if c["attention_tier"] == "tier1":
                tier1_items.append(item)
            else:
                menu_items.append(item)
    tools = {"items": menu_items, "kind": "tools"}
    tier1 = {"count": len(tier1_items), "items": tier1_items}
    return ok({"flag": ctx.flag_registry, "menus": [tools], "tier1": tier1})


def m_register(args: dict[str, Any], ctx: Context) -> str:
    if not ctx.flag_on:
        return ok({"status": "rejected", "reason":
                   "feature flag " + FLAG_NAME + " is off (stock chrome)"})
    desc = args.get("descriptor")
    meta = args.get("registry")
    if not isinstance(desc, dict) or not isinstance(meta, dict):
        return err("kMalformedInput", "register needs descriptor + registry")
    f, verr = validate_descriptor(desc)
    if f is None:
        return err("kMalformedInput", verr)
    c = dict(f)
    kw = meta.get("keywords")
    c["keywords"] = [x for x in kw if isinstance(x, str)] if isinstance(kw, list) else []
    g = meta.get("group")
    c["group"] = g if isinstance(g, str) else ""
    sc = meta.get("scope")
    c["scope"] = scope_from_string(sc) if isinstance(sc, str) else "global"
    pid = meta.get("predicate_id")
    c["predicate_id"] = pid if (isinstance(pid, str) and pid != "") else "always"
    if not known_predicate(c["predicate_id"]):
        return err("kMalformedInput",
                   "unknown availability predicate '" + c["predicate_id"] + "'")
    # In-memory register on a copy; persist only on success.
    import copy as _copy
    scratch = _copy.deepcopy(ctx.registry)
    rok, order, rerr = scratch.register(c)
    if not rok:
        return err("kRejected", rerr)
    ctx.registry = scratch
    if ctx.store_dir != "":
        with open(ctx.registry_path, "wb") as fh:
            fh.write((canonical(ctx.registry.to_json()) + "\n").encode("utf-8"))
    return ok({"id": c["id"], "order": order, "status": "registered",
               "tier1_count": ctx.registry.tier1_count()})


def handle_method(method: str, args: dict[str, Any], ctx: Context) -> str:
    if method == "flag-status":
        return m_flag_status(args, ctx)
    if method == "list":
        return m_list(args, ctx)
    if method == "query":
        return m_query(args, ctx)
    if method == "invoke":
        return m_invoke(args, ctx)
    if method == "bindings-set":
        return m_bindings_set(args, ctx)
    if method == "bindings-list":
        return m_bindings_list(args, ctx)
    if method == "bindings-clear":
        return m_bindings_clear(args, ctx)
    if method == "menu-model":
        return m_menu_model(args, ctx)
    if method == "register":
        return m_register(args, ctx)
    return err("kUnknownMethod", "unknown method '" + method + "'")


_METHODS = {"flag-status", "list", "query", "invoke", "bindings-set",
            "bindings-list", "bindings-clear", "menu-model", "register"}


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="commands_host-fake", add_help=False)
    store_dir = ""
    roster = os.environ.get("XR_DEFAULT_ROSTER",
                            "commands/core/roster_v1.json")
    flags: dict[str, str] = {}
    positional: list[str] = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--store-dir" and i + 1 < len(argv):
            store_dir = argv[i + 1]
            i += 2
        elif a == "--roster" and i + 1 < len(argv):
            roster = argv[i + 1]
            i += 2
        elif a == "--flag" and i + 1 < len(argv):
            kv = argv[i + 1]
            if "=" not in kv:
                ap.print_usage(sys.stderr)
                return 2
            k, v = kv.split("=", 1)
            flags[k] = v
            i += 2
        elif a in ("-h", "--help"):
            print("usage: commands_host <method> ['<json-args>'] [options]")
            return 0
        elif a.startswith("--"):
            ap.print_usage(sys.stderr)
            return 2
        else:
            positional.append(a)
            i += 1
    fr = flags.get(FLAG_NAME, "on")

    method = ""
    args_json = "{}"
    if not positional:
        args_json = sys.stdin.read()
        try:
            p = json.loads(args_json)
        except Exception as e:  # noqa: BLE001
            print(err("kMalformedInput", str(e)))
            return 1
        method = p.get("method", "") if isinstance(p, dict) else ""
        a = p.get("args") if isinstance(p, dict) else None
        args_json = canonical(a) if a is not None else "{}"
    elif positional[0] in _METHODS:
        method = positional[0]
        if len(positional) >= 2:
            args_json = positional[1]
    else:
        args_json = positional[0]
        try:
            p = json.loads(args_json)
        except Exception as e:  # noqa: BLE001
            print(err("kMalformedInput", str(e)))
            return 1
        method = p.get("method", "") if isinstance(p, dict) else ""
        a = p.get("args") if isinstance(p, dict) else None
        args_json = canonical(a) if a is not None else "{}"
    if method == "":
        ap.print_usage(sys.stderr)
        return 2
    try:
        ap = json.loads(args_json)
    except Exception:  # noqa: BLE001
        print(err("kMalformedInput", "args must be a JSON object"))
        return 1
    if not isinstance(ap, dict):
        print(err("kMalformedInput", "args must be a JSON object"))
        return 1

    ctx = Context(store_dir, roster)
    ctx.flag_registry = fr
    e = load_context(ctx)
    if e:
        print(err("kStoreError", e))
        return 1
    print(handle_method(method, ap, ctx))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
