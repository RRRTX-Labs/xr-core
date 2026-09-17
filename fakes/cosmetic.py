#!/usr/bin/env python3
"""fakes/cosmetic.py — behavioral fake for the cosmetic host surface (P12-T3).

A BEHAVIORAL FAKE, not a binding. It exists so the Python reference
implementation of the `renderer/cosmetic` host surface can be replayed
byte-for-byte against the compiled C++ host (tools/cosmetic_vectors_check.py),
which is what makes the golden vectors a parity test rather than a
self-consistency test.

Contract laws honored here (fakes/_base.py, docs/contracts/cosmetic-blob-v1.md,
xr-core/renderer/cosmetic/host_protocol.md):
  * deterministic: no clock, no RNG, no environment, no I/O beyond reading the
    request from stdin.
  * deny-default / total: unknown input maps to a refusing result, never an
    exception a caller could read as "apply".
  * typed errors: results are a result object, {"error":"kRejected",...}, or
    {"error":"<ErrorCode>","detail":...}.
  * the refusal vocabulary is CLOSED and mirrors the C++ *ErrorName() tables.

CRITICAL PARITY RULE: this file must agree with the C++ host byte-for-byte on
stdout AND on exit code, on every path including refusals. Two divergences were
found and fixed this way already:
  1. `comment-unterminated` here vs `comment-refused` in selector.cc — the names
     are COPIED from the C++ tables, not chosen here;
  2. the fake dropped zeroed summary fields on a refusal, because json.dumps
     omits nothing but the C++ JsonValue has no notion of an omitted key — so
     every refusal case diverged at once.
Do not "fix" a divergence by loosening one side; fix the side that is wrong.

Stdlib only. Exit: 0 ok-or-typed-rejection, 1 typed error, 2 usage.
"""
from __future__ import annotations

import hashlib
import json
import sys
from typing import Any

from _base import canonical

SCHEMA_ID = "xr-cosmetic-blob-v1"
SCHEMA_VERSION = 1

# --- closed vocabularies, COPIED from the C++ tables -------------------------
# Mirrors SelectorErrorName() in renderer/cosmetic/core/selector.cc. The C++
# table has 25 entries including "ok"; a refusal is never "ok", so the refusal
# set is the 24 that follow it.
PARSER_REASONS = {
    "empty-selector", "selector-too-long", "too-many-compounds",
    "too-many-attr-selectors", "too-many-pseudo-args", "ident-too-long",
    "unbalanced-paren", "unbalanced-bracket", "empty-compound",
    "leading-combinator", "trailing-combinator", "double-combinator",
    "selector-list-refused", "disallowed-char", "bang-important-refused",
    "at-rule-refused", "markup-refused", "url-function-refused",
    "expression-refused", "unknown-pseudo-class", "disallowed-pseudo-arg",
    "universal-with-pseudo", "comment-refused", "escape-sequence-refused",
}
# Mirrors BlobErrorName() in core/blob.cc.
BLOB_REASONS = {
    "malformed-blob", "schema-mismatch", "unknown-field", "scope-mismatch",
    "sha256-mismatch", "too-many-rules", "unknown-refusal-reason",
    "duplicate-rule-id", "rule-rejected", "empty-blob-id",
}
# Mirrors StyleErrorName() in core/style.cc.
STYLE_REASONS = {
    "empty-style-map", "too-many-declarations", "unknown-css-property",
    "empty-style-value", "style-value-too-long", "bang-important-refused",
    "url-function-refused", "non-ascii-control",
}
REFUSAL_REASONS = PARSER_REASONS | BLOB_REASONS | STYLE_REASONS

ACTIONS = {"hide", "collapse", "visibility", "remove", "style"}

# Mirrors DeclarationsForAction() in core/style.cc. A built-in action's
# declarations come from this table, so a rule cannot mix "hide" with its own
# map and get an unbounded declaration set — and the declarations COUNT toward
# RuleBytes, which is why an earlier version of this file reported 22 bytes
# where the host reported 24 for one `hide` rule.
ACTION_DECLARATIONS = {
    "hide": {"display": "none"},
    # uBO's "collapse": out of layout as well as out of paint, so the page does
    # not keep a hole where the ad was.
    "collapse": {"display": "none"},
    # Keeps layout deliberately: used where collapsing would shift content the
    # user is reading.
    "visibility": {"visibility": "hidden"},
    # Removal is a DOM operation, not a style one. Pretending otherwise would
    # put node removal on the CSSOM path.
    "remove": {},
    "style": {},
}
PAGE_MODIFYING = {"remove"}
IDENTITY_CLASSES = {"anonymous", "authenticated", "enterprise"}
TRUST_CLASSES = {"anonymous", "authenticated", "enterprise"}
NAV_CLASSES = {"initial", "cross-document", "same-document"}

# Mirrors kAdmittedProperties in core/style.cc. The negative half is the
# security content: `behavior` and `-moz-binding` execute code, `content`
# generates content (so it is not a hide at all), `position`/`z-index` can
# restack the page over the user's own content, and every URL-taking property
# is a fetch side channel.
ADMITTED_PROPERTIES = {
    "display", "visibility", "opacity", "height", "max-height", "min-height",
    "width", "max-width", "min-width", "overflow", "pointer-events", "clip",
    "clip-path",
}

MAX_RULES = 4096
MAX_SELECTOR_LEN = 512
MAX_STYLE_VALUE_LEN = 256
MAX_DECLARATIONS = 16

# Mirrors ScanDangerousSubstrings() in selector.cc, in the SAME ORDER — refusal
# order is observable, so both sides must report the same reason on the same
# input.
DANGEROUS_SUBSTRINGS = (
    ("expression(", "expression-refused"),
    ("url(", "url-function-refused"),
    ("image-set(", "url-function-refused"),
    ("javascript:", "url-function-refused"),
)

# Mirrors kDegradeTable in core/degrade.cc: condition -> (outcome, page_effect,
# reported, reason). Exposed through degrade-apply so the debug page and the
# tests read the SAME table rather than restating it.
DEGRADE_TABLE = {
    "flag-off": ("inert", "The feature is present and does nothing. No call "
                 "site is active, so the off state is identical to today's "
                 "product.", False, ""),
    "scriptlets-off": ("generic-set-only", "Cosmetic filtering applies; no "
                       "scriptlet runs. The registry is printed as inert on "
                       "the debug page.", True, "scriptlets-off"),
    "blob-invalid": ("drop-blob", "The whole blob is discarded and the page "
                     "renders unstyled for this frame.", True, "blob-invalid"),
    "blob-missing": ("no-work", "No blob for this scope key, so nothing is "
                     "emitted and no observer is installed.", True,
                     "blob-missing"),
    "scope-mismatch": ("drop-blob", "The blob is for another scope and is "
                       "discarded rather than applied.", True,
                       "scope-mismatch"),
    "rule-unparsable": ("drop-blob", "One unparsable rule rejects the whole "
                        "blob. The alternative — dropping the bad rule and "
                        "applying the rest — would let a list author learn "
                        "which payloads survived, which is a probing "
                        "channel.", True, "rule-unparsable"),
    "rule-unknown-pseudo": ("drop-blob", "A rule uses a pseudo this engine "
                            "does not implement; the blob is discarded rather "
                            "than partially applied.", True,
                            "rule-unknown-pseudo"),
    "rule-too-expensive": ("drop-rule", "The single rule is dropped and the "
                           "rest apply. This is the ONLY condition that drops "
                           "one rule, because here the rule is valid and the "
                           "cost is a property of the page.", True,
                           "rule-too-expensive"),
    "shields-down": ("generic-set-only", "Shields are down for this site, so "
                       "only the generic set applies. The generic set is not "
                       "exception-able at the policy level, because a "
                       "shields-down must not leave the page half-styled.",
                       True, "shields-down"),
    "empty-rule-set": ("no-work", "No rules, no work: nothing is emitted and "
                       "no observer is installed.", True, "empty-rule-set"),
    "dom-mutation-storm": ("throttle", "Observer callbacks are throttled for "
                           "this frame rather than dropped, so a busy page "
                           "still converges.", True, "dom-mutation-storm"),
    "engine-unavailable": ("page-unstyled", "The page renders unstyled. This "
                             "is a deliberate divergence from the network "
                             "layer: refusing to hide an ad must never refuse "
                             "to render a page.", True, "engine-unavailable"),
    "main-world-required": ("inert", "The rule needs main-world execution, "
                            "which this phase does not provide.", True,
                            "main-world-required"),
    "generic-set-only": ("generic-set-only", "Only the generic set applies.",
                         True, "generic-set-only"),
}


# --- frame helpers -----------------------------------------------------------

def _rejected(reason: str, detail: str = "") -> dict[str, Any]:
    """A typed rejection (exit 0), matching the host's Reject()."""
    out: dict[str, Any] = {"error": "kRejected", "reason": reason}
    if detail:
        out["detail"] = detail
    return out


def _error(code: str, detail: str) -> dict[str, Any]:
    """A typed error (exit 1), matching the host's Error()."""
    return {"detail": detail, "error": code}


def _contains_no_case(haystack: str, needle: str) -> bool:
    return needle.lower() in haystack.lower()


def validate_selector(text: str) -> str | None:
    """Return a refusal reason, or None if the selector is admissible.

    A deliberately shallow model of core/selector.cc: enough to agree on the
    golden vectors' refusal reasons without reimplementing the grammar. Where
    the C++ parser is stricter than this function the vectors record the C++
    reason, and this function must be extended — never the vector loosened.
    """
    if not text:
        return "empty-selector"
    if len(text) > MAX_SELECTOR_LEN:
        return "selector-too-long"
    # Refusal ORDER mirrors the C++ walk: specific before generic.
    if text.lstrip().startswith("@"):
        return "at-rule-refused"
    for needle, reason in DANGEROUS_SUBSTRINGS:
        if _contains_no_case(text, needle):
            return reason
    if "<!--" in text or "-->" in text or "<" in text or ">" in text and \
            ">" not in text.replace("->", ""):
        # Markup scan. `>` alone is a child combinator and is fine; the test is
        # for a tag or a comment.
        if "<!--" in text or "-->" in text or "<" in text:
            return "markup-refused"
    if "!" in text:
        return "bang-important-refused"
    if text.count("(") != text.count(")"):
        return "unbalanced-paren"
    if text.count("[") != text.count("]"):
        return "unbalanced-bracket"
    if text.endswith(","):
        return "selector-list-refused"
    if "\\" in text:
        return "escape-sequence-refused"
    # `*:has(...)` — a rule that can match the whole document.
    stripped = text.strip()
    if stripped.startswith("*:") or stripped.startswith("*["):
        return "universal-with-pseudo"
    return None


def validate_style(style: Any) -> str | None:
    if not isinstance(style, dict) or not style:
        return "empty-style-map"
    if len(style) > MAX_DECLARATIONS:
        return "too-many-declarations"
    for prop, value in style.items():
        if prop not in ADMITTED_PROPERTIES:
            return "unknown-css-property"
        if not isinstance(value, str) or not value:
            return "empty-style-value"
        if len(value) > MAX_STYLE_VALUE_LEN:
            return "style-value-too-long"
        if "!" in value:
            return "bang-important-refused"
        if "url(" in value.lower():
            return "url-function-refused"
    return None


import re as _re

_TOKEN_RE = _re.compile(r"\s*([>+~])\s*")


def _canonical_compound(compound: str) -> str:
    """Mirror AppendCompound(): ids and classes SORTED, so `.a.b` and `.b.a`
    canonicalize the same. Without this, dedup would depend on the order a list
    author happened to type the classes in."""
    out: list[str] = []
    i = 0
    if i < len(compound) and compound[i] == "*":
        out.append("*")
        i += 1
    tag = ""
    while i < len(compound) and compound[i] not in "#.:[":
        tag += compound[i]
        i += 1
    out.append(tag)
    ids: list[str] = []
    classes: list[str] = []
    rest: list[str] = []
    while i < len(compound):
        c = compound[i]
        if c in "#.":
            j = i + 1
            while j < len(compound) and compound[j] not in "#.:[":
                j += 1
            name = compound[i + 1:j]
            (ids if c == "#" else classes).append(name)
            i = j
        else:
            # An attribute or pseudo: bracket-balanced copy, verbatim.
            depth = 0
            j = i
            while j < len(compound):
                if compound[j] in "([":
                    depth += 1
                elif compound[j] in ")]":
                    depth -= 1
                elif compound[j] in "#.:" and depth == 0:
                    break
                j += 1
            rest.append(compound[i:j])
            i = j
    for name in sorted(ids):
        out.append("#" + name)
    for name in sorted(classes):
        out.append("." + name)
    out.extend(rest)
    return "".join(out)


def canonicalize_selector(text: str) -> str:
    """The dedup key. Mirrors CanonicalizeSelector() in core/keyset.cc:
    compounds joined by " <combinator> ", each compound canonicalized."""
    parts = _TOKEN_RE.split(text.strip())
    # split() with a capture group yields [compound, tok, compound, tok, ...]
    out: list[str] = []
    for i, part in enumerate(parts):
        if i % 2 == 1:
            out.append(f" {part} ")
        elif part:
            out.append(_canonical_compound(part))
    return "".join(out)


def _rule_dedup_key(rule: dict[str, Any]) -> str:
    sel = canonicalize_selector(rule.get("selector", ""))
    action = rule.get("action", "")
    style = rule.get("style") or {}
    effective = style if action == "style" else ACTION_DECLARATIONS.get(action, {})
    decls = ",".join(f"{k}:{effective[k]}" for k in sorted(effective))
    return f"{sel}\x01{action}\x01{decls}"


def compile_key_set(rules: list[Any]) -> tuple[str | None, dict[str, Any]]:
    """Mirror of CompileKeySet(). Returns (reason, result)."""
    if not rules:
        return "empty-key-set", {}
    if len(rules) > MAX_RULES:
        return "too-many-rules", {}
    seen_ids: set[str] = set()
    seen_keys: set[str] = set()
    compiled = 0
    dupes = 0
    total = 0
    for i, rule in enumerate(rules):
        if not isinstance(rule, dict):
            return "rule-rejected", {}
        rid = rule.get("id")
        if not isinstance(rid, str) or not rid:
            return "duplicate-rule-id", {}
        if rid in seen_ids:
            return "duplicate-rule-id", {}
        seen_ids.add(rid)
        sel = rule.get("selector")
        if not isinstance(sel, str):
            return "rule-rejected", {}
        reason = validate_selector(sel)
        if reason:
            return reason, {}
        action = rule.get("action")
        if action not in ACTIONS:
            return "rule-rejected", {}
        own_style = rule.get("style") or {}
        if action == "style":
            sreason = validate_style(own_style)
            if sreason:
                return sreason, {}
        effective_style = own_style if action == "style" \
            else ACTION_DECLARATIONS[action]
        # Dedup BEFORE accounting, matching the C++ walk: a rule that collapses
        # into one already seen costs nothing, because it is not stored. Doing
        # this the other way round made two text variants of one selector cost
        # double.
        key = _rule_dedup_key(rule)
        if key in seen_keys:
            dupes += 1
            continue
        seen_keys.add(key)
        # Byte accounting mirrors RuleBytes(): the fields the blob cache
        # actually stores, counted in characters (sizeof() would include
        # padding and vector capacity, making the number meaningless as a
        # cache bound). The ACTION is not counted; each declaration costs
        # k+v+2 and each exception site s+1.
        canon = canonicalize_selector(sel)
        total += len(rid) + len(canon)
        for k, v in effective_style.items():
            total += len(k) + len(str(v)) + 2
        for site in rule.get("exception_sites") or []:
            total += len(str(site)) + 1
        compiled += 1
    return None, {"compiled_selectors": compiled, "duplicates_removed": dupes,
                  "bytes": total, "rules": rules}


# --- blob envelope -----------------------------------------------------------

def _blob_canonical(blob: dict[str, Any]) -> str:
    return canonical(blob)


def _blob_digest(blob: dict[str, Any]) -> str:
    body = {k: v for k, v in blob.items() if k != "sha256"}
    return hashlib.sha256(_blob_canonical(body).encode()).hexdigest()


def build_blob(args: dict[str, Any]) -> dict[str, Any]:
    blob_id = args.get("blob_id")
    if not isinstance(blob_id, str) or not blob_id:
        return _error("kMalformedInput", "blob_id must be a non-empty string")
    epoch = args.get("generated_epoch")
    if not isinstance(epoch, int) or isinstance(epoch, bool):
        return _error("kMalformedInput", "generated_epoch must be an integer")
    scope = args.get("scope")
    if not isinstance(scope, dict):
        return _error("kMalformedInput", "scope must be an object")
    if not isinstance(scope.get("site"), str) or \
            not isinstance(scope.get("identity_class"), str):
        return _error("kMalformedInput", "scope needs site and identity_class")
    rules = args.get("rules")
    if not isinstance(rules, list):
        return _error("kMalformedInput", "rules must be an array")
    out_rules = []
    for i, r in enumerate(rules):
        if not isinstance(r, dict):
            return _rejected("rule-rejected", f"rule {i} is not an object")
        for field in ("id", "selector", "action"):
            if not isinstance(r.get(field), str):
                return _rejected("rule-rejected",
                                 f"rule {i} needs id, selector and action")
        style = r.get("style")
        if style is not None:
            if not isinstance(style, dict):
                return _rejected("rule-rejected", "style must be an object")
            for v in style.values():
                if not isinstance(v, str):
                    return _rejected("rule-rejected",
                                     "style values must be strings")
        ex = r.get("exception_sites")
        if ex is not None:
            if not isinstance(ex, list):
                return _rejected("rule-rejected",
                                 "exception_sites must be an array")
            for s in ex:
                if not isinstance(s, str):
                    return _rejected("rule-rejected",
                                     "exception_sites must be strings")
        rule_obj: dict[str, Any] = {
            "action": r["action"],
            "enabled": bool(r.get("enabled", True)),
            "id": r["id"],
            "selector": r["selector"],
        }
        # Empty collections are OMITTED, matching the C++ serializer. Emitting
        # them would change the digest, so a blob built here would never
        # verify there.
        if ex:
            rule_obj["exception_sites"] = list(ex)
        if style:
            rule_obj["style"] = dict(style)
        out_rules.append(rule_obj)
    blob: dict[str, Any] = {
        "blob_id": blob_id,
        "generated_epoch": epoch,
        "refusals": list(args.get("refusals") or []),
        "rules": out_rules,
        "schema": SCHEMA_ID,
        "schema_version": SCHEMA_VERSION,
        "scope": {"identity_class": scope["identity_class"],
                  "site": scope["site"]},
    }
    blob["sha256"] = _blob_digest(blob)
    text = _blob_canonical(blob)
    return {"blob": text, "bytes": len(text), "sha256": blob["sha256"]}


def check_blob(args: dict[str, Any]) -> dict[str, Any]:
    raw = args.get("blob")
    if not isinstance(raw, str):
        return _error("kMalformedInput", "blob must be a string")
    try:
        blob = json.loads(raw)
    except json.JSONDecodeError:
        return _rejected("malformed-blob")
    if not isinstance(blob, dict):
        return _rejected("malformed-blob")
    if blob.get("schema") != SCHEMA_ID or \
            blob.get("schema_version") != SCHEMA_VERSION:
        return _rejected("schema-mismatch")
    digest = blob.get("sha256")
    if not isinstance(digest, str) or len(digest) != 64:
        return _rejected("malformed-blob")
    if _blob_digest(blob) != digest:
        return _rejected("sha256-mismatch")
    scope = blob.get("scope")
    if not isinstance(scope, dict):
        return _rejected("malformed-blob")
    frame = args.get("frame_scope")
    if frame is not None:
        if not isinstance(frame, dict) or \
                not isinstance(frame.get("site"), str) or \
                not isinstance(frame.get("identity_class"), str):
            return _error("kMalformedInput",
                          "frame_scope needs site and identity_class")
        if scope != frame:
            return _rejected("scope-mismatch")
    for entry in blob.get("refusals") or []:
        if not isinstance(entry, dict):
            return _rejected("malformed-blob")
        if entry.get("reason") not in REFUSAL_REASONS:
            return _rejected("unknown-refusal-reason")
    rules = blob.get("rules")
    if not isinstance(rules, list):
        return _rejected("malformed-blob")
    reason, ks = compile_key_set(rules)
    if reason:
        return _rejected(reason)
    return {
        "blob_id": blob.get("blob_id", ""),
        "bytes": ks["bytes"],
        "compiled_selectors": ks["compiled_selectors"],
        "duplicates_removed": ks["duplicates_removed"],
        "page_modifying": sum(
            1 for r in rules
            if isinstance(r, dict) and r.get("action") in PAGE_MODIFYING),
        "producer_refusals": len(blob.get("refusals") or []),
        "sha256": digest,
    }


# --- the remaining methods ---------------------------------------------------

def flag_status(cosmetic: bool, scriptlets: bool) -> dict[str, Any]:
    return {
        "scriptlet_registry_state": "active" if scriptlets else "inert: flag off",
        "xr_shield_cosmetic_v1": "on" if cosmetic else "off",
        "xr_shield_scriptlets": "on" if scriptlets else "off",
    }


def selector_parse(args: dict[str, Any]) -> dict[str, Any]:
    sel = args.get("selector")
    if not isinstance(sel, str):
        return _error("kMalformedInput", "selector must be a string")
    reason = validate_selector(sel)
    if reason:
        return _rejected(reason)
    # Compound count: split on combinators, mirroring the AST closely enough
    # for the vectors.
    canon = canonicalize_selector(sel)
    compounds = 1
    for tok in canon.split(" "):
        if tok in (">", "+", "~"):
            compounds += 1
    return {"canonical": canon, "compounds": compounds, "pseudos": []}


def scope_key(args: dict[str, Any]) -> dict[str, Any]:
    for field in ("frame_site", "frame_identity", "url_class"):
        if not isinstance(args.get(field), str):
            return _error("kMalformedInput", f"{field} must be a string")
    trust = args.get("trust", "anonymous")
    if trust not in TRUST_CLASSES:
        return _error("kMalformedInput", "trust is not a known identity class")
    nav = args.get("navigation", "initial")
    if nav not in NAV_CLASSES:
        return _error("kMalformedInput", "navigation is not a known class")
    # The embedder is NEVER an input. Refused, not ignored: silently dropping
    # it would let a caller keep supplying it and believe it worked.
    embedder = args.get("embedder_site")
    if isinstance(embedder, str) and embedder:
        return _rejected("embedder-refused",
                         "the embedder site is not an input to the scope key")
    # Field ORDER and the \x01 separator are part of the format — reordering
    # changes every key, so it is pinned by the golden vectors. Note what is
    # ABSENT: the embedder site, the full document URL, and any timestamp.
    # kInitial and kCrossDocument encode identically ("doc"); only
    # same-document differs, so an SPA push keeps the same key by design.
    sep = "\x01"
    nav_token = "same-doc" if nav == "same-document" else "doc"
    frame = sep.join(["cosmetic-scope-v1", args["frame_site"], trust,
                      args["frame_identity"], args["url_class"], nav_token])
    digest = hashlib.sha256(frame.encode()).hexdigest()
    # The partition is derived from the identity and its TRUST, never from the
    # site: eviction is per-identity, so a blob dropped for identity A must not
    # be reachable from identity B. Note the order — trust FIRST — which the
    # earlier version of this file had backwards.
    partition = hashlib.sha256(
        sep.join([trust, args["frame_identity"]]).encode()).hexdigest()
    return {"hex": digest, "partition": partition}


def key_set(args: dict[str, Any]) -> dict[str, Any]:
    rules = args.get("rules")
    if not isinstance(rules, list):
        return _error("kMalformedInput", "rules must be an array")
    for i, r in enumerate(rules):
        if not isinstance(r, dict):
            return _rejected("rule-rejected", f"rule {i} is not an object")
        for field in ("id", "selector", "action"):
            if not isinstance(r.get(field), str):
                return _rejected("rule-rejected",
                                 f"rule {i} needs id, selector and action")
    reason, ks = compile_key_set(rules)
    if reason:
        return _rejected(reason)
    return {"bytes": ks["bytes"],
            "compiled_selectors": ks["compiled_selectors"],
            "duplicates_removed": ks["duplicates_removed"]}


def degrade_apply(args: dict[str, Any]) -> dict[str, Any]:
    name = args.get("condition")
    if not isinstance(name, str):
        return _error("kMalformedInput", "condition must be a string")
    row = DEGRADE_TABLE.get(name)
    if row is None:
        # Refused rather than defaulted, so a caller cannot ask about a state
        # the table does not cover and get a plausible answer.
        return _rejected("unknown-degrade-condition", name)
    outcome, page_effect, reported, reason = row
    return {"condition": name, "outcome": outcome, "page_effect": page_effect,
            "reason": reason, "reported": reported}


def page_states(cosmetic: bool, rule_count: int) -> dict[str, Any]:
    return {
        "blob_cache_entries": 0,
        # A network-service value this host does not own. Reporting a number
        # would be inventing one.
        "blob_cache_occupancy": "NOT-RUN (network-service side)",
        "cosmetic_enabled": cosmetic,
        "degrade_events": 0,
        "generic_set_applies": cosmetic,
        "keyset_rules": rule_count,
        "observer_installed": bool(cosmetic and rule_count > 0),
        "refused_pseudos": 0,
        "refused_selectors": 0,
    }


def dispatch(method: str, args: dict[str, Any], cosmetic: bool,
             scriptlets: bool) -> tuple[dict[str, Any], int]:
    """Returns (result, exit_code). Mirrors the host's Dispatch()."""
    if method == "flag-status":
        return flag_status(cosmetic, scriptlets), 0
    if method == "selector-parse":
        return selector_parse(args), 0
    if method == "scope-key":
        return scope_key(args), 0
    if method == "blob-build":
        return build_blob(args), 0
    if method == "blob-check":
        return check_blob(args), 0
    if method == "key-set":
        return key_set(args), 0
    if method == "degrade-apply":
        return degrade_apply(args), 0
    if method == "page-states":
        rules = args.get("keyset_rules", 0)
        if not isinstance(rules, int) or isinstance(rules, bool) or rules < 0:
            rules = 0
        return page_states(cosmetic, rules), 0
    # Unknown method is a typed error, never a silent no-op.
    return _error("kUnknownMethod", method), 1


def main(argv: list[str]) -> int:
    cosmetic = False   # xr_shield_cosmetic_v1, default OFF
    scriptlets = False  # xr_shield_scriptlets, default OFF
    rest: list[str] = []
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--flag" and i + 1 < len(argv):
            kv = argv[i + 1]
            i += 2
            if "=" not in kv:
                print("usage error: --flag k=v", file=sys.stderr)
                return 2
            k, v = kv.split("=", 1)
            on = v in ("on", "true")
            if k == "xr_shield_cosmetic_v1":
                cosmetic = on
            elif k == "xr_shield_scriptlets":
                scriptlets = on
            else:
                print(f"usage error: unknown flag {k}", file=sys.stderr)
                return 2
        elif a.startswith("--"):
            print(f"usage error: unknown option {a}", file=sys.stderr)
            return 2
        else:
            rest.append(a)
            i += 1
    if len(rest) > 1:
        print("usage: cosmetic [--flag k=v ...] [request.json]",
              file=sys.stderr)
        return 2
    raw = rest[0] if rest else sys.stdin.read()
    try:
        frame = json.loads(raw)
    except json.JSONDecodeError:
        print(canonical(_error("kMalformedInput",
                               "request must be a JSON object")))
        return 1
    if not isinstance(frame, dict):
        print(canonical(_error("kMalformedInput",
                               "request must be a JSON object")))
        return 1
    method = frame.get("method")
    if not isinstance(method, str):
        print(canonical(_error("kMalformedInput",
                               "method must be a string")))
        return 1
    # The parity frame is {"args":{…},"method":"…"}; a flat request is accepted
    # too, and both resolve to the same object so the two cannot disagree.
    args = frame.get("args")
    if not isinstance(args, dict):
        args = frame
    result, code = dispatch(method, args, cosmetic, scriptlets)
    print(canonical(result))
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
