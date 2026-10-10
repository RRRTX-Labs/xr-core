"""Python twin of identity/core/ledger_tag (P14-T5, P14-CLOSE C-2).

The second backend of `ledger-identity-overlay-v1`. The byte-law
(xr-browser docs/contracts/vectors/ledger-identity-overlay-v1.json) replays
every vector against BOTH this module and the compiled identity_host's
living subcommands (`ledger-tag`, `omnibox-filter`, `history-oracle`) and
requires byte-identical stdout and equal exit codes
(xr-browser tools/ledger_identity_check.py).

Laws L1–L5 are documented once, in identity/core/ledger_tag.h. This file
mirrors them; it does not restate them.

Invocation (the host's living form):
    python3 fakes/ledger_identity.py <cmd> '<json-args>'
Exit 0 for success and kRejected (in-band), 1 for kMalformedInput. Stdlib
only, deterministic, no clock, no I/O beyond argv/stdout.
"""
from __future__ import annotations

import json
import sys
from typing import Any

SCHEMA = "ledger-identity-overlay-v1"
# L2: the eight frozen ActivityKind values + the two overlay classes, in the
# core's canonical order.
CLASSES = ("kPolicy", "kBlock", "kPermission", "kIdentity", "kRoute",
           "kVault", "kGuard", "kDownload", "kHistory", "kBookmark")
UPSTREAM_FIELDS = ("url_id", "visit_id", "url", "title", "ts")
_HEX = set("0123456789abcdef")


def canonical(obj: Any) -> str:
    return json.dumps(obj, sort_keys=True, separators=(",", ":"))


def domain_shape_ok(d: str) -> bool:
    """identity/core/mint.cc DomainShapeOk, byte-for-byte in behaviour."""
    if len(d) not in (39, 40) or not d.startswith("xr:"):
        return False
    for i in range(3, len(d)):
        c = d[i]
        if c in _HEX:
            continue
        if c == "-" and i in (11, 16, 21, 26):
            continue
        return False
    return d[17] == "4" and d[22] in "89ab"


def _valid_id(obj: Any) -> tuple[str, str]:
    v = obj.get("identity_id") if isinstance(obj, dict) else None
    if not isinstance(v, str) or not v:
        return "", "identity-id-required"
    if not domain_shape_ok(v):
        return "", "identity-id-malformed"
    return v, ""


def _reject(reason: str) -> tuple[dict, int]:
    return {"error": "kRejected", "reason": reason}, 0


def ledger_tag(args: dict) -> tuple[dict, int]:
    ident, why = _valid_id(args)
    if not ident:
        return _reject(why)
    cls = args.get("event_class")
    cls = cls if isinstance(cls, str) else ""
    if cls not in CLASSES:
        return _reject(f"unknown-event-class:{cls}")
    event = args.get("event")
    if not isinstance(event, dict):
        return _reject("event-not-object")
    return {"event": event, "event_class": cls, "identity_id": ident,
            "schema": SCHEMA}, 0


def omnibox_filter(args: dict) -> tuple[dict, int]:
    current, why = _valid_id(args)
    if not current:
        return _reject(why)
    rows = args.get("rows")
    if not isinstance(rows, list):
        return _reject("rows-not-array")
    mine, unknown = [], []
    for row in rows:
        if not isinstance(row, dict):
            continue
        owner, _ = _valid_id(row)
        if not owner:
            u = {k: v for k, v in row.items() if k != "identity_id"}
            u["provenance"] = "unknown"
            unknown.append(u)
        elif owner == current:
            mine.append(row)
        # another identity's row: dropped, not counted (L4)
    return {"current": mine, "identity_id": current, "unknown": unknown}, 0


def history_oracle(args: dict) -> tuple[dict, int]:
    ident, why = _valid_id(args)
    if not ident:
        return _reject(why)
    rows = args.get("upstream_rows")
    if not isinstance(rows, list):
        return _reject("upstream-rows-not-array")
    for row in rows:
        if not isinstance(row, dict):
            return _reject("upstream-row-not-object")
        for k in sorted(row):
            if k not in UPSTREAM_FIELDS:
                return _reject(f"upstream-row-unknown-field:{k}")

    def write(overlay: list | None) -> list:
        upstream = []
        for row in rows:
            upstream.append({k: row[k] for k in UPSTREAM_FIELDS if k in row})
            if overlay is not None:
                overlay.append({"identity_id": ident,
                                "url_id": row.get("url_id"),
                                "visit_id": row.get("visit_id")})
        return upstream

    overlay: list = []
    before = canonical(write(None))
    after = canonical(write(overlay))
    same = before == after and after == canonical(rows)
    return {"overlay_rows": len(overlay),
            "upstream_bytes_after": len(after.encode("utf-8")),
            "upstream_bytes_before": len(before.encode("utf-8")),
            "verdict": "diff-clean" if same else "DELTA"}, 0


DISPATCH = {"ledger-tag": ledger_tag, "omnibox-filter": omnibox_filter,
            "history-oracle": history_oracle}


def call(cmd: str, args_text: str) -> tuple[str, int]:
    fn = DISPATCH.get(cmd)
    if fn is None:
        return canonical({"detail": f"unknown method '{cmd}'",
                          "error": "kUnknownMethod"}), 1
    try:
        args = json.loads(args_text)
    except ValueError:
        args = None
    if not isinstance(args, dict):
        return canonical({"detail": "args must be a JSON object",
                          "error": "kMalformedInput"}), 1
    out, rc = fn(args)
    return canonical(out), rc


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(canonical({"detail": "usage: <cmd> '<args>'",
                         "error": "kMalformedInput"}))
        return 1
    text, rc = call(argv[1], argv[2] if len(argv) > 2 else "{}")
    print(text)
    return rc


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
