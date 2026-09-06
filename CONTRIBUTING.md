# Contributing to xr-core

Short guide; the full governance lives in the meta repository
(`xr-browser/`, sibling directory).

## Ground rules (Plan = single source of truth)

1. **The Plan binds.** `xr-browser/docs/XR_BROWSER_MASTER_IMPLEMENTATION_PLAN.md`
   (SHA-256 pinned) is the single operational source of truth. If you
   believe a Decision Register row is wrong: stop, write
   `xr-browser/docs/adr/draft-issue-<n>.md` with evidence, keep the Plan as
   is, and let the orchestrator rule (L24).
2. **DCO on every commit.** Every commit must carry
   `Signed-off-by: Your Name <your@email>` (Developer Certificate of
   Origin, https://developercertificate.org/). `git commit --signoff` adds
   it. CI enforces this (`tools/dco_check.py`).
3. **S0 paths need dual senior review** (one from Security): `/mojom/
   /policy/ /identity/ /net/ /vault/ /extensions/` (see `CODEOWNERS`,
   generated from `xr-browser/docs/process/s0-paths.yaml` — do not edit
   CODEOWNERS by hand).
4. **No new seams without an RFC.** Coding agents and new engineers work
   from issue text + the Plan; inventing interfaces is out of scope by
   definition — raise an RFC (ADR) instead (L24).
5. **No feature without tests, no completion claim without evidence**
   (L8/L11): PRs link the test artifacts; the PR template enforces the
   fields.

## Currently (P1)

This repository contains **governance files only**. There is no code to
compile, no `BUILD.gn`, no `DEPS`. Phase P2 (build system) and P5
(contract freeze) create the engineering surface; contribute to those via
the meta-repo issue tracker once it is hosted.

## PR template

Use the meta-repo template (`xr-browser/.github/pull_request_template.md`):
task id, contract refs, S0 flag, evidence paths.
