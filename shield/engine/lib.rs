// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/engine — the Rust side of the C ABI in
// xr_shield_engine.h: the vendored adblock-rust engine behind the SAME
// BlockingEngine contract the C++ core injects (P11-T8: the
// parity-measured binding; the T2 skeleton's no-op match is gone).
//
// STATUS: HOSTED-LANE. This crate builds with cargo in the core-hardening
// shield-vendor job against //xr/third_party/rust (source replacement,
// offline). There is no local cargo build; the signatures below were
// written against the vendored 0.13.3 source
// (engine.rs:136 new_with_filter_set, engine.rs:254 check_network_request,
// lists.rs:230 FilterSet::new(debug), lists.rs:240 add_filter_list —
// add_filter is #[cfg(test)]-only (run 34758097067 taught that);
// request.rs:207 Request::new, blocker.rs:98 check → BlockerResult). T8's parity job
// (xr-browser tools/shield_parity.py, >=1,500-case corpus, ±2% agreement /
// FP <=0.5%) is the gate that turns "mirrors" into "measured".
//
// Laws honored here: total (every export catches panics — a caught panic
// flips alive to false and the posture core fail-OPENS; the release
// profile therefore must NOT set panic="abort", which would make
// catch_unwind a no-op), deterministic (no clock, no RNG, no I/O),
// allow-overrides-block (adblock-rust's native exception semantics — the
// same rule shield/core/fake_engine.h's TableEngine implements).
//
// v1 mapping decisions (recorded in xr-browser
// docs/shield/parity-divergences.md):
//   * DIVISION OF LABOR: adblock-rust decides FILTER SHAPE (the ABP
//     syntax subset the v1 grammar compiles to, indexed — the product
//     performance path); the shim enforces the v1 RULE-OPTION law
//     post-hit (domains/exclude_domains are EXACT membership against the
//     FFI-passed registrable_domain/host, per fake_engine.h — NOT
//     $domain=, whose ABP subdomain semantics are broader than v1's
//     documented exact-membership law; divergence class D-2 is thereby
//     an enforcement point, not a measured gap).
//   * FilterSet::new(true): debug mode keeps each filter's raw_line so a
//     BlockerResult recovers to a rule index through filter_index.
//     Fused/duplicate-text filters fall back to the u32::MAX sentinel =
//     matched-but-unidentified; the C++ adapter bounds-checks and leaves
//     rule_id empty rather than guessing (recovery ambiguity for rules
//     with options is divergence class D-4 — single-rule parity cases
//     are exact).
//   * redirect rules compile as their bare filter and the hit maps back
//     to action 2 + the rule's resource NAME through the side table: v1
//     bundles carry no resource bodies, and the C++ core's kRedirect is
//     a labeled block at the network layer — same observable as
//     TableEngine (divergence class D-1 resolved by mapping, body-less
//     redirect resources stay a documented v1 limit).
//   * Request::new(url, url, "other", "get"): the v1 FFI carries no
//     initiator, so the request is its own first-party source; v1 rules
//     have no third-party dimension (no $third-party in the grammar —
//     "$" in filter text is a bundle-compile refusal), so this is exact.

use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::sync::atomic::{AtomicBool, Ordering};

use adblock::engine::Engine;
use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;

/// Sentinel for a hit whose filter could not be recovered to a rule index
/// (optimizer-fused filters). The C++ adapter treats out-of-range indices
/// as "matched, unidentified" and leaves rule_id empty.
const UNIDENTIFIED: u32 = u32::MAX;

/// Mirror of the C header's XrShieldHit (layout fixed by
/// xr_shield_engine.h).
#[repr(C)]
pub struct XrShieldHit {
    pub action: u8,
    pub list_index: u32,
    pub rule_index: u32,
    pub redirect_resource: *const c_char,
}

/// Per-compiled-filter v1 rule metadata the engine itself does not model:
/// the option law (exact membership) and the action/resource mapping.
struct RuleMeta {
    domains: Vec<String>,
    exclude: Vec<String>,
    /// header action code for block-class hits: block 0 / redirect 2 /
    /// replace 3 (allow compiles as @@ and reports 1 via the exception).
    action_code: u8,
    /// resource name for redirect hits ("" otherwise); CString so hit_out
    /// borrows it for the engine's lifetime (header borrow contract).
    resource: CString,
}

/// The opaque handle behind `XrShieldEngine*`.
pub struct XrShieldEngine {
    engine: Engine,
    /// rule recovery table: (list_index, rule_index) per compiled filter,
    /// in FilterSet insertion order.
    rule_table: Vec<(u32, u32)>,
    /// side table parallel to rule_table: the v1 option/action law.
    meta: Vec<RuleMeta>,
    filter_index: HashMap<String, u32>, // debug raw_line -> rule index
    /// Atomic so a caught panic in any export can flip death through a
    /// shared reference (the header's alive() observable).
    alive: AtomicBool,
}

/// Rebuild ABP filter text from a normalized xr-list-bundle-v1 rule: the
/// v1 grammar is an ABP subset, so the filter body passes through; allow
/// rules become @@ exceptions. Rule OPTIONS are deliberately NOT emitted
/// as $domain= — the shim enforces v1's exact-membership law post-hit
/// (see the mapping-decisions header comment). Returns None for kinds the
/// network blocker must not see (cosmetic).
fn rule_to_filter(kind: &str, action: &str, filter: &str) -> Option<String> {
    if kind == "cosmetic" {
        return None; // the network engine ignores cosmetic rules (v1 law)
    }
    if action == "allow" {
        // exception rule: allow overrides block (native ABP semantics)
        return Some(format!("@@{filter}"));
    }
    Some(filter.to_string())
}

fn strs(rule: &serde_json::Value, key: &str) -> Vec<String> {
    rule.get(key)
        .and_then(|v| v.as_array())
        .map(|a| {
            a.iter()
                .filter_map(|x| x.as_str().map(String::from))
                .collect()
        })
        .unwrap_or_default()
}

fn build(bundle_json: *const c_char) -> Result<Box<XrShieldEngine>, &'static CStr> {
    if bundle_json.is_null() {
        return Err(c"missing-bundle");
    }
    let text = unsafe { CStr::from_ptr(bundle_json) }
        .to_str()
        .map_err(|_| c"bundle-not-utf8")?;
    let doc: serde_json::Value =
        serde_json::from_str(text).map_err(|_| c"bundle-not-json")?;
    let lists = doc.get("lists").and_then(|l| l.as_array())
        .ok_or(c"lists-not-array")?;
    // debug=true: raw_line retention is what makes rule recovery possible.
    let mut set = FilterSet::new(true);
    let opts = ParseOptions::default();
    let mut rule_table: Vec<(u32, u32)> = Vec::new();
    let mut meta: Vec<RuleMeta> = Vec::new();
    let mut filter_index: HashMap<String, u32> = HashMap::new();
    for (li, list) in lists.iter().enumerate() {
        let rules = list.get("rules").and_then(|r| r.as_array())
            .ok_or(c"rules-not-array")?;
        for (ri, rule) in rules.iter().enumerate() {
            let kind = rule.get("kind").and_then(|v| v.as_str()).unwrap_or("");
            let action =
                rule.get("action").and_then(|v| v.as_str()).unwrap_or("block");
            let filter =
                rule.get("filter").and_then(|v| v.as_str()).unwrap_or("");
            if let Some(f) = rule_to_filter(kind, action, filter) {
                // add_filter_list: the public API (add_filter is
                // #[cfg(test)]-only — run 34758097067 taught that). One
                // filter per call keeps insertion order = rule_table.
                set.add_filter_list(format!("{f}\n"), opts);
                let idx = rule_table.len() as u32;
                rule_table.push((li as u32, ri as u32));
                let resource = rule
                    .get("resource")
                    .and_then(|v| v.as_str())
                    .unwrap_or("");
                meta.push(RuleMeta {
                    domains: strs(rule, "domains"),
                    exclude: strs(rule, "exclude_domains"),
                    action_code: match action {
                        "redirect" => 2,
                        "replace" => 3,
                        _ => 0,
                    },
                    resource: CString::new(resource)
                        .unwrap_or_else(|_| CString::new("").expect("empty")),
                });
                filter_index.insert(f, idx);
            }
        }
    }
    // Network decisions only — Engine without cosmetics, without resource
    // bodies (v1 bundles carry none). The parity corpus pins the agreement
    // band against the TableEngine reference (±2% / FP ≤0.5%).
    let engine = Engine::new_with_filter_set(set);
    Ok(Box::new(XrShieldEngine {
        engine,
        rule_table,
        meta,
        filter_index,
        alive: AtomicBool::new(true),
    }))
}

#[no_mangle]
pub extern "C" fn xr_shield_engine_create(
    bundle_json: *const c_char,
    detail_out: *mut *const c_char,
) -> *mut XrShieldEngine {
    match catch_unwind(AssertUnwindSafe(|| build(bundle_json))) {
        Ok(Ok(engine)) => Box::into_raw(engine),
        Ok(Err(detail)) => {
            if !detail_out.is_null() {
                unsafe { *detail_out = detail.as_ptr().cast::<c_char>() };
            }
            ptr::null_mut()
        }
        Err(_) => {
            // a panic at create time: no engine exists, so there is no
            // alive flag to flip — the caller sees NULL (total)
            if !detail_out.is_null() {
                unsafe { *detail_out = c"engine-panic".as_ptr().cast::<c_char>() };
            }
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn xr_shield_engine_alive(engine: *const XrShieldEngine) -> c_int {
    if engine.is_null() {
        return 0;
    }
    catch_unwind(AssertUnwindSafe(|| unsafe {
        (*engine).alive.load(Ordering::SeqCst)
    }))
    .unwrap_or(false) as c_int
}

/// NUL-terminated C string -> &str ("" on NULL or non-utf8: total).
unsafe fn cstr<'a>(p: *const c_char) -> &'a str {
    if p.is_null() { "" } else { CStr::from_ptr(p).to_str().unwrap_or_default() }
}

/// The decision core, isolated so a panic anywhere inside flips alive.
/// Returns 1 with *hit_out filled on a rule hit, 0 on no opinion.
unsafe fn match_inner(
    this: &XrShieldEngine,
    match_url: *const c_char,
    host: *const c_char,
    registrable_domain: *const c_char,
    hit_out: *mut XrShieldHit,
) -> c_int {
    if !this.alive.load(Ordering::SeqCst) {
        return 0; // dead engines have no opinion (caller owns fail-open)
    }
    if match_url.is_null() {
        return 0;
    }
    let url = cstr(match_url);
    let host = cstr(host);
    let rd = cstr(registrable_domain);
    // v1 redaction: the caller passes "scheme://host/path", lowercased,
    // port/query/fragment stripped. First-party source assumption: the
    // request is its own source (no initiator rides the FFI in v1).
    let Ok(req) = Request::new(url, url, "other", "get") else {
        return 0; // unparsable URL: no opinion (deterministic refusal)
    };
    let res = this.engine.check_network_request(&req);
    // Decision precedence mirrors shield/core/fake_engine.h:
    //   block filter hit, not excepted  -> block-class hit (action 0/2)
    //   block filter hit AND exception  -> kAllowHit (action 1)
    //   no filter hit                   -> no opinion (0)
    // should_block() = important || (filter.is_some() && exception.is_none())
    let (block_class, info) = if res.should_block() {
        (true, res.filter.as_ref())
    } else if res.filter.is_some() && res.exception.is_some() {
        (false, res.exception.as_ref())
    } else {
        return 0;
    };
    // Rule recovery: debug raw_line -> filter_index -> rule_table/meta.
    let idx = info
        .and_then(|d| d.raw_line.as_deref())
        .and_then(|raw| this.filter_index.get(raw).copied());
    let (li, ri) = idx
        .and_then(|i| this.rule_table.get(i as usize).copied())
        .unwrap_or((UNIDENTIFIED, UNIDENTIFIED));
    if let Some(i) = idx {
        if let Some(m) = this.meta.get(i as usize) {
            // v1 option law (fake_engine.h): domains/exclude_domains are
            // EXACT set membership against the request's registrable_domain
            // or host. A rule whose options do not hold does not apply —
            // for a block hit that is "no opinion from THIS rule", and the
            // parity corpus pins single-rule cases where that is exact
            // (multi-rule interaction: divergence class D-4, documented).
            let in_set = |set: &[String]| {
                set.iter().any(|d| d == rd || d == host)
            };
            if (!m.domains.is_empty() && !in_set(&m.domains))
                || (!m.exclude.is_empty() && in_set(&m.exclude))
            {
                return 0;
            }
            if !hit_out.is_null() {
                // allow -> 1; block-class -> v1 action code (D-1 mapping)
                let action = if !block_class { 1u8 } else { m.action_code };
                (*hit_out).action = action;
                (*hit_out).list_index = li;
                (*hit_out).rule_index = ri;
                (*hit_out).redirect_resource = if action == 2 {
                    m.resource.as_ptr()
                } else {
                    c"".as_ptr().cast::<c_char>()
                };
                return 1;
            }
            return 1;
        }
    }
    // Matched but unidentified (fused filter text): report the hit with
    // the sentinel indices; the C++ adapter bounds-checks and leaves
    // rule_id empty. Options cannot be enforced on an unidentified rule —
    // accepted and documented (D-4); the parity corpus is single-rule,
    // where fusion cannot occur.
    if !hit_out.is_null() {
        (*hit_out).action = if block_class { 0 } else { 1 };
        (*hit_out).list_index = li;
        (*hit_out).rule_index = ri;
        (*hit_out).redirect_resource = c"".as_ptr().cast::<c_char>();
    }
    1
}

#[no_mangle]
pub extern "C" fn xr_shield_engine_match(
    engine: *const XrShieldEngine,
    match_url: *const c_char,
    host: *const c_char,
    path: *const c_char,
    registrable_domain: *const c_char,
    hit_out: *mut XrShieldHit,
) -> c_int {
    // path rides the redacted match_url; v1 options key on rd/host only.
    let _ = path;
    if engine.is_null() {
        return 0;
    }
    let this = unsafe { &*engine };
    match catch_unwind(AssertUnwindSafe(|| unsafe {
        match_inner(this, match_url, host, registrable_domain, hit_out)
    })) {
        Ok(v) => v,
        Err(_) => {
            // caught panic => engine death is REPRESENTABLE: flip alive so
            // the posture core fail-opens (amber), per the header contract.
            this.alive.store(false, Ordering::SeqCst);
            0
        }
    }
}

#[no_mangle]
pub extern "C" fn xr_shield_engine_free(engine: *mut XrShieldEngine) {
    if engine.is_null() {
        return;
    }
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        drop(Box::from_raw(engine));
    }));
}

#[no_mangle]
pub extern "C" fn xr_shield_engine_kill_for_test(engine: *mut XrShieldEngine) {
    if engine.is_null() {
        return;
    }
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        (*engine).alive.store(false, Ordering::SeqCst);
    }));
}
