"""Behavioral fake for xr.mojom.DownloadSafety. Deterministic; stdlib."""
from __future__ import annotations

import json
import sys
from typing import Any

from _base import canonical, err, ok

# Deterministic verdict table keyed by content hash prefix.
_KNOWN_SAFE = "safe"
_KNOWN_BAD = "bad0"


def scan(content_sha256: str, mime_type: str) -> dict:
    if not content_sha256:
        return err("kMalformedInput")
    if content_sha256.startswith(_KNOWN_BAD):
        return ok({"verdict": "kDangerous", "quarantine": "kHeld", "direct_media_affordance": False})
    if content_sha256.startswith(_KNOWN_SAFE):
        media = mime_type.startswith(("audio/", "video/", "image/"))
        return ok({"verdict": "kSafe", "quarantine": "kReleased", "direct_media_affordance": media})
    # unknown => treat as unsafe (deny-default).
    return ok({"verdict": "kUnknown", "quarantine": "kHeld", "direct_media_affordance": False})


def call(method: str, args: dict[str, Any]) -> dict:
    if method == "Scan":
        return scan(args.get("content_sha256", ""), args.get("mime_type", ""))
    if method == "Verdict":
        return scan(args.get("download_id", ""), args.get("mime_type", ""))
    return err("kMalformedInput")


def main(argv: list[str]) -> int:
    payload = json.loads(argv[1] if len(argv) > 1 else sys.stdin.read())
    print(canonical(call(payload.get("method", ""), payload.get("args", {}))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
