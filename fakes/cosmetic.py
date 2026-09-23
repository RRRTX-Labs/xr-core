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
    "url-function-refused", "control-char-refused",
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

# Mirrors kPseudoTable in core/pseudo.cc: name -> (admitted, takes_arg,
# arg_is_selector). Arity is enforced from THIS table, not from the
# parser's guesses: a functional pseudo used bare, or a bare one used
# functionally, is a malformed rule. Without it the fake accepted
# `:remove()` with an argument and `:has` without one.
PSEUDO_TABLE = {
    "has-text": (True, True, False),
    "matches-attr": (False, True, False),
    "matches-css": (False, True, False),
    "matches-css-before": (False, True, False),
    "matches-css-after": (False, True, False),
    "matches-path": (True, True, False),
    "min-text-length": (True, True, False),
    "upward": (True, True, False),
    "xpath": (False, True, False),
    "has": (True, True, True),
    "not": (True, True, True),
    "is": (True, True, True),
    "nth-ancestor": (False, True, False),
    "if": (False, True, False),
    "if-not": (False, True, False),
    "remove": (True, False, False),
}

# Mirrors CanonicalizePseudoAlias() in core/pseudo.cc.
# Mirrors IsAdmittedNativePseudo() in core/pseudo.cc VERBATIM (extracted, not
# retyped). The engine passes ANY unknown pseudo-class through as
# AnythingElse, so "the engine accepts it" proves nothing — this
# allowlist is what makes the admitted set ours. A pseudo in this set
# takes no argument unless the procedural table says otherwise.
# Native pseudos that take an argument in CSS.
_NATIVE_TAKES_ARG = {"nth-child", "nth-last-child", "nth-of-type",
                     "nth-last-of-type", "lang", "not", "is", "has"}

NATIVE_PSEUDOS = {
    "first-child",
    "last-child",
    "only-child",
    "first-of-type",
    "last-of-type",
    "only-of-type",
    "nth-child",
    "nth-last-child",
    "nth-of-type",
    "nth-last-of-type",
    "empty",
    "root",
    "checked",
    "disabled",
    "enabled",
    "link",
    "visited",
    "hover",
    "focus",
    "target",
    "lang",
    "not",
    "is",
    "has",
}

PSEUDO_ALIASES = {
}

MAX_RULES = 4096
MAX_SELECTOR_LEN = 512   # kMaxSelectorLen
MAX_COMPOUNDS = 24       # kMaxCompounds — sequences separated by combinators
MAX_ATTR_SELECTORS = 6   # kMaxAttrSelectors
MAX_IDENT_LEN = 128      # kMaxIdentLen
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

# Mirrors kDegradeTable in core/degrade.cc VERBATIM — these strings were
# extracted from the compiled host, not retyped, because a page_effect
# that drifts from the table is a debug page that lies about what the
# user is looking at. condition -> (outcome, page_effect, reported,
# reason). Exposed through degrade-apply so the debug page and the
# tests read the SAME table rather than restating it.
DEGRADE_TABLE = {
    'flag-off': (
        'inert',
        'The page renders exactly as it does today: no style is emitted, no observer is installed, no call site is active.',
        False, ''),
    'scriptlets-off': (
        'inert',
        "Cosmetic hiding still applies; scriptlets do nothing. The debug page reports the registry as 'inert: flag off' verbatim.",
        False, ''),
    'blob-invalid': (
        'drop-blob',
        'The whole blob is dropped and the page renders unstyled. NOT the half-styled state that dropping only the bad rule would produce.',
        True, 'blob-invalid'),
    'blob-missing': (
        'generic-set-only',
        'The always-on generic hide set still applies; site-specific rules do not. The page renders with only the conservative junk selectors hidden.',
        True, 'blob-missing'),
    'scope-mismatch': (
        'drop-blob',
        "The blob is not applied at all. A blob for site A in a frame on site B would leak one identity's rule set into another, so this is a hard drop and not a partial apply.",
        True, 'scope-mismatch'),
    'rule-unparsable': (
        'drop-blob',
        'One unparsable rule rejects the whole blob. The alternative — dropping the bad rule and applying the rest — would let a list author learn which payloads survived, which is a probing channel.',
        True, 'rule-unparsable'),
    'rule-unknown-pseudo': (
        'drop-blob',
        'As above. A pseudo-class outside the allowlist means the producer and the renderer disagree about the contract.',
        True, 'unknown-pseudo-class'),
    'rule-too-expensive': (
        'drop-rule',
        'THIS is the one condition that drops a single rule rather than the blob, because the rule is valid and the cost is a property of the page rather than of the rule. The remaining rules still apply.',
        True, 'rule-too-expensive'),
    'shields-down': (
        'generic-set-only',
        'Site-specific rules are off. The generic set STILL applies, because a shields-down must not leave the page half-styled — the user asked for less filtering, not for a broken layout.',
        False, ''),
    'empty-rule-set': (
        'no-work',
        "Zero rules means zero emitted style and no mutation observer installed. This is what makes the default-off state byte-identical to today's product, and it is asserted rather than assumed.",
        False, ''),
    'dom-mutation-storm': (
        'throttle',
        'The page keeps rendering; cosmetic matching slows to the budget. A mutation storm must never make the page jank, so the cosmetic work yields rather than competing with layout.',
        True, 'dom-mutation-storm'),
    'engine-unavailable': (
        'page-unstyled',
        "The page renders unstyled. Fail-OPEN is correct here: refusing to hide an ad must never refuse to render a page. This is the deliberate opposite of the network layer's fail-CLOSED rule.",
        True, 'engine-unavailable'),
    'main-world-required': (
        'drop-rule',
        'A scriptlet that would need a main-world handle is refused outright — the handle is never set — and the rest of the registry still works.',
        True, 'main-world-required'),
    'generic-set-only': (
        'generic-set-only',
        'Only the conservative generic set applies. This is the floor: the least the feature ever does when everything else is unavailable.',
        False, ''),
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
    # Refusal ORDER mirrors the C++ walk exactly, because refusal order is
    # observable: the same input must produce the same typed reason on both
    # backends or the parity checker reddens.
    if text.lstrip().startswith("@"):
        return "at-rule-refused"
    for needle, reason in DANGEROUS_SUBSTRINGS:
        if _contains_no_case(text, needle):
            return reason
    if "<!--" in text or "-->" in text or "<" in text:
        return "markup-refused"
    # The literal `!important`, not any `!`. ScanDangerousSubstrings() tests for
    # the substring "!important"; a bare `!` falls through to the walk, which
    # reports kDisallowedChar because `!` is not in the grammar. Refusing every
    # `!` here would report the wrong reason for `.a!` and would also refuse a
    # selector the C++ refuses for a different reason — same verdict, wrong
    # diagnosis, and a red vector.
    if _contains_no_case(text, "!important"):
        return "bang-important-refused"
    # One rule, one selector: a comma-separated LIST is refused rather than
    # split, so a rule can never widen its own scope after compilation. This
    # refuses `:is(.a,.b)` too, deliberately — the alternative is a selector
    # whose scope depends on how the engine splits it.
    if "," in text:
        return "selector-list-refused"
    if "\\" in text:
        return "escape-sequence-refused"
    # A structural break the walk cannot continue through. The C++ parser
    # reports kDisallowedChar here rather than a balance error, because it hits
    # the offending character while walking and never gets to count: `div:(.ad`
    # is a pseudo-class with no name, and `*.bogus(` is a `(` with no pseudo
    # before it. `div:has(` DOES have a name, so it falls through to the balance
    # check and reports unbalanced-paren. Reporting "unbalanced" for all three
    # would describe the symptom instead of where the walk stopped.
    if _has_structural_break(text):
        return "disallowed-char"
    if text.count("(") != text.count(")"):
        return "unbalanced-paren"
    if text.count("[") != text.count("]"):
        return "unbalanced-bracket"
    # Characters outside the grammar, mirroring the C++ walk rather than
    # guessing a set. IsIdentChar() admits alnum, `_`, `-` and bytes >= 0x80;
    # everything else must be a structural character the grammar uses in the
    # position it appears. `$`, `^`, `*` and `|` are attribute OPERATORS and are
    # only legal immediately before `=` — which is why `a[href$="x"]` parses and
    # `a[href$]` does not. Getting this wrong in either direction is a parity
    # failure, and a too-wide set is the worse one: it would accept a selector
    # the renderer refuses at document-start.
    for idx, ch in enumerate(text):
        if ch.isalnum() or ord(ch) >= 0x80 or ch in _PLAIN_STRUCTURAL:
            continue
        if ch == "\\":
            continue  # refused earlier by the escape check
        if ch in "$^*|":
            # An attribute operator: legal only immediately before `=`.
            if idx + 1 < len(text) and text[idx + 1] == "=":
                continue
            return "disallowed-char"
        if ch in "?!&;%":
            # Legal inside a bracketed attribute value or a pseudo argument,
            # where the walk has already accepted the opener and reads the value
            # verbatim — the C++ accepts `:has-text(a?b)` and
            # `:matches-path(/a?b&c)` for exactly this reason. The test is
            # whether an opener is still OPEN at this index.
            before = text[:idx]
            if before.count("(") > before.count(")") or \
                    before.count("[") > before.count("]"):
                continue
            return "disallowed-char"
        return "disallowed-char"
    # Combinator structure. The C++ walk keeps ONE flag meaning "a combinator
    # has been recorded for this position", so two in a row, one at the start,
    # or one at the end are each a distinct refusal.
    toks = [t for t in _TOKEN_RE.split(text.strip())]
    compounds_list = [t for t in toks[0::2]]
    combinators = [t for t in toks[1::2]]
    if any(c in (">", "+", "~") for c in combinators[:1]) and not compounds_list[0]:
        return "leading-combinator"
    if compounds_list and not compounds_list[-1] and combinators:
        return "trailing-combinator"
    for i, comp in enumerate(compounds_list):
        if not comp and i not in (0, len(compounds_list) - 1):
            return "double-combinator"
    # The structural bounds, in the order the C++ walk hits them.
    stripped0 = text.strip()
    compounds = len([t for t in _TOKEN_RE.split(stripped0) if t and
                     t not in (">", "+", "~")])
    if compounds > MAX_COMPOUNDS:
        return "too-many-compounds"
    if text.count("[") > MAX_ATTR_SELECTORS:
        return "too-many-attr-selectors"
    for m in _IDENT_RE.finditer(text):
        if len(m.group(0)) > MAX_IDENT_LEN:
            return "ident-too-long"
    if stripped0.startswith("*:") or stripped0.startswith("*["):
        return "universal-with-pseudo"
    # Admission and arity, from the table. A pseudo outside the admitted set is
    # refused with the parser's own vocabulary; the table's kRefused* reasons
    # are the WHY, recorded in the blob's refusal note rather than here.
    depth = 0
    i = 0
    while i < len(text):
        c = text[i]
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif c == ":" and depth == 0:
            j = i + 1
            while j < len(text) and (text[j].isalnum() or text[j] in "-_"):
                j += 1
            # Lowercased before lookup, matching the parser: it folds the
            # pseudo name to lower case so `DIV:HAS(.a)` is the same rule as
            # `div:has(.a)`, while the TAG keeps its case (CSS tag names are
            # case-insensitive in HTML but the canonical form preserves what
            # was written). Looking up the name as-written made every
            # upper-case pseudo an "unknown pseudo class".
            raw_name = text[i + 1:j].lower()
            name = PSEUDO_ALIASES.get(raw_name, raw_name)
            # An EMPTY argument list is not an argument, matching the C++ walk:
            # `:remove()` is the documented spelling and takes none, so `:has()`
            # is refused (it takes one) while `:remove()` is not.
            has_arg = False
            if j < len(text) and text[j] == "(":
                close = text.find(")", j)
                has_arg = close != -1 and text[j + 1:close].strip() != ""
            info = PSEUDO_TABLE.get(name)
            if info is None:
                # Not in the procedural table: it must be in the native CSS
                # allowlist, or it is refused. Two closed tables decide, and
                # anything in neither is refused rather than forwarded.
                if name not in NATIVE_PSEUDOS:
                    return "unknown-pseudo-class"
                # A native pseudo takes an argument only where CSS says so
                # (the nth-* family and :lang). The rest are bare.
                takes_arg = name in _NATIVE_TAKES_ARG
                if takes_arg != has_arg:
                    return "disallowed-pseudo-arg"
            else:
                if not info[0]:
                    return "unknown-pseudo-class"
                _, takes_arg, _ = info
                if takes_arg and not has_arg:
                    return "disallowed-pseudo-arg"
                if not takes_arg and has_arg:
                    return "disallowed-pseudo-arg"
            i = j
            continue
        i += 1
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
# A pseudo-class name: ':' followed by an identifier. Bracket-aware scanning is
# not needed here because an attribute selector's value cannot contain a bare
# ':' that this would mis-read — and if one ever can, the vectors will say so.
_PSEUDO_RE = _re.compile(r":(-?[A-Za-z_][A-Za-z0-9_-]*)")
# An identifier: a tag, class or id name. Bounded by kMaxIdentLen.
_IDENT_RE = _re.compile(r"[A-Za-z_][A-Za-z0-9_-]*")
# The structural characters the grammar admits, as a set literal so the quote
# and backslash members do not need escaping inside a string.
# `:` for pseudo-classes and `,` for the one-selector check (which has already
# run by this point, but a comma inside an attribute value is legitimate and
# must not be reported as a bad character).
# `/` appears inside `:matches-path(/blog)`, `?` and `&` inside query strings a
# path matcher may carry, `;` inside an attribute value, and `!` is refused
# earlier by the bang check so listing it here is harmless.
# Structural characters legal anywhere they appear. Built as a set so the
# quote member needs no escaping inside a literal.
# `-` and `_` are IsIdentChar members and are already accepted by the
# `isalnum()` test above for ASCII, but `-` is NOT alnum, so it must be listed.
_PLAIN_STRUCTURAL = set("-_#.[]()>+~~*:,/= \t") | {'"'}



def _has_structural_break(text: str) -> bool:
    """True where the C++ walk stops with kDisallowedChar.

    Mirrors the cases the parser cannot continue through: a pseudo-class or
    attribute selector that opens and never gets a name/closer, and an
    unterminated bracket. Deliberately narrow — anything the walk CAN continue
    through is left to the balance and bounds checks below, so this does not
    become a second parser that disagrees with the first.
    """
    i = 0
    depth_paren = 0
    depth_bracket = 0
    while i < len(text):
        c = text[i]
        if c == "(":
            # A functional pseudo must precede it: an identifier that starts at
            # a `:`. `div:has(` qualifies and falls through to the balance
            # check; `*.bogus(` does not — `bogus` is a class-less identifier
            # with no colon, so the walk has nowhere to put the `(` and reports
            # kDisallowedChar.
            j = i - 1
            name_end = j
            while j >= 0 and (text[j].isalnum() or text[j] in "-_"):
                j -= 1
            # The name must be NON-EMPTY: `div:(.ad` has a colon but nothing
            # between it and the paren, which is a pseudo-class with no name.
            if j < 0 or text[j] != ":" or name_end == j:
                return True
            depth_paren += 1
        elif c == ")":
            depth_paren -= 1
            if depth_paren < 0:
                return True
        elif c == ":":
            # A pseudo-class with no name. `div:` ends the string there and
            # `div:5` has a digit where an identifier must start; the C++ walk
            # calls ReadIdent, gets nothing, and reports kDisallowedChar rather
            # than an unknown pseudo — there is no name to look up.
            j = i + 1
            if j >= len(text) or not (text[j].isalpha() or text[j] in "-_"):
                return True
        elif c == ".":
            # An empty class name. `div..ad` has two dots in a row, so the
            # second one starts a class with no name; ReadIdent returns empty
            # and the walk reports kDisallowedChar.
            j = i + 1
            if j >= len(text) or not (text[j].isalnum() or text[j] in "-_"):
                return True
        elif c == "[":
            # An attribute selector whose NAME is missing or malformed is a bad
            # character — `[.ad` opens a bracket and immediately hits a `.`,
            # which cannot start an attribute name. One whose name is fine but
            # which never closes is an unbalanced bracket (`div[`, `div[abc`),
            # and the balance check below reports it. Telling these apart is
            # the difference between "you wrote a character that is not in the
            # grammar" and "you forgot to close a bracket".
            j = i + 1
            # End of input right after `[` is an unclosed bracket, not a bad
            # character: there is nothing there to be one. A character that
            # cannot start an attribute name (`[.ad`) IS a bad character.
            if j < len(text) and not (text[j].isalnum() or
                                      text[j] in "-_"):
                return True
            depth_bracket += 1
        elif c == "]":
            depth_bracket -= 1
            if depth_bracket < 0:
                return True
        i += 1
    # An unclosed bracket is NOT a structural break: the C++ walk reads the
    # attribute name, finds no `]`, and reports kUnbalancedBracket. Returning
    # True here made `div[` and `div[abc` report disallowed-char instead. Only
    # a MALFORMED opener (a `[` followed by something that cannot start a name)
    # is a bad character, and that is caught inside the loop above.
    return False



def _split_combinators(text: str) -> tuple[list[str], list[str]]:
    """Split a selector into compounds and the combinators between them.

    A bare space IS the descendant combinator, so it produces an empty token on
    the left-hand side of nothing — `_TOKEN_RE.split` alone would swallow it and
    report one compound where the C++ parser sees two. Bracket depth is tracked
    so a space inside `:has-text(a b)` is not mistaken for a combinator.
    """
    compounds: list[str] = []
    combinators: list[str] = []
    cur = ""
    depth = 0
    i = 0
    while i < len(text):
        c = text[i]
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        if depth == 0:
            if c in ">+~":
                compounds.append(cur)
                combinators.append(c)
                cur = ""
                i += 1
                while i < len(text) and text[i].isspace():
                    i += 1
                continue
            if c.isspace():
                j = i
                while j < len(text) and text[j].isspace():
                    j += 1
                # Whitespace AROUND an explicit combinator belongs to that
                # combinator, not to a descendant one: in `div > .ad` the spaces
                # either side of `>` are separators, and the C++ parser records
                # a single kChild. Emitting a descendant token for the leading
                # space produced `div    > .ad`. Trailing whitespace with
                # nothing after it is likewise not a combinator.
                if j < len(text) and text[j] not in ">+~":
                    compounds.append(cur)
                    combinators.append("")  # the descendant combinator
                    cur = ""
                    i = j
                    continue
                i = j
                continue
        cur += c
        i += 1
    compounds.append(cur)
    return compounds, combinators



def _canonical_attr(piece: str) -> str:
    """Canonical form of an attribute selector or a pseudo, verbatim except for
    the quoting rule AppendCompound() applies.

    AppendCompound() emits `[name OP "value"]` — it ALWAYS quotes a value when
    the operator is not kExists, and appends ` i` for the case-insensitive flag.
    A list may write `div[class*=advert]` with no quotes; the canonical form has
    them. Getting this wrong would make the dedup key differ between the quoted
    and unquoted spellings of one rule, so a list shipping both would pay twice
    for one hide.
    """
    if not piece.startswith("[") or not piece.endswith("]"):
        return piece  # a pseudo: emitted verbatim
    body = piece[1:-1]
    # Split at the operator.
    for op in ("^=", "$=", "*=", "~=", "|=", "="):
        idx = body.find(op)
        if idx == -1:
            continue
        name = body[:idx].strip()
        tail = body[idx + len(op):].strip()
        flags = ""
        if tail.endswith(" i") or tail.endswith(" I"):
            flags = " i"
            tail = tail[:-2].strip()
        value = tail.strip()
        if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
            value = value[1:-1]
        return f'[{name}{op}"{value}"{flags}]'
    # kExists: no operator, no quotes.
    return f"[{body.strip()}]"


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
    # `[` must terminate the tag, or an attribute selector gets swallowed into
    # it and never reaches _canonical_attr() — which is why `div[class*=
    # promoted]` canonicalized without the quotes AppendCompound() adds. The
    # tag scan and the piece scan must agree on where a tag ends.
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
            # An attribute or pseudo: bracket-balanced copy, verbatim. The scan
            # starts at i+1 and the delimiter test only fires at depth 0 — the
            # earlier version started at i, so a leading ':' matched its own
            # break condition, produced an empty slice, and never advanced i:
            # an infinite loop on any pseudo-class. `div:has-text(x)` hung the
            # fake outright, which the vector generator caught because it
            # compares exit codes and a hung process has neither.
            # An attribute selector ends at ITS OWN closing bracket, not at a
            # balanced depth: `[class*=promoted]:has(img)` must stop after the
            # `]`, or the piece swallows the pseudo and _canonical_attr() sees
            # `[class*=promoted]:has(img)` — which has no operator-then-value
            # shape it recognises, so it returned the text unchanged and the
            # quotes AppendCompound() adds were never applied.
            if compound[i] == "[":
                j = i + 1
                while j < len(compound) and compound[j] != "]":
                    j += 1
                j = min(j + 1, len(compound))
            else:
                depth = 0
                j = i + 1
                while j < len(compound):
                    if compound[j] == "(":
                        depth += 1
                    elif compound[j] == ")":
                        depth -= 1
                        if depth == 0:
                            j += 1
                            break
                    elif compound[j] in "#.:" and depth == 0:
                        break
                    j += 1
            piece = compound[i:j]
            # Lowercase the pseudo NAME in the canonical form. AppendCompound()
            # writes the name as the parser stored it, and the parser folded it
            # to lower case, so `DIV:HAS(.a)` canonicalizes to `DIV:has(.a)` —
            # the tag keeps its case and the pseudo does not. Leaving the name
            # as-written made the dedup key differ between `:HAS` and `:has`,
            # which are the same rule.
            if piece.startswith(":"):
                close = piece.find("(")
                if close == -1:
                    piece = piece.lower()
                else:
                    piece = piece[:close].lower() + piece[close:]
            # Drop empty parens, matching AppendCompound(): it emits `:name` and
            # only adds `(args)` when the parsed pseudo HAS args, so `:remove()`
            # canonicalizes to `:remove`. Leaving the parens in made the dedup
            # key differ between the two spellings of one rule.
            if piece.endswith("()"):
                piece = piece[:-2]
            rest.append(_canonical_attr(piece))
            i = max(j, i + 1)
    for name in sorted(ids):
        out.append("#" + name)
    for name in sorted(classes):
        out.append("." + name)
    out.extend(rest)
    return "".join(out)


def canonicalize_selector(text: str) -> str:
    """The dedup key. Mirrors CanonicalizeSelector() in core/keyset.cc:
    compounds joined by " <combinator> ", each compound canonicalized."""
    # CombinatorToken() in core/keyset.cc maps kDescendant to " " (a single
    # space), and CanonicalizeSelector() puts a space on EACH side of the token.
    # So a descendant combinator renders as THREE spaces, not one — an odd
    # looking output that is nonetheless the pinned format, and a fake that
    # "tidied" it to one space would diverge on every descendant selector.
    compounds, combinators = _split_combinators(text.strip())
    out: list[str] = []
    for i, comp in enumerate(compounds):
        if i > 0:
            token = combinators[i - 1] if i - 1 < len(combinators) else " "
            if not token:
                token = " "  # the descendant combinator
            out.append(f" {token} ")
        out.append(_canonical_compound(comp))
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
        return "empty-rule-set", {}
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
            # The C++ names an empty id distinctly from a repeated one: the
            # enum is kDuplicateId in both cases but `reason` differs, and a
            # list author fixing "duplicate-rule-id" on a rule whose id is
            # merely blank would look for the wrong thing.
            return "empty-rule-id", {}
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
            # The C++ reports the SPECIFIC sub-reason in `reason` while the enum
            # stays kRuleRejected, so the fake must too — a caller that only
            # ever sees "rule-rejected" cannot tell a list author what to fix.
            return "unknown-action", {}
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
        # Only the two scope keys bind. The embedder is "not an input to this
        # comparison anywhere" (core/scope_key.h); the C++ host reads exactly
        # site + identity_class from frame_scope, so an extra key (e.g.
        # embedder_site) must not turn this into a refusal — parity law.
        if scope.get("site") != frame.get("site") or \
                scope.get("identity_class") != frame.get("identity_class"):
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
    # Count from the SAME tokenizer the canonical form is built with. Counting
    # by splitting the canonical string on spaces is wrong twice over: a
    # descendant combinator renders as three spaces, and an explicit one is
    # surrounded by them, so the split produces empty tokens that are not
    # compounds.
    compounds = len(_split_combinators(sel.strip())[0])
    # Pseudo-class NAMES, in source order, across every compound. A list that
    # reports the count but not the names cannot be checked against the
    # admitted set, which is the whole point of reporting them.
    pseudos: list[str] = []
    for m in _PSEUDO_RE.finditer(canon):
        pseudos.append(m.group(1))
    return {"canonical": canon, "compounds": compounds, "pseudos": pseudos}


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
