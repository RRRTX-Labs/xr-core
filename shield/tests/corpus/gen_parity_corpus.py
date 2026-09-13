#!/usr/bin/env python3
"""Generate the T8 shield parity corpus (xr-shield-parity-corpus v1).

Machine-derived, license-clean (synthetic, CC0), deterministic (pure
enumeration). Every case carries the EXPECTED verdict derived from the
documented v1 semantics (fake_engine.h's law comment) by an independent
implementation — NOT by running any engine. Replayed by xr-browser
tools/shield_parity.py (±2% agreement / FP ≤0.5%). CLI: [--out PATH]
[--check] [--json]; exit 0 ok · 1 drift · 2 usage
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

INSTANCES = 21  # d = 0..20 — 73 variants × 21 ≥ the 1,500-case floor
ATTR = "XR P11 synthetic parity fixture (CC0-1.0)"
SEP = "\x01"  # "^" sentinel inside parsed segments
CLASSES = ["kSubresource", "kScript", "kNavigation", "kNetwork",
           "kPermission", "kStorage"]
WHY = {"block": "rule-blocked", "allow": "rule-allowed",
       "redirect": "rule-redirected", "replace": "rule-replaced"}


def split_url(url: str) -> dict:
    pos = url.find("://")
    scheme = url[:pos].lower()
    rest = url[pos + 3:]
    host_end = len(rest)
    for ch in "/?#":
        i = rest.find(ch)
        if i != -1:
            host_end = min(host_end, i)
    host = rest[:host_end].lower()
    host = host.split(":", 1)[0]
    pstart = rest.find("/")
    tail = rest[pstart:] if (pstart != -1 and pstart <= host_end) else "/"
    cut = len(tail)
    for ch in "?#":
        i = tail.find(ch)
        if i != -1:
            cut = min(cut, i)
    path = tail[:cut]
    if not path.startswith("/"):
        path = "/" + path
    return {"scheme": scheme, "host": host, "path": path}


def parse_filter(f: str) -> dict:
    body = f
    domain_anchor = left_anchor = right_anchor = False
    if body.startswith("||"):
        domain_anchor, body = True, body[2:]
    elif body.startswith("|"):
        left_anchor, body = True, body[1:]
    if body.endswith("|") and body != "":
        right_anchor, body = True, body[:-1]
    segments: list[str] = []
    cur = ""
    for c in body:
        if c == "*":
            if cur != "" or not segments:
                segments.append(cur)
            cur = ""
            continue
        cur += SEP if c == "^" else c
    segments.append(cur)
    return {"domain_anchor": domain_anchor, "left_anchor": left_anchor,
            "right_anchor": right_anchor, "segments": segments}


def match_seg_at(t: str, seg: str, pos: int) -> int:
    p = pos
    for i, c in enumerate(seg):
        if c == SEP:
            if p == len(t):
                return p if i + 1 == len(seg) else -1
            if t[p] not in "/:":
                return -1
            p += 1
            continue
        if p >= len(t) or t[p] != c:
            return -1
        p += 1
    return p


def find_seg(t: str, seg: str, frm: int) -> int:
    for s in range(frm, len(t) + 1):
        e = match_seg_at(t, seg, s)
        if e >= 0:
            return e
    return -1


def match_segments(segs: list[str], target: str, la: bool, ra: bool) -> bool:
    if not segs:
        return False
    if ra and segs[-1] != "":
        last = segs[-1]
        anchor = -1
        for s in range(len(target) + 1):
            if match_seg_at(target, last, s) == len(target):
                anchor = s
                break
        if anchor < 0:
            return False
        pos = 0
        for i, seg in enumerate(segs[:-1]):
            e = match_seg_at(target, seg, pos) if (i == 0 and la) \
                else find_seg(target, seg, pos)
            if e < 0 or e > anchor:
                return False
            pos = e
        return True
    pos = 0
    for i, seg in enumerate(segs):
        e = match_seg_at(target, seg, pos) if (i == 0 and la) \
            else find_seg(target, seg, pos)
        if e < 0:
            return False
        pos = e
    return True


def filter_match(pf: dict, parts: dict) -> bool:
    surface = f"{parts['scheme']}://{parts['host']}{parts['path']}"
    if not pf["domain_anchor"]:
        return match_segments(pf["segments"], surface, pf["left_anchor"],
                              pf["right_anchor"])
    seg0 = pf["segments"][0]
    cut = len(seg0)
    for i, c in enumerate(seg0):
        if c == SEP or c == "/":
            cut = i
            break
    d = seg0[:cut]
    host = parts["host"]
    if not (d == "" or host == d
            or (len(host) > len(d) and host.endswith("." + d))):
        return False
    rest = seg0[cut:]
    if pf["right_anchor"] and rest == "" and len(pf["segments"]) == 1:
        return parts["path"] == "/"  # "||d|" — the bare domain
    return match_segments([rest] + pf["segments"][1:], parts["path"], True,
                          pf["right_anchor"])


def expected(rule_docs: list[dict], url: str, rd: str) -> dict:
    parts = split_url(url)
    host, first = parts["host"], None
    for rv in rule_docs:
        if rv["kind"] == "cosmetic":
            continue
        doms = rv.get("domains", [])
        excl = rv.get("exclude_domains", [])
        if doms and not any(x in (rd, host) for x in doms):
            continue
        if excl and any(x in (rd, host) for x in excl):
            continue
        if not filter_match(parse_filter(rv["filter"]), parts):
            continue
        hit = {"action": rv["action"], "engine_hit": True,
               "why_code": WHY[rv["action"]], "rule_id": rv["id"]}
        if rv["action"] == "allow":
            return hit
        if first is None:
            first = hit
    return first or {"action": "allow", "engine_hit": False,
                     "why_code": "no-match", "rule_id": ""}


def net(rid, filt, action="block", **kw):
    r = {"id": rid, "kind": "network", "filter": filt, "action": action}
    r.update(kw)
    return r


def rd2(host: str) -> str:
    return ".".join(host.split(".")[-2:])


SPECS: dict[str, tuple] = {
    "host-caret": (lambda d: [net("r0", f"||h{d}.track.example.com^")], [
        ("exact", "https://h{d}.track.example.com/x.js", None, []),
        ("root", "https://h{d}.track.example.com/", None, []),
        ("sub", "https://s.h{d}.track.example.com/a", None, []),
        ("deep-sub", "https://a.b.h{d}.track.example.com/", None, []),
        ("prefix-lookalike", "https://h{d}x.track.example.com/x", None, []),
        ("suffix-lookalike", "https://xh{d}.track.example.com/x", None, []),
        ("other-tld", "https://h{d}.track.example.net/x", None, []),
        ("query", "https://h{d}.track.example.com/x?q=h{d}", None, []),
        ("fragment", "https://h{d}.track.example.com/x#f", None, []),
        ("port", "https://h{d}.track.example.com:8080/x", None, []),
        ("colon-path", "https://h{d}.track.example.com/a:b", None, [])]),
    "host-path": (lambda d: [net("r0", f"||p{d}.example.net/js/ad.js")], [
        ("exact", "https://p{d}.example.net/js/ad.js", None, []),
        ("extended", "https://p{d}.example.net/js/ad.jsx", None, []),
        ("nonprefix", "https://p{d}.example.net/x/js/ad.js", None, []),
        ("sub-host", "https://s.p{d}.example.net/js/ad.js", None, []),
        ("wrong-host", "https://q{d}.example.net/js/ad.js", None, []),
        ("query", "https://p{d}.example.net/js/ad.js?v=1", None, []),
        ("root", "https://p{d}.example.net/", None, []),
        ("path-case", "https://p{d}.example.net/JS/ad.js", None, []),
        ("host-case", "https://P{d}.EXAMPLE.net/js/ad.js", None, [])]),
    "bare-domain-pipe": (lambda d: [net("r0", f"||z{d}.example.org|")], [
        ("root", "https://z{d}.example.org/", None, []),
        ("path", "https://z{d}.example.org/x", None, []),
        ("query-root", "https://z{d}.example.org/?q", None, []),
        ("sub-root", "https://s.z{d}.example.org/", None, []),
        ("sub-path", "https://s.z{d}.example.org/x", None, [])]),
    "left-anchor": (lambda d: [net("r0", f"|https://la{d}.example.com/x")], [
        ("exact", "https://la{d}.example.com/x", None, []),
        ("extended", "https://la{d}.example.com/xy", None, []),
        ("http-scheme", "http://la{d}.example.com/x", None, []),
        ("sub-host", "https://s.la{d}.example.com/x", None, []),
        ("deeper", "https://la{d}.example.com/x/y", None, [])]),
    "right-anchor": (lambda d: [net("r0", f"ra{d}.js|")], [
        ("ends-surface", "https://x{d}.example.com/a/ra{d}.js", None, []),
        ("not-at-end", "https://x{d}.example.com/a/ra{d}.js/x", None, []),
        ("query-stripped", "https://x{d}.example.com/a/ra{d}.js?q", None, []),
        ("host-only", "https://ra{d}.js/x", None, [])]),
    "right-anchor-void": (lambda d: [net("r0", f"va{d}.js*|")], [
        ("interior", "https://x{d}.example.com/a/va{d}.js/b", None, []),
        ("ends", "https://x{d}.example.com/a/va{d}.js", None, []),
        ("absent", "https://x{d}.example.com/a/z.js", None, [])]),
    "wildcard-mid": (lambda d: [net("r0", f"||w{d}.example.com/ad_*.js")], [
        ("exact", "https://w{d}.example.com/ad_1.js", None, []),
        ("empty-star", "https://w{d}.example.com/ad_.js", None, []),
        ("nonprefix", "https://w{d}.example.com/x/ad_1.js", None, []),
        ("extended", "https://w{d}.example.com/ad_1.jsx", None, []),
        ("wrong-host", "https://v{d}.example.com/ad_1.js", None, []),
        ("query", "https://w{d}.example.com/ad_1.js?x", None, [])]),
    "wildcard-multi": (lambda d: [net("r0", f"*mm{d}*")], [
        ("in-path", "https://x{d}.example.com/mm{d}/a", None, []),
        ("in-host", "https://amm{d}b.example.com/x", None, []),
        ("absent", "https://x{d}.example.com/a", None, []),
        ("query-only", "https://x{d}.example.com/a?q=mm{d}", None, []),
        ("path-case", "https://x{d}.example.com/MM{d}/a", None, [])]),
    "sep-chain": (lambda d: [net("r0", f"||s{d}.example.com^a^b.js")], [
        ("slashes", "https://s{d}.example.com/a/b.js", None, []),
        ("colon-sep", "https://s{d}.example.com/a:b.js", None, []),
        ("fused", "https://s{d}.example.com/ab.js", None, []),
        ("nonprefix", "https://s{d}.example.com/x/a/b.js", None, [])]),
    "plain-literal": (lambda d: [net("r0", f"pl{d}.js")], [
        ("in-path", "https://x{d}.example.com/a/pl{d}.js", None, []),
        ("in-host", "https://pl{d}.js.example.com/a", None, []),
        ("absent", "https://x{d}.example.com/a/z.js", None, [])]),
    "allow-paired": (lambda d: [
        net("r-block", f"||ap{d}.example.com^"),
        net("r-allow", f"||ap{d}.example.com^", action="allow")], [
        ("hit", "https://ap{d}.example.com/x", None, []),
        ("sub-hit", "https://s.ap{d}.example.com/x", None, []),
        ("miss", "https://x{d}.example.com/y", None, [])]),
    "allow-only": (lambda d: [
        net("r0", f"||ao{d}.example.com^", action="allow")], [
        ("hit", "https://ao{d}.example.com/x", None,
         ["provenance-divergence-D5"]),
        ("miss", "https://x{d}.example.com/y", None, [])]),
    "domains-rd": (lambda d: [
        net("r0", f"||dm{d}.example.com^", domains=[f"site{d}.example",
                                                    f"dm{d}.example.com"])], [
        ("rd-in", "https://dm{d}.example.com/x", "site{d}.example", []),
        ("host-in", "https://dm{d}.example.com/x", "ot{d}.example", [])]),
    "domains-out": (lambda d: [
        net("r0", f"||dn{d}.example.com^", domains=[f"near{d}.example"])], [
        ("rd-out", "https://dn{d}.example.com/x", "ot{d}.example", [])]),
    "exclude-rd": (lambda d: [
        net("r0", f"||ex{d}.example.com^",
            exclude_domains=[f"safe{d}.example"])], [
        ("rd-excluded", "https://ex{d}.example.com/x", "safe{d}.example", []),
        ("rd-ok", "https://ex{d}.example.com/x", "ot{d}.example", [])]),
    "exclude-host": (lambda d: [
        net("r0", f"||eh{d}.example.com^",
            exclude_domains=[f"eh{d}.example.com"])], [
        ("host-excluded", "https://eh{d}.example.com/x", "ot{d}.example",
         [])]),
    "redirect": (lambda d: [
        {"id": "r0", "kind": "redirect", "filter": f"||rt{d}.example.com^ban",
         "action": "redirect", "resource": "1x1.gif"}], [
        ("hit", "https://rt{d}.example.com/banner", None, []),
        ("miss", "https://rt{d}.example.com/other", None, [])]),
    "replace": (lambda d: [
        {"id": "r0", "kind": "resource", "filter": f"||rz{d}.example.com/x",
         "action": "replace", "resource": "noop.js"}], [
        ("hit", "https://rz{d}.example.com/x", None, []),
        ("miss", "https://rz{d}.example.com/y", None, [])]),
    "cosmetic-ignored": (lambda d: [
        {"id": "r0", "kind": "cosmetic", "filter": f"cs{d}.example.com",
         "action": "block"}], [
        ("would-match", "https://cs{d}.example.com/x", None, [])]),
    "option-law-pin": (lambda d: [
        net("r0", f"||pin{d}.example.com^",
            domains=[f"x.pin{d}.example.com"])], [
        ("subdomain-entry-out", "https://y.x.pin{d}.example.com/a", None,
         ["option-law-pin-D2"]),
        ("host-exact-in", "https://x.pin{d}.example.com/a", None,
         ["option-law-pin-D2"])]),
}


def build() -> dict:
    bundles: dict[str, dict] = {}
    cases: list[dict] = []
    n = 0
    for cls in sorted(SPECS):
        rules_fn, variants = SPECS[cls]
        for d in range(INSTANCES):
            bid = f"{cls}-d{d}"
            rules = rules_fn(d)
            bundles[bid] = {
                "schema": "xr-list-bundle", "schema_version": 1,
                "name": f"parity-{bid}", "bundle_version": 1, "refusals": [],
                "lists": [{"name": "l0", "attribution": ATTR,
                           "rules": rules}]}
            for vname, utmpl, rdspec, tags in variants:
                url = utmpl.format(d=d)
                host = split_url(url)["host"]
                if rdspec is None:
                    rd = rd2(host)
                elif callable(rdspec):
                    rd = rdspec(d, host)
                else:
                    rd = rdspec.format(d=d)
                exp = expected(rules, url, rd)
                cases.append({
                    "id": f"pc-{n:05d}", "bundle": bid, "url": url, "rd": rd,
                    "request_class": CLASSES[n % len(CLASSES)],
                    "rule_class": cls, "variant": vname, "tags": tags,
                    "expect": exp})
                n += 1
    return {"schema": "xr-shield-parity-corpus", "schema_version": 1,
            "generated_by": "shield/tests/corpus/gen_parity_corpus.py",
            "instances": INSTANCES, "case_count": len(cases),
            "bundles": bundles, "cases": cases}


def dump(doc: dict) -> str:
    return json.dumps(doc, indent=1, sort_keys=True) + "\n"


def main(argv: list[str]) -> int:
    out = Path(__file__).resolve().parent / "parity-corpus-v1.json"
    check = as_json = False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--out" and i + 1 < len(argv):
            out, i = Path(argv[i + 1]), i + 2
        elif a in ("--check", "--json"):
            check, as_json = check or a == "--check", \
                as_json or a == "--json"
            i += 1
        else:
            print(f"error: unknown argument {a!r}", file=sys.stderr)
            return 2
    doc = build()
    text = dump(doc)
    if check:
        if not out.is_file():
            print(f"error: --check but {out} does not exist", file=sys.stderr)
            return 1
        same = out.read_text(encoding="utf-8") == text
        rep = {"tool": "gen_parity_corpus", "mode": "check", "path": str(out),
               "identical": same, "case_count": doc["case_count"]}
        print(json.dumps(rep) if as_json else
              f"gen_parity_corpus --check: "
              f"{'IDENTICAL' if same else 'DRIFT'} ({rep['case_count']})")
        return 0 if same else 1
    out.write_text(text, encoding="utf-8")
    rep = {"tool": "gen_parity_corpus", "mode": "write", "path": str(out),
           "case_count": doc["case_count"], "bundles": len(doc["bundles"])}
    print(json.dumps(rep) if as_json else
          f"gen_parity_corpus: wrote {rep['case_count']} cases -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
