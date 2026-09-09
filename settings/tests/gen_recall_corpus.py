#!/usr/bin/env python3
"""Generate settings/tests/recall_corpus.json — the P8 settings-search recall
query set (200 phrases), as DATA (checked in). Regenerate with:
    python3 settings/tests/gen_recall_corpus.py --out settings/tests/recall_corpus.json
    python3 settings/tests/gen_recall_corpus.py --check   (CI diff-clean gate)

The corpus is DERIVED from the schema data (settings_schema_v1.json):
queries are deterministic transforms of each setting's search aliases —
paraphrase templates, subseq-safe typos (deletions only: subsequence
matching tolerates deletions but not insertions/transpositions), alias
phrases verbatim, concatenations, and the RTL (Arabic) aliases. That keeps
"query set" and "index" in one place: recall measures the DATA, not a
hand-tuned query list that can drift from the aliases.

Recall contract (test_settings_search / test_recall_corpus): >=95% of the
200 phrases must resolve to the RIGHT setting in the top 3.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Paraphrase templates: {alias} is replaced; every word still scores, so
# full sentences exercise word-level matching (the fzf-class per-word core).
TEMPLATES = [
    "{a}",                     # verbatim alias
    "how do i change {a}",     # paraphrase family
    "where is {a}",
    "settings for {a}",
    "turn on {a}",
    "find {a}",
    "{a} settings",
]

# Deletion typos: subsequence-safe by construction (query = target minus one
# char). Applied to ASCII words of >= 5 chars, at most one typo per query.
DEL_INDEX = [1, 2, len_cut := -1] if False else [1, 2, -2]


def del_typo(word: str) -> list[str]:
    out: list[str] = []
    for i in range(1, len(word) - 1):
        if len(out) >= 2:
            break
        out.append(word[:i] + word[i + 1:])
    if len(word) >= 5:
        out.append(word[:-1])
    return out


def expand_alias(alias: str) -> list[dict[str, str]]:
    """Deterministic variants of one alias for its owning setting key."""
    variants: list[dict[str, str]] = []
    for t in TEMPLATES:
        variants.append({"query": t.format(a=alias), "expect": "?"})
    # concatenated phrase form (spaces removed) — matches via the joined
    # field token (subseq/prefix rules).
    if " " in alias:
        variants.append({"query": alias.replace(" ", ""), "expect": "?"})
        words = alias.split(" ")
        if len(words) >= 2:
            # word-order flip: no contiguous phrase survives — the row is
            # decided by the per-word matcher alone.
            variants.append({"query": " ".join(reversed(words)), "expect": "?"})
            # dropped first word: the query is resolved against the whole
            # alias/token set by the remaining words.
            variants.append({"query": " ".join(words[1:]), "expect": "?"})
    return variants


def build(schema_path: Path) -> dict[str, object]:
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    groups: list[list[dict[str, str]]] = []  # per-setting variant list
    for st in schema["settings"]:
        key = st["key"]
        g: list[dict[str, str]] = []
        aliases = st.get("aliases", [])
        for alias in aliases:
            for v in expand_alias(alias):
                v["expect"] = key
                g.append(v)
            # typos on the alias's ASCII words (deterministic, subseq-safe)
            for word in alias.split(" "):
                if word.isascii() and word.isalpha() and len(word) >= 5:
                    for t in del_typo(word):
                        q = alias.replace(word, t, 1)
                        if q != alias:
                            g.append({"query": q, "expect": key})
        # The plan's vocabulary-law probes: container(s) is an ALIAS for the
        # identity vocabulary — search-only, never a label.
        if key.startswith("identity."):
            g.append({"query": "container", "expect": key})
            g.append({"query": "containers", "expect": key})
        # Deterministic de-dup per group, preserve order.
        seen: set[str] = set()
        g = [e for e in g if not (e["query"] in seen or seen.add(e["query"]))]
        if g:
            groups.append(g)
    # Round-robin over groups so EVERY setting is represented in the 200.
    # Pool is ~850 >> 200, so one cycle suffices without repetition.
    dedup: list[dict[str, str]] = []
    idx = [0] * len(groups)
    total = 200
    while len(dedup) < total:
        advanced = 0
        for gi, g in enumerate(groups):
            if len(dedup) >= total:
                break
            if idx[gi] < len(g):
                dedup.append(g[idx[gi]])
                idx[gi] += 1
                advanced += 1
        if advanced == 0:
            break  # pools exhausted mid-cycle
    if len(dedup) < total:  # should not happen: pool >> 200
        raise SystemExit(f"corpus underfull: {len(dedup)}")
    # balance check: every setting must appear
    per_key: dict[str, int] = {}
    for e in dedup:
        per_key[e["expect"]] = per_key.get(e["expect"], 0) + 1
    if len(per_key) != len(groups):
        raise SystemExit(f"corpus misses settings: {set(per_key)} vs {len(groups)}")
    return {
        "schema": "xr-settings-recall-corpus",
        "schema_version": 1,
        "note": "200-phrase settings-search recall corpus (P8). >=95% top-3 "
                "bar. Derived deterministically from settings_schema_v1.json "
                "aliases by gen_recall_corpus.py — regenerate, never hand-edit. "
                "Every searchable setting is represented (round-robin).",
        "generated_from": "settings/core/settings_schema_v1.json",
        "count": len(dedup),
        "queries": dedup,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out",
                    default=str(ROOT / "settings/tests/recall_corpus.json"))
    ap.add_argument("--schema",
                    default=str(ROOT / "settings/core/settings_schema_v1.json"))
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    doc = build(Path(args.schema))
    if args.check:
        cur = json.loads(Path(args.out).read_text(encoding="utf-8"))
        if cur == doc:
            print("recall_corpus.json is diff-clean")
            return 0
        print("FAIL: recall_corpus.json drifted — run the generator")
        return 1
    Path(args.out).write_text(
        json.dumps(doc, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {doc['count']} queries to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
