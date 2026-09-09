"""Behavioral reference fake for the C++ settings host (settings-host-protocol v1).

This is a BEHAVIORAL mirror of xr-core/settings/host/settings_host +
settings/core, NOT a binding. It exists so dependent tracks (P13/P16/P21) can
develop against the settings host over the SAME stdio JSON protocol without a
C++ toolchain, and so `xrctl settings --backend fake|cpp` drives either
interchangeably. The parity harness (xr-browser tools/tests/
test_p8_settings_themes.py) asserts this fake is BYTE-IDENTICAL to the
compiled C++ host over the full method surface (P6/P7 pattern).

Contract laws honored here (mirror the C++):
  * search ranking is the settings-core scorer (word exact/prefix/subsequence
    squares + phrase dominance) — identical integers, identical tie-breaks;
  * sections/anchors derive from the schema DATA; availability reads the
    PINNED policy state (deny on unknown predicates / unknown fields);
  * routers never invent anchors; unresolvable anchors are typed errors with
    prefix-derived suggestions;
  * counters are LOCAL, day-granular, disposable-safe (no --store-dir =>
    zero bytes ever written) and deny-preserve (a ledger that fails to load
    is kept and Save refuses to overwrite it);
  * flag xr_settings_v0=off => nothing renders/writes: sections/search are
    empty and every other method returns the typed flag-off refusal.
  * set() refuses policy-owned rows with the typed surfaced-by-policy reason
    (enterprise preemption, P6-T7) — v0 ships no user write path.

Stdlib only. Python 3.12+. Exit: 0 typed result, 1 typed error, 2 usage.
Canonical JSON is sorted-keys compact with ensure_ascii escapes (byte-
identical to the C++ JsonValue::Canonical() serializer).
"""
from __future__ import annotations

import argparse
import datetime
import json
import sys
from pathlib import Path
from typing import Any

FLAG_NAME = "xr_settings_v0"
STATE_SCHEMA = "xr-settings-state"
STATE_VERSION = 1
COUNTERS_SCHEMA = "xr-settings-counters"
COUNTERS_VERSION = 1
COUNTERS_RETENTION_DAYS = 90
COUNTERS_FILE = "settings-counters.json"


def canonical(obj: Any) -> str:
    """Byte-stable canonical JSON: sorted keys, compact, ASCII-escaped."""
    return json.dumps(obj, sort_keys=True, separators=(",", ":"))


def err(code: str, detail: str = "") -> dict[str, Any]:
    o: dict[str, Any] = {"error": code}
    if detail:
        o["detail"] = detail
    return o


def lower_ascii(s: str) -> str:
    return "".join(chr(ord(c) + 32) if 65 <= ord(c) <= 90 else c for c in s)


# ---------------------------------------------------------------------------
# schema (strict loader mirroring settings/core/settings_schema.cc)
# ---------------------------------------------------------------------------

class SchemaError(Exception):
    pass


def _load_schema_doc(text: str) -> dict[str, Any]:
    try:
        doc = json.loads(text)
    except json.JSONDecodeError as e:
        raise SchemaError(f"schema file is not strict JSON: {e}")
    if not isinstance(doc, dict):
        raise SchemaError("schema doc must be an object")
    sv = doc.get("schema_version")
    if not isinstance(sv, int):
        raise SchemaError("schema doc missing integer schema_version")
    for key in ("sections", "settings"):
        if not isinstance(doc.get(key), list):
            raise SchemaError(f"schema doc missing {key} array")
    anchor = doc.get("anchor_root", "xr://settings")
    if not isinstance(anchor, str):
        raise SchemaError("anchor_root must be a string")
    return {"schema_version": sv, "anchor_root": anchor,
            "sections": doc["sections"], "settings": doc["settings"]}


class Schema:
    """Strict settings schema: sections + rows from data, deny-safe defaults."""

    def __init__(self) -> None:
        self.version = 0
        self.anchor_root = "xr://settings"
        self.sections: list[dict[str, Any]] = []
        self.rows: dict[str, dict[str, Any]] = {}
        self.order: list[str] = []
        self.writable: list[str] = []
        self._enum_options: dict[str, list[str]] = {}

    def load_text(self, text: str) -> None:
        d = _load_schema_doc(text)
        self.version = d["schema_version"]
        self.anchor_root = d["anchor_root"]
        self.sections = list(d["sections"])
        self.rows = {}
        self.order = []
        self.writable = list(d.get("writable_in_v0", []))
        self._enum_options = {k: list(v) for k, v in
                              (d.get("enum_options") or {}).items()}
        sec_ids = {s.get("id") for s in self.sections if isinstance(s, dict)}
        for row in d["settings"]:
            if not isinstance(row, dict):
                raise SchemaError("setting row must be an object")
            pol = row.get("policy")
            if pol is not None:
                if not isinstance(pol, dict):
                    raise SchemaError(f"'{row.get('key')}': policy must be object")
                bad = set(pol) - {"kind", "field"}
                if bad:
                    raise SchemaError(f"{row.get('key')}: policy unknown field "
                                      f"'{sorted(bad)[0]}'")
            known = {"key", "type", "default", "attention_tier", "scope",
                     "section", "title_id", "desc_id", "aliases",
                     "security_relevant", "policy"}
            bad = set(row) - known
            if bad:
                raise SchemaError(f"setting row: unknown field '{sorted(bad)[0]}'")
            key = row.get("key")
            if not isinstance(key, str):
                raise SchemaError("setting row missing key")
            if "default" not in row:
                raise SchemaError(f"'{key}': missing default (deny-safe law)")
            sec = row.get("section")
            if sec not in sec_ids:
                raise SchemaError(f"'{key}' has unknown section '{sec}'")
            # owning section = the row's key prefix
            prefix = key.split(".", 1)[0] if "." in key else key
            if sec != prefix:
                raise SchemaError(
                    f"setting '{key}' section '{sec}' != owning section "
                    f"'{prefix}'")
            self.rows[key] = row
            self.order.append(key)
        for key in self.writable:
            if key not in self.rows:
                raise SchemaError("writable_in_v0 entry must be a known key")
        # sections referencing unknown rows
        for s in self.sections:
            for ref in s.get("settings", []):
                if ref not in self.rows:
                    raise SchemaError(
                        f"section '{s.get('id')}' references unknown setting "
                        f"'{ref}'")

    # -- queries ------------------------------------------------------------
    def find_setting(self, key: str) -> dict[str, Any] | None:
        return self.rows.get(key)

    def find_section(self, sid: str) -> dict[str, Any] | None:
        for s in self.sections:
            if s.get("id") == sid:
                return s
        return None

    def known_key(self, key: str) -> bool:
        return key in self.rows

    def enum_options(self, key: str) -> list[str]:
        return self._enum_options.get(key, [])

    def writable_in_v0(self, key: str) -> bool:
        return key in self.writable

    def sections_in_order(self) -> list[dict[str, Any]]:
        return sorted(self.sections, key=lambda s: int(s.get("order", 0)))

    def settings_in_order(self) -> list[dict[str, Any]]:
        return [self.rows[k] for k in self.order]


# ---------------------------------------------------------------------------
# pinned policy state (mirror of sections.cc PolicyState)
# ---------------------------------------------------------------------------

class StateError(Exception):
    pass


def _parse_state(text: str) -> dict[str, Any]:
    try:
        doc = json.loads(text)
    except json.JSONDecodeError as e:
        raise StateError(f"state doc is not strict JSON: {e}")
    if not isinstance(doc, dict):
        raise StateError("state doc must be an object")
    if doc.get("schema") != STATE_SCHEMA:
        raise StateError("state doc: schema must be 'xr-settings-state'")
    if doc.get("schema_version") != STATE_VERSION:
        raise StateError("state doc: schema_version must be 1")
    ctx = doc.get("context")
    if ctx is not None:
        if not isinstance(ctx, dict):
            raise StateError("state doc: context must be object")
        bad = set(ctx) - {"identity_id", "origin"}
        if bad:
            raise StateError(f"state doc: context unknown field '{sorted(bad)[0]}'")
    values = doc.get("values")
    if not isinstance(values, dict):
        raise StateError("state doc: values object required")
    out: dict[str, Any] = {}
    for key, row in values.items():
        if not isinstance(row, dict):
            raise StateError(f"state doc: value row for '{key}' not object")
        bad = set(row) - {"value", "source", "managed"}
        if bad:
            raise StateError(f"state doc: row '{key}' unknown field '{sorted(bad)[0]}'")
        if "value" not in row:
            raise StateError(f"state doc: row '{key}' missing value")
        src = row.get("source", "resolver")
        if src not in ("resolver", "enterprise"):
            raise StateError(
                "state doc: row '{key}' source must be resolver|enterprise")
        out[key] = {"value": row["value"], "source": src}
    identity_id = (ctx or {}).get("identity_id")
    return {"context": ctx or {}, "values": out,
            "identity_active": isinstance(identity_id, str) and bool(identity_id)}


class PolicyState:
    def __init__(self) -> None:
        self.values: dict[str, Any] = {}
        self.identity_active = False

    def load_text(self, text: str) -> None:
        d = _parse_state(text)
        self.values = d["values"]
        self.identity_active = d["identity_active"]

    def get(self, key: str) -> tuple[bool, Any]:
        if key in self.values:
            row = self.values[key]
            return True, row
        return False, None


# ---------------------------------------------------------------------------
# section registry (generated from schema data + pinned state)
# ---------------------------------------------------------------------------

def section_available(section: dict[str, Any], state: PolicyState) -> tuple[bool, str]:
    av = section.get("availability", "always")
    if av == "always":
        return True, ""
    if av == "identity.active":
        if state.identity_active:
            return True, ""
        return False, "no active identity in the pinned snapshot"
    return False, (f"unknown availability predicate '{av}' (deny — L3)")


def build_registry(schema: Schema, state: PolicyState) -> list[dict[str, Any]]:
    reg: list[dict[str, Any]] = []
    for s in schema.sections_in_order():
        avail, reason = section_available(s, state)
        anchor = f"{schema.anchor_root}/{s['id']}"
        keys = [k for k in s.get("settings", []) if k in schema.rows]
        row = {"id": s["id"], "order": int(s.get("order", 0)),
               "title_id": s.get("title_id", ""), "anchor": anchor,
               "available": avail, "availability": s.get("availability",
                                                         "always"),
               "setting_keys": keys}
        if not avail:
            row["unavailable_reason"] = reason
        reg.append(row)
    return reg


# ---------------------------------------------------------------------------
# search (exact port of settings/core/search.cc scoring semantics)
# ---------------------------------------------------------------------------

def _is_subseq(needle: str, hay: str) -> bool:
    pos = 0
    for c in needle:
        found = False
        while pos < len(hay):
            if hay[pos] == c:
                pos += 1
                found = True
                break
            pos += 1
        if not found:
            return False
    return True


def word_token_score(word: str, token: str) -> int:
    if not word or not token:
        return 0
    if token == word:
        return 3
    if len(word) >= 3 and len(token) > len(word) and token.startswith(word):
        return 2
    if len(word) >= 3 and _is_subseq(word, token):
        return 1
    return 0


def _join_no_spaces(s: str) -> str:
    return s.replace(" ", "")


def build_index(schema: Schema) -> list[dict[str, Any]]:
    idx: list[dict[str, Any]] = []
    order = 0
    for s in schema.sections_in_order():
        idx.append({"key": s["id"], "kind": "section", "order": order,
                    "fields": [lower_ascii(s["id"])]})
        order += 1
    for row in schema.settings_in_order():
        fields = [row["key"], row["section"]] + list(row.get("aliases", []))
        idx.append({"key": row["key"], "kind": "setting", "order": order,
                    "fields": [lower_ascii(f) for f in fields]})
        order += 1
    return idx


def match_query(query: str, index: list[dict[str, Any]]) -> list[dict[str, Any]]:
    q = lower_ascii(query)
    words = [w for w in q.split(" ") if w and len(w) > 1]
    qj = _join_no_spaces(q)
    if not qj:
        return []
    scored: list[tuple[str, str, int, int]] = []  # key, kind, score, order
    for e in index:
        total = 0
        for w in words:
            best = 0
            for f in e["fields"]:
                for tok in f.split(" "):
                    v = word_token_score(w, tok)
                    if v > best:
                        best = v
                joined = _join_no_spaces(f)
                vj = word_token_score(w, joined)
                if vj > best:
                    best = vj
            total += best * best
        phrase = 0
        for f in e["fields"]:
            fj = _join_no_spaces(f)
            if not fj:
                continue
            if fj == qj:
                phrase = 300
                break
            if phrase < 150 and _is_subseq(qj, fj):
                phrase = 150
        score = total + phrase
        if score > 0:
            scored.append((e["key"], e["kind"], score, e["order"]))
    scored.sort(key=lambda t: (-t[2], t[3]))
    return [{"key": k, "kind": kind, "score": s, "order": o}
            for k, kind, s, o in scored]


# ---------------------------------------------------------------------------
# router (mirror of router.cc)
# ---------------------------------------------------------------------------

class Router:
    def __init__(self, schema: Schema) -> None:
        self.schema = schema

    @staticmethod
    def anchor_suffix(section: str, key: str) -> str:
        if len(key) > len(section) + 1 and key.startswith(section + "."):
            return key[len(section) + 1:]
        return key

    def section_anchor(self, section: str) -> str:
        if self.schema.find_section(section) is None:
            return ""
        return f"{self.schema.anchor_root}/{section}"

    def setting_anchor(self, setting: str) -> str:
        defn = self.schema.find_setting(setting)
        if defn is None:
            return ""
        return (f"{self.schema.anchor_root}/{defn['section']}/"
                f"{self.anchor_suffix(defn['section'], setting)}")

    def section_anchors(self) -> list[str]:
        return [self.section_anchor(s["id"])
                for s in self.schema.sections_in_order()]

    def resolve(self, anchor: str) -> dict[str, Any]:
        r: dict[str, Any] = {"ok": False, "kind": "unknown"}
        path = anchor
        prefix = "xr://settings"
        if path.startswith(prefix):
            path = path[len(prefix):]
        if path.startswith("/"):
            path = path[1:]
        parts = [p for p in path.split("/") if p]
        if not parts:
            r.update({"ok": True, "kind": "home",
                      "canonical": self.schema.anchor_root})
            return r
        sec = self.schema.find_section(parts[0])
        if sec is None:
            r["error"] = (f"unknown section '{parts[0]}' (anchors derive from "
                          "the schema — no section, no anchor)")
            for s in self.schema.sections_in_order():
                if s["id"].startswith(parts[0]) or parts[0].startswith(s["id"]):
                    r.setdefault("suggestions", []).append(
                        f"{self.schema.anchor_root}/{s['id']}")
            return r
        if len(parts) == 1:
            r.update({"ok": True, "kind": "section", "section": sec["id"],
                      "canonical": f"{self.schema.anchor_root}/{sec['id']}"})
            return r
        if len(parts) == 2:
            defn = self.schema.find_setting(parts[1])
            if (defn is None or defn["section"] != sec["id"]) and "." not in parts[1]:
                for row in self.schema.settings_in_order():
                    if (row["section"] == sec["id"] and
                            self.anchor_suffix(sec["id"], row["key"]) == parts[1]):
                        defn = row
                        break
            if defn is not None and defn["section"] == sec["id"]:
                r.update({"ok": True, "kind": "setting", "section": sec["id"],
                          "setting": defn["key"],
                          "canonical": f"{self.schema.anchor_root}/{sec['id']}/"
                          f"{self.anchor_suffix(sec['id'], defn['key'])}"})
                return r
            r["error"] = (f"unknown setting '{parts[1]}' in section "
                          f"'{sec['id']}'")
            return r
        r["error"] = "anchor path too deep (xr://settings/<section>[/<setting>])"
        return r


# ---------------------------------------------------------------------------
# counters (mirror of counters.cc; day-granular, local, deny-preserve)
# ---------------------------------------------------------------------------

class CounterStore:
    def __init__(self, store_dir: str = "") -> None:
        self.dir = store_dir
        self.days: dict[str, dict[str, Any]] = {}
        self.loaded = False
        self.load_failed = False

    def path(self) -> str:
        return "" if not self.dir else f"{self.dir}/{COUNTERS_FILE}"

    def load(self) -> tuple[bool, bool, str]:
        """Returns (ok_or_absent, preserved, error)."""
        self.days = {}
        self.loaded = True
        self.load_failed = False
        if not self.dir:
            return True, False, ""
        p = Path(self.path())
        if not p.is_file():
            return True, False, ""
        try:
            text = p.read_text(encoding="utf-8")
        except Exception:
            return False, True, ("ledger corrupt — kept as-is, nothing "
                                 "rewritten (deny-preserve)")
        try:
            doc = json.loads(text)
        except json.JSONDecodeError:
            self.load_failed = True
            return False, True, ("ledger corrupt — kept as-is, nothing "
                                 "rewritten (deny-preserve)")
        if not isinstance(doc, dict) or (doc.get("schema") != COUNTERS_SCHEMA or
                                         doc.get("schema_version") !=
                                         COUNTERS_VERSION):
            self.load_failed = True
            return False, True, ("ledger schema mismatch — kept as-is "
                                 "(downgrade no-op law)")
        days = doc.get("days")
        if isinstance(days, dict):
            for day, row in days.items():
                if isinstance(row, dict):
                    self.days[day] = row
        return True, False, ""

    @staticmethod
    def _utc_today() -> str:
        return datetime.datetime.now(datetime.timezone.utc).date().isoformat()

    def _day_ref(self) -> dict[str, Any]:
        day = self._utc_today()
        if day not in self.days:
            self.days[day] = {}
        return self.days[day]

    def open_section(self, section: str) -> None:
        if not section or not self.loaded:
            return
        day = self._day_ref()
        opened = day.setdefault("opened", {})
        opened[section] = int(opened.get(section, 0)) + 1

    def accept_query(self) -> None:
        if not self.loaded:
            return
        day = self._day_ref()
        day["queries"] = int(day.get("queries", 0)) + 1

    def setting_changed(self, key: str) -> None:
        if not key or not self.loaded:
            return
        day = self._day_ref()
        changed = day.setdefault("changed", {})
        changed[key] = int(changed.get(key, 0)) + 1

    def save(self) -> tuple[bool, str]:
        if not self.dir:
            return True, ""
        p = Path(self.path())
        if self.load_failed and p.is_file():
            return False, ("on-disk ledger failed to load and is preserved — "
                           "refusing to overwrite (deny-preserve)")
        cutoff = (datetime.date.fromisoformat(self._utc_today()) -
                  datetime.timedelta(days=COUNTERS_RETENTION_DAYS)).isoformat()
        days = {d: row for d, row in self.days.items() if d >= cutoff}
        doc = {"schema": COUNTERS_SCHEMA, "schema_version": COUNTERS_VERSION,
               "days": days}
        try:
            p.parent.mkdir(parents=True, exist_ok=True)
            tmp = Path(str(p) + ".tmp")
            tmp.write_text(canonical(doc) + "\n", encoding="utf-8")
            tmp.replace(p)
        except OSError as e:
            return False, str(e)
        return True, ""

    def dump(self) -> dict[str, Any]:
        doc: dict[str, Any] = {"schema": COUNTERS_SCHEMA,
                               "schema_version": COUNTERS_VERSION,
                               "days": dict(self.days),
                               "in_memory": not self.dir}
        if self.load_failed:
            doc["load_error"] = "preserved"
        return doc

    def total_queries(self) -> int:
        return sum(int(day.get("queries", 0)) for day in self.days.values())


# ---------------------------------------------------------------------------
# context + method handlers
# ---------------------------------------------------------------------------

class Context:
    def __init__(self) -> None:
        self.schema = Schema()
        self.state = PolicyState()
        self.counters = CounterStore()
        self.flag = "on"


def load_context(ctx: Context, store_dir: str, schema_path: str,
                 state_path: str) -> str:
    """Returns '' on success or the typed kStoreError message."""
    try:
        text = Path(schema_path).read_text(encoding="utf-8")
    except OSError as e:
        return f"kStoreError|schema file not readable: {schema_path} ({e})"
    try:
        ctx.schema.load_text(text)
    except SchemaError as e:
        return f"kStoreError|settings schema rejected (strict): {e}"
    if state_path:
        try:
            stext = Path(state_path).read_text(encoding="utf-8")
        except OSError as e:
            return f"kStoreError|state file not readable: {state_path} ({e})"
        try:
            ctx.state.load_text(stext)
        except StateError as e:
            return f"kStoreError|policy state rejected (strict): {e}"
    ctx.counters = CounterStore(store_dir)
    ok, _preserved, cerr = ctx.counters.load()
    if not ok and cerr:
        return f"kStoreError|{cerr}"
    return ""


def _typed_value(schema: Schema, key: str, v: Any) -> tuple[bool, str]:
    defn = schema.find_setting(key)
    if defn is None:
        return False, ""
    t = defn["type"]
    ok_type = ((t == "bool" and isinstance(v, bool)) or
               (t in ("enum", "string") and isinstance(v, str)) or
               (t == "int" and isinstance(v, int) and not isinstance(v, bool)))
    if not ok_type:
        return False, f"policy state value for '{key}' mismatches declared type '{t}' (never guess)"
    if t == "enum" and v not in schema.enum_options(key):
        return False, f"policy state value for '{key}' not in declared enum options"
    return True, ""


def m_flag_status(ctx: Context) -> dict[str, Any]:
    return {"xr_settings_v0": "on" if ctx.flag == "on" else "off"}


def m_sections(ctx: Context) -> dict[str, Any]:
    if ctx.flag != "on":
        return err("kRejected",
                   "feature flag xr_settings_v0 is off — nothing renders")
    reg = build_registry(ctx.schema, ctx.state)
    out = []
    for s in reg:
        row = {"id": s["id"], "title_id": s["title_id"], "anchor": s["anchor"],
               "available": s["available"], "availability": s["availability"],
               "settings": s["setting_keys"]}
        if not s["available"]:
            row["unavailable_reason"] = s["unavailable_reason"]
        out.append(row)
    return {"sections": out, "count": len(out)}


def m_search(ctx: Context, args: dict[str, Any]) -> dict[str, Any]:
    q = args.get("query", "")
    if not isinstance(q, str):
        q = ""
    if ctx.flag != "on":
        return {"results": [], "count": 0}
    index = build_index(ctx.schema)
    results = match_query(q, index)
    rows = [{"key": r["key"], "kind": r["kind"], "score": r["score"]}
            for r in results]
    recorded = False
    for r in results:
        if r["kind"] == "setting":
            ctx.counters.accept_query()
            recorded = True
    if recorded:
        ctx.counters.save()
    return {"results": rows, "count": len(rows)}


def m_get(ctx: Context, args: dict[str, Any]) -> dict[str, Any]:
    k = args.get("key", "")
    if not isinstance(k, str) or not k:
        return err("kMalformedInput", "get: 'key' string required")
    if ctx.flag != "on":
        return err("kRejected", "feature flag xr_settings_v0 is off")
    defn = ctx.schema.find_setting(k)
    if defn is None:
        return err("kRejected", f"unknown setting key '{k}' (strict: a setting "
                   "without a schema entry does not ship)")
    o: dict[str, Any] = {"key": k}
    source = "default"
    has, row = ctx.state.get(k)
    if has:
        ok_t, verr = _typed_value(ctx.schema, k, row["value"])
        if not ok_t:
            return err("kRejected", verr)
        o["value"] = row["value"]
        source = row["source"]
    else:
        o["value"] = defn.get("default")
    o["source"] = source
    o["preempted"] = source == "enterprise"
    o["writable"] = ctx.schema.writable_in_v0(k)
    o["section"] = defn["section"]
    o["attention_tier"] = defn["attention_tier"]
    o["scope"] = defn["scope"]
    return o


def m_set(ctx: Context, args: dict[str, Any]) -> dict[str, Any]:
    k = args.get("key", "")
    value = args.get("value") if "value" in args else None
    if not isinstance(k, str) or not k or "value" not in args:
        return err("kMalformedInput", "set: 'key' + 'value' required")
    if ctx.flag != "on":
        return err("kRejected", "feature flag xr_settings_v0 is off — nothing writes")
    defn = ctx.schema.find_setting(k)
    if defn is None:
        return err("kRejected", f"unknown setting key '{k}' (strict)")
    if not ctx.schema.writable_in_v0(k):
        return err("kRejected", f"setting '{k}' is surfaced-by-policy-only in v0 "
                   "— no user write path (writes land with the P13+ settings "
                   "store); never silently override")
    ok_t, verr = _typed_value(ctx.schema, k, value)
    if not ok_t:
        return err("kMalformedInput", verr)
    ctx.counters.setting_changed(k)
    ok_s, serr = ctx.counters.save()
    if not ok_s:
        return err("kIoError",
                   "counters persist failed (setting change not lost in-memory)"
                   + (f": {serr}" if serr else ""))
    return {"key": k, "status": "set",
            "persisted": bool(ctx.counters.dir)}


def m_router_resolve(ctx: Context, args: dict[str, Any]) -> dict[str, Any]:
    anchor = args.get("anchor", "")
    if not isinstance(anchor, str):
        anchor = ""
    if ctx.flag != "on":
        return err("kRejected", "feature flag xr_settings_v0 is off")
    router = Router(ctx.schema)
    r = router.resolve(anchor)
    if r["ok"] and r["kind"] in ("section", "setting"):
        ctx.counters.open_section(r["section"])
        ctx.counters.save()
    return r


def m_counters_dump(ctx: Context) -> dict[str, Any]:
    if ctx.flag != "on":
        return err("kRejected",
                   "feature flag xr_settings_v0 is off — no counters written")
    return ctx.counters.dump()


def m_schema_dump(ctx: Context) -> dict[str, Any]:
    if ctx.flag != "on":
        return err("kRejected", "feature flag xr_settings_v0 is off")
    return {"schema": "xr-settings-schema",
            "schema_version": ctx.schema.version,
            "anchor_root": ctx.schema.anchor_root,
            "keys": list(ctx.schema.order), "count": len(ctx.schema.order)}


HANDLERS = {
    "flag-status": (m_flag_status, False),
    "sections": (m_sections, False),
    "search": (m_search, True),
    "get": (m_get, True),
    "set": (m_set, True),
    "router-resolve": (m_router_resolve, True),
    "counters-dump": (m_counters_dump, False),
    "schema-dump": (m_schema_dump, False),
}


def _emit(obj: Any) -> int:
    sys.stdout.write(canonical(obj) + "\n")
    return 0


def _emit_err(code: str, detail: str = "") -> int:
    sys.stdout.write(canonical(err(code, detail)) + "\n")
    return 1


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="settings.py", add_help=False)
    ap.add_argument("--store-dir", default="")
    ap.add_argument("--schema", default="settings/core/settings_schema_v1.json")
    ap.add_argument("--state", default="")
    ap.add_argument("--flag", default="xr_settings_v0=on")
    opts, rest = ap.parse_known_args(argv)

    flag = "off" if opts.flag.endswith("=off") else "on"
    if opts.flag.split("=", 1)[0] != FLAG_NAME or \
            opts.flag.split("=", 1)[1] not in ("on", "off"):
        sys.stderr.write("usage error: --flag xr_settings_v0=on|off\n")
        return 2

    # (method, args-json) resolution — mirrors the C++ argv/stdio forms.
    method = ""
    args_text = "{}"
    if not rest:
        args_text = sys.stdin.read()
        try:
            req = json.loads(args_text)
        except json.JSONDecodeError as e:
            return _emit_err("kMalformedInput", f"json parse error: {e}")
        if not isinstance(req, dict):
            return _emit_err("kMalformedInput", "request must be an object")
        m = req.get("method")
        method = m if isinstance(m, str) else ""
        args_text = canonical(req.get("args", {})) if "args" in req else "{}"
    elif rest[0] in HANDLERS:
        method = rest[0]
        if len(rest) >= 2:
            args_text = rest[1]
    else:
        args_text = rest[0]
        try:
            req = json.loads(args_text)
        except json.JSONDecodeError as e:
            return _emit_err("kMalformedInput", f"json parse error: {e}")
        m = req.get("method") if isinstance(req, dict) else None
        method = m if isinstance(m, str) else ""
        args_text = canonical(req.get("args", {})) if isinstance(req, dict) \
            and "args" in req else "{}"
    if not method:
        sys.stderr.write("usage: settings.py <method> ['<json-args>'] "
                         "[options] | '<json-with-method>' | stdin\n")
        return 2
    try:
        args = json.loads(args_text) if args_text.strip() else {}
    except json.JSONDecodeError as e:
        return _emit_err("kMalformedInput", f"json parse error: {e}")
    if not isinstance(args, dict):
        return _emit_err("kMalformedInput", "args must be a JSON object")

    ctx = Context()
    ctx.flag = flag
    if method == "flag-status":
        return _emit(m_flag_status(ctx))
    if flag == "on":
        lr = load_context(ctx, opts.store_dir, opts.schema, opts.state)
        if lr:
            code, _, detail = lr.partition("|")
            return _emit_err(code, detail)
    if method not in HANDLERS:
        return _emit(err("kUnknownMethod", f"unknown method '{method}'"))
    handler, takes_args = HANDLERS[method]
    result = handler(ctx, args) if takes_args else handler(ctx)
    return _emit(result)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
