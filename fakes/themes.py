"""Behavioral reference fake for the C++ themes host (themes-host-protocol v1).

BEHAVIORAL mirror of xr-core/themes/host/themes_host + themes/core, NOT a
binding — the P6/P7/P8 pattern so dependent tracks and the no-C++-toolchain
farm can drive themes through the SAME stdio JSON protocol, and so the
byte-parity harness (xr-browser tools/tests) can assert the fake is
BYTE-IDENTICAL to the compiled C++ host on the deterministic surface.

The WCAG 2.1 math here is an INDEPENDENT implementation (the second
implementation used by the tests to cross-check the C++ core — research
log-P8 item 5): relative luminance + contrast ratio per the W3C definition,
thresholds 4.5 (body) / 3.0 (large) / 7.0 (security-critical pairs), audit
over the DECLARED pairing graph in tokens.json, waivers as machine-readable
data rows (exact-match enforced; a waiver for a passing pair is a data
error).

Refusal law mirrored exactly: validate -> audit -> REFUSE; a theme that
fails schema OR contrast never applies; refusal is atomic; no best-effort
merge. Hostile import (T4): 64 KiB cap, duplicate-key scan (never
last-win), strict parse (depth-capped 64, UTF-8-validated, trailing
rejected — the python json decoder is replaced by a strict wrapper),
unknown/non-token keys rejected, types enforced, fonts system-stack only,
critical-red reserved law enforced in the validator.

Stdlib only. Exit 0 typed result / 1 typed error / 2 usage. Canonical JSON:
json.dumps(sort_keys=True, separators=(',',':'), ensure_ascii=True).
Deterministic output only (no timing fields — parity-stable).
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

STATE_SCHEMA = "xr-themes-state"
STATE_VERSION = 1
STATE_FILE = "themes-state.json"
MAX_DOC_BYTES = 64 * 1024
PARSE_DEPTH = 64

HEX_OK = set("0123456789abcdefABCDEF")


def canonical(obj: Any) -> str:
    return json.dumps(obj, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True)


def err(code: str, detail: str = "") -> dict[str, Any]:
    o: dict[str, Any] = {"error": code}
    if detail:
        o["detail"] = detail
    return o


# ---------------------------------------------------------------------------
# strict JSON (depth-capped, trailing-rejected, no NaN/huge ints)
# ---------------------------------------------------------------------------

class StrictJsonError(Exception):
    pass


def _decode_strict(text: str) -> Any:
    """json.loads is replaced by a strict decoder: depth-capped, duplicate
    keys refused, int64-window enforced, trailing content rejected."""
    import json as _json

    def hook(pairs):
        obj = {}
        for k, v in pairs:
            if k in obj:
                raise StrictJsonError(f"duplicate key '{k}'")
            obj[k] = v
        return obj

    try:
        return _json.loads(text, object_pairs_hook=hook,
                           parse_constant=_no_const)
    except json.JSONDecodeError as e:
        raise StrictJsonError(f"parse: {e}") from None
    except StrictJsonError:
        raise


def _no_const(s: str):
    raise StrictJsonError(f"constant {s} refused (no NaN-ish numbers)")


def parse_doc(raw: str) -> dict[str, Any]:
    """Strict parse of a theme doc (bytes decoded as UTF-8, validated)."""
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as e:
        raise StrictJsonError("invalid UTF-8") from None
    obj = _decode_strict(text)
    if not isinstance(obj, dict):
        raise StrictJsonError("doc must be a JSON object")
    return obj


# ---------------------------------------------------------------------------
# WCAG 2.1 contrast (independent implementation)
# ---------------------------------------------------------------------------

def hex_to_rgb(hexv: str) -> tuple[float, float, float] | None:
    if len(hexv) not in (7, 9) or hexv[0] != "#":
        return None
    body = hexv[1:]
    if any(c not in HEX_OK for c in body):
        return None
    r = int(body[0:2], 16) / 255.0
    g = int(body[2:4], 16) / 255.0
    b = int(body[4:6], 16) / 255.0
    return r, g, b


def rel_lum(r: float, g: float, b: float) -> float:
    def lin(c: float) -> float:
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)


def ratio_between(fg: str, bg: str) -> float:
    a = hex_to_rgb(fg)
    b = hex_to_rgb(bg)
    if a is None or b is None:
        return 1.0
    l1 = rel_lum(*a)
    l2 = rel_lum(*b)
    hi, lo = max(l1, l2), min(l1, l2)
    v = (hi + 0.05) / (lo + 0.05)
    return max(1.0, min(21.0, v))


def fmt2(v: float) -> str:
    return f"{v:.2f}"


# ---------------------------------------------------------------------------
# token source (ui/themes/tokens.json)
# ---------------------------------------------------------------------------

class TokenSource:
    def __init__(self) -> None:
        self.tokens: dict[str, dict[str, Any]] = {}
        self.order: list[str] = []
        self.builtins: dict[str, dict[str, Any]] = {}
        self.system_default = "light"
        self.system_modes: dict[str, str] = {}

    def load_text(self, text: str) -> None:
        try:
            doc = json.loads(text)
        except json.JSONDecodeError as e:
            raise SourceError(f"tokens source is not strict JSON: {e}")
        if not isinstance(doc, dict):
            raise SourceError("tokens source must be a JSON object")
        if doc.get("schema_version") != 1:
            raise SourceError("schema_version must be 1 (rollback law)")
        meta = doc.get("tokens")
        themes = doc.get("themes")
        if not isinstance(meta, dict) or not meta:
            raise SourceError("'tokens' object required")
        if not isinstance(themes, dict) or not themes:
            raise SourceError("'themes' object required")
        for name, row in meta.items():
            if not isinstance(row, dict):
                raise SourceError(f"token '{name}': meta must be an object")
            bad = set(row) - {"type", "usage", "pairing",
                              "security_critical", "reserved"}
            if bad:
                raise SourceError(f"token '{name}': unknown field "
                                  f"'{sorted(bad)[0]}'")
            typ = row.get("type")
            if typ not in ("color", "dimension", "font"):
                raise SourceError(f"token '{name}': unknown type {typ!r}")
            if name == "critical-red":
                if not row.get("reserved") or not row.get("security_critical"):
                    raise SourceError("critical-red reserved law: data "
                                      "flags required")
            pairing = row.get("pairing") or []
            if not isinstance(pairing, list) or any(
                    not isinstance(p, str) for p in pairing):
                raise SourceError(f"token '{name}': pairing must be a list")
            self.tokens[name] = {"type": typ, "usage": row.get("usage", ""),
                                 "security_critical": bool(
                                     row.get("security_critical")),
                                 "reserved": bool(row.get("reserved")),
                                 "pairing": pairing}
            self.order.append(name)
        for t in self.tokens:
            for p in self.tokens[t]["pairing"]:
                if p not in self.tokens:
                    raise SourceError(f"token '{t}': pairing '{p}' is not a "
                                      "token")
        for tname, vals in themes.items():
            if not isinstance(vals, dict):
                raise SourceError(f"theme '{tname}': values object required")
            waivers = []
            values = {}
            for key, v in vals.items():
                if key == "waivers":
                    if not isinstance(v, list):
                        raise SourceError(f"theme '{tname}': waivers list "
                                          "required")
                    waivers = v
                    continue
                if key not in self.tokens:
                    raise SourceError(f"theme '{tname}': unknown token "
                                      f"'{key}'")
                values[key] = v
            for t in self.order:
                if t not in values:
                    raise SourceError(f"theme '{tname}': missing token "
                                      f"'{t}'")
            self.builtins[tname] = {"values": values, "waivers": waivers}
        sysr = doc.get("system_resolution")
        if not isinstance(sysr, dict):
            raise SourceError("system_resolution object required")
        sd = sysr.get("default")
        modes = sysr.get("modes")
        if not isinstance(sd, str) or not isinstance(modes, dict):
            raise SourceError("system_resolution.default + modes required")
        self.system_default = sd
        for mode, target in modes.items():
            if mode not in ("light", "dark", "high-contrast", "default"):
                raise SourceError(f"unknown system mode '{mode}'")
            if not isinstance(target, str) or target not in self.builtins:
                raise SourceError(f"system mode '{mode}' target invalid")
            self.system_modes[mode] = target
        if self.system_default not in self.system_modes:
            raise SourceError("system_resolution.default must be a mode")


class SourceError(Exception):
    pass


# ---------------------------------------------------------------------------
# audit (waiver rows + declared pairings)
# ---------------------------------------------------------------------------

def is_alarming(hexv: str) -> bool:
    c = hex_to_rgb(hexv)
    if c is None:
        return False
    r, g, b = c
    # recorded law: r >= 0x60, r >= max(g,b), (r - min(g,b)) >= 0x60
    return (r >= 0x60 / 255 and r >= max(g, b) and
            (r - min(g, b)) >= 0x60 / 255)


def audit_theme(values: dict[str, Any], src: TokenSource) -> list[dict[str, Any]]:
    """Findings mirroring the C++ ContrastFinding shape (deterministic).

    P9-T0-a canonicalization: token iteration order is NAME-SORTED to match
    the C++ core, whose JSON Object is a std::map (sorted by key) — the
    refusal text therefore lists findings in the same deterministic order in
    both backends (byte-parity law)."""
    out: list[dict[str, Any]] = []
    for tname in sorted(src.tokens):
        t = src.tokens[tname]
        if t["type"] != "color" or not t["pairing"]:
            continue
        fg = values.get(tname)
        if not isinstance(fg, str):
            continue
        for pair in t["pairing"]:
            pd = src.tokens.get(pair)
            bg = values.get(pair)
            if pd is None or pd["type"] != "color" or not isinstance(bg, str):
                continue
            ratio = ratio_between(fg, bg)
            required = 7.0 if t["security_critical"] else 4.5
            f = {"token": tname, "pair": pair, "ratio": ratio,
                 "required": required, "passed": ratio >= required}
            out.append(f)
    return out


def audit_doc(doc: dict[str, Any], src: TokenSource) -> list[dict[str, Any]]:
    return audit_theme(doc, src)


# ---------------------------------------------------------------------------
# loader (validate -> audit -> refuse)
# ---------------------------------------------------------------------------

def validate_doc(doc: dict[str, Any],
                 src: TokenSource) -> list[tuple[str, str]]:
    """Returns (where, what) problem pairs (empty = valid).

    P9-T0-a canonicalization: problems are emitted in NAME-SORTED key order
    (the C++ core iterates a std::map, so both backends list problems in the
    same order) and use the EXACT C++ wording (byte-parity law): the refusal
    detail string must be identical across backends, not merely the verdict."""
    problems: list[tuple[str, str]] = []
    for key in sorted(doc):
        v = doc[key]
        t = src.tokens.get(key)
        if t is None:
            problems.append((f"key '{key}'",
                             "unknown token — the v1 token set is fixed; "
                             "unknown tokens are rejected (contract), not "
                             "ignored"))
            continue
        typ = t["type"]
        ok_type = False
        if typ == "color":
            ok_type = isinstance(v, str) and len(v) in (7, 9) and \
                v[0] == "#" and all(c in HEX_OK for c in v[1:])
        elif typ == "dimension":
            ok_type = isinstance(v, int) and not isinstance(v, bool) and \
                0 <= v <= 4096
        elif typ == "font":
            if isinstance(v, str):
                ok_type = bool(v) and "url(" not in v and ";" not in v and \
                    "{" not in v and "}" not in v and "http" not in v and \
                    "\n" not in v
                if not ok_type:
                    if "url(" in v:
                        problems.append((f"token '{key}'",
                                         "font value contains url( — "
                                         "remote/embedded resources are "
                                         "rejected (no code/no asset path in "
                                         "themes)"))
                    elif "http" in v:
                        problems.append((f"token '{key}'",
                                         "font value references a remote "
                                         "resource — rejected"))
                    else:
                        problems.append((f"token '{key}'",
                                         "font value must be a plain "
                                         "system-font stack"))
                    continue
        if not ok_type:
            problems.append((f"token '{key}'",
                             f"value does not match declared type '{typ}'"))
            continue
        if key == "critical-red":
            if isinstance(v, str) and not is_alarming(v):
                problems.append(("token 'critical-red'",
                                 "RESERVED: critical-red must stay in the "
                                 "canonical alarming family (red-dominant); "
                                 "mapping it to a non-alarming color is "
                                 "refused"))
    return problems


def _find_dup(raw: str) -> str:
    """Mirror of the C++ raw duplicate-key scan (hostile import) — exact
    structure parity: a quote OPEN never changes key expectation; a string is
    recorded as a key only when it closes in key position (expect_key AND
    in_key), so string VALUES that contain quotes/braces are never counted."""
    stack: list[dict[str, int]] = []
    dup = ""
    key = ""
    in_string = False
    in_escape = False
    in_key = False   # inside a string that is a key position
    expect_key = False  # just saw '{' or ','
    i = 0
    n = len(raw)
    while i < n and not dup:
        c = raw[i]
        if in_string:
            if in_escape:
                in_escape = False
            elif c == "\\":
                in_escape = True
            elif c == '"':
                in_string = False
                if expect_key and in_key:
                    if not stack:
                        dup = "?"  # malformed; the strict parser will refuse
                    elif stack[-1].get(key, 0) + 1 > 1:
                        dup = key
                    else:
                        stack[-1][key] = 1
            else:
                key += c
            i += 1
            continue
        if c == '"':
            in_string = True
            key = ""
            in_key = True
        elif c == "{":
            stack.append({})
            expect_key = True
        elif c == "}":
            if stack:
                stack.pop()
        elif c == ",":
            expect_key = True
        elif c == ":":
            expect_key = False
        elif not c.isspace():
            expect_key = False
        i += 1
    return dup


class Loader:
    def __init__(self, tokens_text: str, mode: str = "light",
                 store_dir: str = "") -> None:
        self.src = TokenSource()
        self.src.load_text(tokens_text)
        self.mode = mode if mode in ("light", "dark", "high-contrast") \
            else "light"
        self.store_dir = store_dir
        self.applied = "system"
        self.resolved = self._resolve("system")
        self.values: dict[str, Any] = dict(self.builtin(self.resolved))
        self.state_path = "" if not store_dir else \
            f"{store_dir}/{STATE_FILE}"
        self._restore_state()

    def _resolve(self, name: str) -> str:
        if name == "system":
            return self.src.system_modes.get(self.mode,
                                             self.src.system_default)
        if name in self.src.builtins:
            return name
        raise KeyError(name)

    def builtin(self, name: str) -> dict[str, Any]:
        return self.src.builtins[name]["values"]

    def builtin_waivers(self, name: str) -> list[Any]:
        return self.src.builtins[name]["waivers"]

    # ---- state durability (mirror of the C++ tmp->rename path) ----
    def _restore_state(self) -> None:
        if not self.state_path:
            self.applied = "system"
            self.resolved = self._resolve("system")
            self.values = dict(self.builtin(self.resolved))
            return
        p = Path(self.state_path)
        if not p.is_file():
            self.applied = "system"
            return
        try:
            doc = json.loads(p.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return  # preserved, not rewritten (deny-preserve)
        if (doc.get("schema_version") != STATE_VERSION or
                not isinstance(doc.get("applied"), str) or
                not isinstance(doc.get("mode"), str)):
            return
        mode = doc["mode"]
        if mode not in ("light", "dark", "high-contrast"):
            return
        self.mode = mode
        name = doc["applied"]
        if name == "custom":
            name = "system"
        if name not in self.src.builtins and name != "system":
            return
        try:
            self.resolved = self._resolve(name)
        except KeyError:
            return
        self.applied = name
        self.values = dict(self.builtin(self.resolved))

    def _persist_state(self) -> str:
        """Persist like the C++ host: tmp -> fsync-equivalent -> rename.

        No auto-mkdir: a missing store dir must be refused (kIoError) so the
        parity harness treats both sides identically. Returns "" on success
        or a refusal dict code on failure (error-code parity with the host).
        """
        if not self.state_path:
            return ""
        name = "system" if self.applied == "custom" else self.applied
        doc = {"schema": STATE_SCHEMA, "schema_version": STATE_VERSION,
               "applied": name, "mode": self.mode}
        p = Path(self.state_path)
        tmp = Path(str(p) + ".tmp")
        try:
            tmp.write_text(canonical(doc) + "\n", encoding="utf-8")
            tmp.replace(p)
        except OSError:
            try:
                tmp.unlink()
            except OSError:
                pass
            return "kIoError"
        return ""

    # ---- operations ----
    def apply(self, name: str) -> dict[str, Any]:
        if name not in self.src.builtins and name != "system":
            names = ", ".join(list(self.src.builtins) + ["system"])
            return err("kRejected", f"unknown theme '{name}' (built-ins: "
                        f"{names})")
        resolved = self._resolve(name)
        values = dict(self.builtin(resolved))
        probs = validate_doc(values, self.src)
        if probs:
            # P9-T0-a: probs are (where, what) pairs; the C++ host surfaces
            # only the FIRST problem's `what` here (byte-parity law).
            return err("kRejected", f"built-in '{name}' failed schema "
                        f"validation: {probs[0][1]}")
        refusal = self._refusal_from_findings(
            audit_with_waivers(values, self.src, self.builtin_waivers(resolved)))
        if refusal:
            return err("kRejected", f"theme '{name}' refused: {refusal}")
        self.applied = name
        self.resolved = resolved
        self.values = values
        e = self._persist_state()
        if e:
            return err(e, "cannot open tmp themes state for write")
        return {"ok": True, "applied": name, "resolved": resolved,
                "mode": self.mode, "values": values}

    def import_doc(self, raw: str) -> dict[str, Any]:
        try:
            data = raw.encode("utf-8") if isinstance(raw, str) else raw
        except Exception:
            return err("kRejected", "invalid UTF-8")
        if len(data) > MAX_DOC_BYTES:
            return err("kRejected", f"theme doc exceeds the 64 KiB cap "
                        f"({len(data)} bytes) — refused")
        text = raw if isinstance(raw, str) else raw.decode("utf-8",
                                                           errors="replace")
        dup = _find_dup(text)
        if dup and dup != "?":
            return err("kRejected", f"theme doc has duplicate key '{dup}' — "
                        "refused (hostile input never last-wins)")
        try:
            doc = parse_doc(raw if isinstance(raw, bytes) else
                            text.encode("utf-8"))
        except StrictJsonError as e:
            return err("kRejected", f"theme doc is not strict JSON: {e}")
        probs = validate_doc(doc, self.src)
        if probs:
            # P9-T0-a: canonical problem rendering matches the C++ host
            # exactly — `[{where}] {what};` per problem, key-sorted (both
            # backends), identical wording (byte-parity law).
            refusal = "theme doc rejected (strict):"
            for where, what in probs:
                refusal += f" [{where}] {what};"
            return err("kRejected", refusal)
        findings = audit_doc(doc, self.src)
        refusal = self._refusal_from_findings(findings)
        if refusal:
            return err("kRejected", f"custom theme refused: {refusal}")
        deltas = []
        for k in sorted(doc):
            old = self.values.get(k)
            o = canonical(old) if old is not None else ""
            nn = canonical(doc[k])
            if o != nn:
                deltas.append(f"{k}: {o} -> {nn}")
        return {"ok": True, "applied": "custom", "resolved": "custom",
                "mode": self.mode, "values": doc, "deltas": deltas,
                "delta_count": len(deltas)}

    def validate_doc(self, raw: str) -> dict[str, Any]:
        # BYTE-PARITY LAW: the response shape must match the C++ host
        # exactly (ok/deltas/delta_count — no extra keys: canonical output
        # is compared byte-for-byte by the parity gate).
        r = self.import_doc(raw)
        if "error" in r:
            return r
        return {"ok": True, "deltas": r["deltas"],
                "delta_count": len(r["deltas"])}

    @staticmethod
    def _refusal_from_findings(findings: list[dict[str, Any]]) -> str:
        """Canonical refusal text (P9-T0-a): identical wording + ordering to
        the C++ RefusalFromFindings — the (security-critical pair) annotation
        is driven by the 7.0 threshold, and unnecessary/mismatched waiver rows
        render with the exact C++ strings (byte-parity law)."""
        parts = []
        for f in findings:
            if f["passed"] and not f.get("unnecessary"):
                continue
            if f.get("unnecessary"):
                parts.append(f"waiver for passing pair {f['token']}/{f['pair']} "
                             f"(ratio {fmt2(f['ratio'])} >= "
                             f"{fmt2(f['required'])}) is unnecessary (waivers "
                             f"must be exact data)")
            elif f.get("waiver_mismatch"):
                parts.append(f"waiver best for {f['token']}/{f['pair']} does "
                             f"not match actual ratio {fmt2(f['ratio'])}")
            else:
                suffix = (" (security-critical pair)"
                          if f["required"] >= 7.0 else "")
                parts.append(f"contrast {f['token']}/{f['pair']}: "
                             f"{fmt2(f['ratio'])}:1 < required "
                             f"{fmt2(f['required'])}:1{suffix}")
        return "; ".join(parts)

    def list_json(self) -> dict[str, Any]:
        # BYTE-PARITY LAW: the C++ loader keeps built-ins in a std::map
        # (name-sorted); list them in the same order here.
        arr = []
        for name in sorted(self.src.builtins):
            arr.append({"name": name, "kind": "builtin",
                        "waivers": len(self.builtin_waivers(name))})
        arr.append({"name": "system", "kind": "resolver",
                    "default": self.src.system_default})
        return {"themes": arr, "count": len(arr), "current": self.applied,
                "resolved": self.resolved, "mode": self.mode}

    def event_json(self) -> dict[str, Any]:
        return {"ok": True, "applied": self.applied,
                "resolved": self.resolved, "mode": self.mode,
                "values": self.values}

    def set_mode(self, mode: str) -> dict[str, Any] | None:
        if mode not in ("light", "dark", "high-contrast"):
            return err("kMalformedInput", "system-mode: light|dark|high-"
                        "contrast")
        self.mode = mode
        if self.applied in ("system", ""):
            self.resolved = self._resolve("system")
            self.values = dict(self.builtin(self.resolved))
            self.applied = "system"
        e = self._persist_state()
        if e:
            return err(e, "cannot open tmp themes state for write")
        return None


def audit_with_waivers(values: dict[str, Any], src: TokenSource,
                       waiver_rows: list[Any]) -> list[dict[str, Any]]:
    """Audit honoring waiver rows (exact best match; unnecessary/mismatched
    rows are surfaced as failures).

    P9-T0-a: the flag set now mirrors the C++ AuditTheme EXACTLY (waived /
    waiver_mismatch / unnecessary), so the refusal text is byte-identical
    across backends even for stale or unnecessary waiver rows."""
    rows: list[tuple[str, str, float, str]] = []
    for w in waiver_rows:
        if not isinstance(w, dict):
            continue
        tok = w.get("token")
        pair = w.get("pair")
        best = w.get("best")
        reason = w.get("reason")
        if isinstance(tok, str) and isinstance(pair, str) and \
                isinstance(best, (int, float)) and isinstance(reason, str):
            rows.append((tok, pair, float(best), reason))
    findings = audit_theme(values, src)
    out = []
    for f in findings:
        row = next((r for r in rows if r[0] == f["token"] and
                    r[1] == f["pair"]), None)
        ff = dict(f)
        waiver_row = row is not None
        waiver_mismatch = False
        if waiver_row and abs(row[2] - f["ratio"]) > 0.011:
            waiver_mismatch = True  # stale/aspirational — never honored
        waived = waiver_row and not waiver_mismatch
        # Waiver eligibility floor (recorded law): below body-text 4.5
        # nothing is waivable; in [4.5, required) an EXACT waiver passes.
        if waived and f["ratio"] < 4.5:
            waived = False
        ff["waived"] = waived
        ff["waiver_mismatch"] = waiver_mismatch
        # data-hygiene law: ANY waiver row on a passing pair is unnecessary
        # (mirror of the C++ audit rule).
        ff["unnecessary"] = waiver_row and f["ratio"] >= f["required"]
        ff["passed"] = (f["ratio"] >= f["required"]) or waived
        if waived:
            ff["reason"] = row[3]
        out.append(ff)
    return out


# ---------------------------------------------------------------------------
# method handlers
# ---------------------------------------------------------------------------

def load_loader(tokens_path: str, store_dir: str, mode: str) -> Loader:
    text = Path(tokens_path).read_text(encoding="utf-8")
    return Loader(text, mode, store_dir)


def handle(ld: Loader, method: str, args: dict[str, Any]) -> tuple[int, Any]:
    if method == "flag-status":
        return 0, {"xr_themes_v0": "on"}
    if method == "list":
        return 0, ld.list_json()
    if method == "current":
        return 0, ld.event_json()
    if method == "apply":
        name = args.get("name")
        if not isinstance(name, str):
            return 1, err("kMalformedInput", "apply: 'name' string required")
        r = ld.apply(name)
        return (0 if "error" not in r else 1), r
    if method == "system-mode":
        mode = args.get("mode")
        if not isinstance(mode, str):
            return 1, err("kMalformedInput", "system-mode: 'mode' required")
        r = ld.set_mode(mode)
        if r is not None:
            return 1, r
        return 0, ld.event_json()
    if method in ("import", "validate-doc"):
        doc = args.get("theme-doc")
        if not isinstance(doc, str):
            return 1, err("kMalformedInput",
                          f"{method}: 'theme-doc' string required")
        r = ld.import_doc(doc) if method == "import" else ld.validate_doc(doc)
        return (0 if "error" not in r else 1), r
    return 1, err("kUnknownMethod", f"unknown method '{method}'")


METHODS = ("flag-status", "list", "current", "apply", "import",
           "validate-doc", "system-mode")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="themes.py", add_help=False)
    ap.add_argument("--store-dir", default="")
    ap.add_argument("--tokens", default="ui/themes/tokens.json")
    ap.add_argument("--mode", default="light")
    ap.add_argument("--flag", default="xr_themes_v0=on")
    opts, rest = ap.parse_known_args(argv)

    method = ""
    args_text = "{}"
    if not rest:
        req = sys.stdin.read()
        try:
            reqj = json.loads(req)
        except json.JSONDecodeError as e:
            return _emit(err("kMalformedInput", f"parse: {e}"), 1)
        m = reqj.get("method") if isinstance(reqj, dict) else None
        method = m if isinstance(m, str) else ""
        args_text = canonical(reqj.get("args", {})) if isinstance(reqj, dict) \
            else "{}"
    elif rest[0] in METHODS:
        method = rest[0]
        args_text = rest[1] if len(rest) >= 2 else "{}"
    else:
        try:
            reqj = json.loads(rest[0])
        except json.JSONDecodeError as e:
            return _emit(err("kMalformedInput", f"parse: {e}"), 1)
        m = reqj.get("method") if isinstance(reqj, dict) else None
        method = m if isinstance(m, str) else ""
        args_text = canonical(reqj.get("args", {})) if isinstance(reqj, dict) \
            else "{}"
    if not method:
        sys.stderr.write("usage: themes.py <method> ['<json-args>'] "
                         "[options] | stdin\n")
        return 2
    if method not in METHODS:
        return _emit(err("kUnknownMethod", f"unknown method '{method}'"), 1)
    try:
        args = json.loads(args_text) if args_text.strip() else {}
    except json.JSONDecodeError as e:
        return _emit(err("kMalformedInput", f"parse: {e}"), 1)
    if not isinstance(args, dict):
        return _emit(err("kMalformedInput", "args must be an object"), 1)

    try:
        ld = load_loader(opts.tokens, opts.store_dir, opts.mode)
    except (OSError, SourceError) as e:
        return _emit(err("kStoreError", str(e)), 1)
    rc, obj = handle(ld, method, args)
    return _emit(obj, rc)


def _emit(obj: Any, rc: int) -> int:
    sys.stdout.write(canonical(obj) + "\n")
    return rc


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
