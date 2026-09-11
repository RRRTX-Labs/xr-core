// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: shield/engine — the Rust side of the C ABI in
// xr_shield_engine.h: the vendored adblock-rust engine behind the SAME
// BlockingEngine contract the C++ core injects (P11-T2 skeleton; the
// parity-measured binding is T8).
//
// STATUS: HOSTED-LANE SKELETON. This crate builds with cargo in the
// core-hardening shield-vendor job against //xr/third_party/rust (source
// replacement, offline). There is no local cargo build; nothing in-tree
// claims this file compiles today. The v1 grammar mapping below mirrors
// shield/core/fake_engine.h semantics; T8's parity job
// (xr-browser tools/shield_parity.py, >=1,500-case corpus, ±2% agreement /
// FP <=0.5%) is the gate that turns "mirrors" into "measured".
//
// Laws honored here: total (every export catches panics — a panic flips
// alive to false, the posture core fail-OPENS), deterministic (no clock,
// no RNG, no I/O), allow-overrides-block (adblock-rust's native exception
// semantics — the same rule the TableEngine implements).

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

use adblock::blocker::Blocker;
use adblock::lists::FilterSet;

/// Mirror of the C header's XrShieldHit (layout fixed by
/// xr_shield_engine.h; T8 may swap in bindgen in the hosted lane).
#[repr(C)]
pub struct XrShieldHit {
    pub action: u8,
    pub list_index: u32,
    pub rule_index: u32,
    pub redirect_resource: *const c_char,
}

/// The opaque handle behind `XrShieldEngine*`.
pub struct XrShieldEngine {
    blocker: Blocker,
    /// rule recovery table: (list_index, rule_index) per compiled filter,
    /// in FilterSet insertion order — the C++ side maps a matched filter
    /// back to rule_id/list_id through this table.
    rule_table: Vec<(u32, u32)>,
    /// resources for redirect/replace hits (index-parallel to rule_table)
    resources: Vec<String>,
    alive: bool,
    /// keeps the borrowed XrShieldHit.redirect_resource alive until the
    /// next match call or free (the C header's borrow contract)
    last_resource: CString,
}

/// Rebuild ABP filter text from a normalized xr-list-bundle-v1 rule: the
/// v1 grammar is an ABP subset, so the filter body passes through; the
/// structured options become $domain=… / exception prefixes. Returns None
/// for kinds the network blocker must not see (cosmetic).
fn rule_to_filter(kind: &str, action: &str, filter: &str, domains: &[String],
                  exclude: &[String]) -> Option<String> {
    if kind == "cosmetic" {
        return None; // the network engine ignores cosmetic rules (v1 law)
    }
    let mut out = String::new();
    if action == "allow" {
        out.push_str("@@"); // exception rule: allow overrides block
    }
    out.push_str(filter);
    let mut opts: Vec<String> = Vec::new();
    if !domains.is_empty() {
        opts.push(format!("domain={}", domains.join("|")));
    }
    if !exclude.is_empty() {
        let negated: Vec<String> =
            exclude.iter().map(|d| format!("~{d}")).collect();
        opts.push(format!("domain={}", negated.join("|")));
    }
    if !opts.is_empty() {
        out.push('$');
        out.push_str(&opts.join(","));
    }
    Some(out)
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
    let mut set = FilterSet::new(false);
    let mut rule_table: Vec<(u32, u32)> = Vec::new();
    let mut resources: Vec<String> = Vec::new();
    for (li, list) in lists.iter().enumerate() {
        let rules = list.get("rules").and_then(|r| r.as_array())
            .ok_or(c"rules-not-array")?;
        for (ri, rule) in rules.iter().enumerate() {
            let kind = rule.get("kind").and_then(|v| v.as_str()).unwrap_or("");
            let action =
                rule.get("action").and_then(|v| v.as_str()).unwrap_or("block");
            let filter =
                rule.get("filter").and_then(|v| v.as_str()).unwrap_or("");
            let strs = |key: &str| -> Vec<String> {
                rule.get(key)
                    .and_then(|v| v.as_array())
                    .map(|a| {
                        a.iter()
                            .filter_map(|x| x.as_str().map(String::from))
                            .collect()
                    })
                    .unwrap_or_default()
            };
            let domains = strs("domains");
            let exclude = strs("exclude_domains");
            if let Some(f) =
                rule_to_filter(kind, action, filter, &domains, &exclude)
            {
                set.add_filter(&f);
                rule_table.push((li as u32, ri as u32));
                resources.push(
                    rule.get("resource")
                        .and_then(|v| v.as_str())
                        .unwrap_or("")
                        .to_string(),
                );
            }
        }
    }
    // T8: the Blocker is built WITHOUT cosmetics (network decisions only);
    // the parity corpus pins the agreement band against the TableEngine
    // reference (±2% / FP ≤0.5% on ≥1,500 cases).
    let blocker = Blocker::new(set, Default::default());
    Ok(Box::new(XrShieldEngine {
        blocker,
        rule_table,
        resources,
        alive: true,
        last_resource: CString::new("").expect("empty CString"),
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
    catch_unwind(AssertUnwindSafe(|| unsafe { (*engine).alive }))
        .unwrap_or(false) as c_int
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
    // T8 completes the request mapping (adblock::Request from the
    // caller-redacted parts, third-party resolution via the resolver
    // snapshot, redirect resource lookup, rule_table recovery). The
    // skeleton refuses to guess: an unfinished binding that returns "no
    // opinion" (0) is honest; one that returns half-matched blocks is not.
    let _ = (engine, match_url, host, path, registrable_domain, hit_out);
    0
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
        (*engine).alive = false;
    }));
}
