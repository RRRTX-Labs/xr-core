"""Behavioral fake for xr.mojom.PolicyResolver (P5 freeze).

Implements the §1.11 contract: a TOTAL, PURE, DETERMINISTIC resolution of
(identity, origin, trust_context?, request_class) -> EffectivePolicy.

  * PURE: no clock, no RNG, no environment, no I/O. Proven by
    xr-browser/docs/contracts/tests/test_policy_determinism.py which
    monkeypatches time/random/os.environ and asserts identical output.
  * TOTAL: every input produces an EffectivePolicy. Unknown identity /
    unknown origin / unknown request class / malformed input => a fully
    DENYING policy (deny-default). There is no "unset => allow" path.

The golden vectors (xr-browser/docs/contracts/vectors/policy-resolver-v1.json)
are the ground-truth expectation table; `tools/vectors_check.py` asserts this
fake reproduces them byte-for-byte. This module also exposes `resolve()` so the
xrctl dev CLI can drive it over the stdio JSON protocol.

Stdlib only. Python 3.12+.
"""
from __future__ import annotations

import sys
from typing import Any

from _base import CONTRACT_VERSION, canonical, err, ok

# The frozen set of known identities for the fake/fixtures. Real identities are
# minted by IdentityManager; the resolver only needs to know "is this a shape
# it recognizes". Anything else => deny (total function).
KNOWN_IDENTITIES = {
    "xr:00000000-0000-4000-8000-000000000001",  # standard
    "xr:00000000-0000-4000-8000-000000000002",  # fortress
    "xr:00000000-0000-4000-8000-0000000000ef",  # ephemeral (in_memory)
}
EPHEMERAL_IDENTITIES = {"xr:00000000-0000-4000-8000-0000000000ef"}
FORTRESS_IDENTITIES = {"xr:00000000-0000-4000-8000-000000000002"}

VALID_SCHEMES = {"http", "https"}
TRUST_CONTEXTS = {"kStandard", "kShield", "kFortress"}
REQUEST_CLASSES = {
    "kNavigation",
    "kSubresource",
    "kScript",
    "kPermission",
    "kStorage",
    "kNetwork",
}

# Trust ladder ordering: higher index == strictly more restrictive.
_TIER = {"kStandard": 0, "kShield": 1, "kFortress": 2}


def _deny_policy() -> dict[str, Any]:
    """The fully-denying EffectivePolicy (deny-default)."""
    return {
        "version": {"contract_version": CONTRACT_VERSION},
        "blocking": {
            "block_network_ads": True,
            "block_trackers": True,
            "upgrade_to_https": True,
        },
        "cosmetic": {"hide_cosmetic": True, "block_scriptlets": True},
        "permissions": {
            "geolocation": "kDeny",
            "camera": "kDeny",
            "microphone": "kDeny",
            "notifications": "kDeny",
        },
        "egress": {"block_third_party": True, "route": "kDirect"},
        "fingerprint": {"mode": "kStrict"},
        "storage_scope": {"scope": "kEphemeral", "in_memory": True},
        "vault_scope": {"autofill_allowed": False, "export_allowed": False},
        "process_policy": {"site_isolated": True, "dedicated_process": True},
        "letterbox": True,
    }


def _tiered_policy(
    tier: str, identity: str, request_class: str
) -> dict[str, Any]:
    """Deterministic policy for a recognized (identity, origin, tier, class)."""
    t = _TIER[tier]
    ephemeral = identity in EPHEMERAL_IDENTITIES
    fortress = identity in FORTRESS_IDENTITIES or t == 2

    perms_default = "kAsk" if t == 0 else "kDeny"
    # Permission requests on the strictest tier are always deny.
    perms = {
        "geolocation": perms_default,
        "camera": perms_default,
        "microphone": perms_default,
        "notifications": "kAsk" if t <= 1 else "kDeny",
    }

    route = {0: "kDirect", 1: "kProxy", 2: "kTor"}[t]
    fp = {0: "kReduce", 1: "kReduce", 2: "kStrict"}[t]

    if ephemeral:
        scope = {"scope": "kEphemeral", "in_memory": True}
    elif fortress:
        scope = {"scope": "kFortressPartition", "in_memory": False}
    else:
        scope = {"scope": "kIdentityScoped", "in_memory": False}

    return {
        "version": {"contract_version": CONTRACT_VERSION},
        "blocking": {
            "block_network_ads": True,
            "block_trackers": True,
            "upgrade_to_https": True,
        },
        "cosmetic": {
            "hide_cosmetic": t >= 1,
            "block_scriptlets": t >= 1,
        },
        "permissions": perms,
        "egress": {"block_third_party": t >= 1, "route": route},
        "fingerprint": {"mode": fp},
        "storage_scope": scope,
        "vault_scope": {
            # Autofill only on navigation/permission classes and only when not
            # a script/subresource context; never export.
            "autofill_allowed": request_class in {"kNavigation", "kPermission"},
            "export_allowed": False,
        },
        "process_policy": {
            "site_isolated": True,
            "dedicated_process": fortress,
        },
        "letterbox": t == 2,
    }


def resolve(request: dict[str, Any]) -> dict[str, Any]:
    """Total, pure resolution. `request` mirrors PolicyResolver.Resolve args.

    Returns a ResolveResult-shaped dict: {"ok": EffectivePolicy} always in
    practice, because the resolver is total (deny is still an EffectivePolicy).
    The one error arm is kVersionMismatch, surfaced deny-safe.
    """
    if not isinstance(request, dict):
        return ok(_deny_policy())

    identity = request.get("identity")
    origin = request.get("origin")
    trust = request.get("trust_context")  # optional; absent => kStandard.
    rclass = request.get("request_class")

    # Version gate first (deny-safe error arm).
    cv = request.get("contract_version", CONTRACT_VERSION)
    if cv != CONTRACT_VERSION:
        return err("kVersionMismatch")

    # Malformed / unknown => deny (total).
    if not isinstance(identity, dict) or identity.get("value") not in KNOWN_IDENTITIES:
        return ok(_deny_policy())
    if not isinstance(origin, dict):
        return ok(_deny_policy())
    if origin.get("scheme") not in VALID_SCHEMES:
        return ok(_deny_policy())
    if not origin.get("registrable_domain"):
        return ok(_deny_policy())
    if rclass not in REQUEST_CLASSES:
        return ok(_deny_policy())

    tier = "kStandard" if trust is None else trust
    if tier not in TRUST_CONTEXTS:
        return ok(_deny_policy())

    return ok(_tiered_policy(tier, identity["value"], rclass))


# stdio JSON protocol entry point (see fakes/README.md). One JSON request on
# argv[1] or stdin; one canonical JSON result on stdout; exit 0 always (the
# result carries the typed outcome, deny-safe).
def main(argv: list[str]) -> int:
    raw = argv[1] if len(argv) > 1 else sys.stdin.read()
    try:
        request = __import__("json").loads(raw)
    except Exception:
        print(canonical(ok(_deny_policy())))
        return 0
    print(canonical(resolve(request)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
