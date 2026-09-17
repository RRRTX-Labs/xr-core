#!/usr/bin/env python3
"""fakes/cosmetic.py — behavioral fake for the cosmetic-blob-v1 contract (P12-T3).

A BEHAVIORAL FAKE, not a binding. It exists so the Python reference
implementation of `cosmetic-blob-v1` can be replayed byte-for-byte against the
compiled C++ host (see tools/cosmetic_vectors_check.py), which is what makes
the golden vectors a parity test rather than a self-consistency test.

Contract laws honored here (fakes/_base.py, docs/contracts/cosmetic-blob-v1.md):
  * deterministic: no clock, no RNG, no environment, no I/O beyond reading the
    blob from stdin.
  * deny-default / total: unknown input maps to a refusing result, never an
    exception a caller could read as "apply".
  * typed errors: results are {"ok": ...} or {"error": "<ErrorCode>"}.
  * the refusal vocabulary is CLOSED and mirrors SelectorErrorName() in
    xr-core/renderer/cosmetic/core/selector.h plus the blob-level reasons
    listed in the contract. A reason outside that set is itself a refusal.

CRITICAL PARITY RULE: this file must agree with the C++ core byte-for-byte on
stdout and on exit code. Where the C++ parser has a refusal this file lacks (or
vice versa) the parity checker reddens — that is the point. Do not "fix" a
divergence by loosening one side; fix the side that is wrong.

Stdlib only. Exit: 0 ok-or-refused (a typed refusal is a normal result),
1 malformed input the caller must not interpret, 2 usage.
"""
from __future__ import annotations

import json
import sys
from typing import Any

from _base import canonical, err, ok

SCHEMA_ID = "xr-cosmetic-blob-v1"
SCHEMA_VERSION = 1

# The closed refusal vocabulary. Parser reasons mirror SelectorErrorName() in
# renderer/cosmetic/core/selector.h; blob reasons are the contract's own.
# Keeping this as an explicit set (not a prefix match, not a substring test) is
# what makes "unknown reason" detectable rather than silently accepted.
# The names are copied from SelectorErrorName() in selector.cc, not chosen
# here. `comment-refused` (not "comment-unterminated") was a real divergence
# this file shipped with until the sets were diffed mechanically; the vectors
# are what make such a rename a red test instead of a quiet drift.
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
BLOB_REASONS = {
    "schema-mismatch", "scope-mismatch", "sha256-mismatch", "rule-too-large",
    "too-many-rules", "duplicate-rule-id", "unknown-refusal-reason",
    "no-executable-content",
}
REFUSAL_REASONS = PARSER_REASONS | BLOB_REASONS

ACTIONS = {"hide", "remove", "style"}
IDENTITY_CLASSES = {"anonymous", "authenticated", "enterprise"}

MAX_RULES = 4096
MAX_SELECTOR_LEN = 512
MAX_STYLE_VALUE_LEN = 256

# Substrings that must never appear in a selector. Mirrors
# ScanDangerousSubstrings() in selector.cc; kept in the same ORDER so a
# divergence reports the same typed reason on both sides.
DANGEROUS_SUBSTRINGS = (
    ("expression(", "expression-refused"),
    ("url(", "url-function-refused"),
    ("image-set(", "url-function-refused"),
    ("javascript:", "url-function-refused"),
)


def _contains_no_case(haystack: str, needle: str) -> bool:
    return needle in haystack.lower()


def validate_selector(text: str) -> str | None:
    """Return a refusal reason, or None when the selector is acceptable.

    This is a STRUCTURAL mirror of the C++ parser, not a reimplementation of
    its grammar: it checks the refusals that matter for parity (the security-
    relevant shapes and the bounds) and reports the same typed reason. A
    selector this function accepts but the C++ parser refuses is a parity
    failure the vectors catch, which is the intended detection path.
    """
    if not text or not text.strip():
        return "empty-selector"
    if len(text) > MAX_SELECTOR_LEN:
        return "selector-too-long"
    low = text.lower()
    if "!important" in low:
        return "bang-important-refused"
    # Markup escape: a '<' that starts a tag or an HTML comment. A blanket
    # '<'/'>' refusal would be wrong — '>' is the child combinator.
    if "<!" in text:
        return "markup-refused"
    for i, ch in enumerate(text):
        if ch == "<" and i + 1 < len(text) and (
                text[i + 1].isalpha() or text[i + 1] in "/?"):
            return "markup-refused"
    # '@' before the url() checks: `@import url(x)` is an at-rule, and the
    # typed reason is what sends an operator to the right part of the spec.
    if "@" in text:
        return "at-rule-refused"
    for needle, reason in DANGEROUS_SUBSTRINGS:
        if _contains_no_case(text, needle):
            return reason
    if "\\" in text:
        return "escape-sequence-refused"
    if "/*" in text or "*/" in text:
        return "comment-refused"
    if "," in text:
        # One rule, one selector: a list is refused rather than split, so a
        # rule can never widen its own scope after compilation.
        return "selector-list-refused"
    if "!" in text:
        return "disallowed-char"
    if text.count("[") != text.count("]"):
        return "unbalanced-bracket"
    if text.count("(") != text.count(")"):
        return "unbalanced-paren"
    if ">>" in text:
        return "double-combinator"
    stripped = text.strip()
    if stripped[0] in ">+~":
        return "leading-combinator"
    if stripped[-1] in ">+~":
        return "trailing-combinator"
    # `*:has(...)` selects the whole document: a page-wide mutation channel.
    if stripped.startswith("*:") and "(" in stripped:
        return "universal-with-pseudo"
    return None


def validate_rule(rule: Any, index: int, seen_ids: set[str]) -> str | None:
    if not isinstance(rule, dict):
        return "rule-too-large"
    rid = rule.get("id")
    if not isinstance(rid, str) or not rid:
        return "duplicate-rule-id"
    if rid in seen_ids:
        return "duplicate-rule-id"
    seen_ids.add(rid)
    action = rule.get("action")
    if action not in ACTIONS:
        return "unknown-refusal-reason"
    selector = rule.get("selector")
    if not isinstance(selector, str):
        return "empty-selector"
    reason = validate_selector(selector)
    if reason:
        return reason
    if action == "style":
        style = rule.get("style")
        if not isinstance(style, dict) or not style:
            return "no-executable-content"
        for prop, value in style.items():
            if not isinstance(prop, str) or not isinstance(value, str):
                return "no-executable-content"
            if len(value) > MAX_STYLE_VALUE_LEN:
                return "rule-too-large"
            if "!" in value or _contains_no_case(value, "url("):
                # No declaration smuggling through the style map.
                return "no-executable-content"
    for site in rule.get("exception_sites", []) or []:
        if not isinstance(site, str) or not site:
            return "scope-mismatch"
    return None


def _refused(reason: str, rule_index: int) -> dict[str, Any]:
    """A refusal, in the SAME shape as an acceptance.

    The C++ host always emits all six summary fields (the C++ JsonValue has no
    notion of an omitted key, and the fake's json.dumps would otherwise drop
    the ones set to a falsy default). Byte parity is the contract, so the fake
    must emit the zeroed fields too — otherwise every refusal case diverges and
    the parity checker reddens on 40 cases at once.
    """
    return ok({
        "applied": 0, "enabled": 0, "refused": reason,
        "rule_index": rule_index, "producer_refusals": 0, "page_modifying": 0,
    })


def validate_blob(blob: Any, expected_scope: dict[str, str] | None = None
                  ) -> dict[str, Any]:
    """Validate a whole blob. Returns an ok() with a summary or a refusal.

    One bad rule rejects the WHOLE blob. Dropping the bad rule and applying the
    rest would let a list author learn which payloads survived, which is a
    probing channel; the cost is coverage, and the contract records the choice.
    """
    if not isinstance(blob, dict):
        return err("kMalformedInput")
    if blob.get("schema") != SCHEMA_ID:
        return _refused("schema-mismatch", -1)
    if blob.get("schema_version") != SCHEMA_VERSION:
        return _refused("schema-mismatch", -1)
    scope = blob.get("scope")
    if not isinstance(scope, dict):
        return err("kMalformedInput")
    if scope.get("identity_class") not in IDENTITY_CLASSES:
        return _refused("scope-mismatch", -1)
    if not isinstance(scope.get("site"), str) or not scope.get("site"):
        return _refused("scope-mismatch", -1)
    if expected_scope is not None and scope != expected_scope:
        # A blob for one site must not apply in another. The embedder is not
        # consulted: see scope_key.h.
        return _refused("scope-mismatch", -1)

    rules = blob.get("rules")
    if not isinstance(rules, list):
        return err("kMalformedInput")
    if len(rules) > MAX_RULES:
        return _refused("too-many-rules", -1)

    # The producer's own refusals must name reasons we recognize. An unknown
    # reason means producer and consumer disagree about the contract, and
    # guessing is how a validator becomes permissive.
    for entry in blob.get("refusals") or []:
        if not isinstance(entry, dict):
            return err("kMalformedInput")
        if entry.get("reason") not in REFUSAL_REASONS:
            return _refused("unknown-refusal-reason",
                            int(entry.get("rule_index", -1)))

    seen_ids: set[str] = set()
    for i, rule in enumerate(rules):
        reason = validate_rule(rule, i, seen_ids)
        if reason:
            return _refused(reason, i)

    enabled = sum(1 for r in rules
                  if isinstance(r, dict) and r.get("enabled", True))
    return ok({
        "applied": len(rules),
        "enabled": enabled,
        "refused": None,
        "rule_index": -1,
        "producer_refusals": len(blob.get("refusals") or []),
        "page_modifying": sum(
            1 for r in rules
            if isinstance(r, dict) and r.get("action") == "remove"),
    })


def main(argv: list[str]) -> int:
    if len(argv) > 2 or (len(argv) == 2 and argv[1] not in ("--scope",)):
        print("usage: cosmetic [--scope] < blob.json", file=sys.stderr)
        return 2
    raw = sys.stdin.read()
    try:
        blob = json.loads(raw)
    except json.JSONDecodeError:
        print(canonical(err("kMalformedInput")))
        return 1
    expected = None
    if len(argv) == 2 and argv[1] == "--scope":
        # The frame's own scope, supplied by the caller. Never derived from the
        # blob itself, and never from an embedder.
        expected = {"site": "example.test", "identity_class": "anonymous"}
    print(canonical(validate_blob(blob, expected)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
