// Copyright 2026 RRRTX Labs
// Use of this source code is governed by the MPL-2.0 license that can be
// found in the LICENSE file.
//
// Intent: the replay seen-set. Replayed (previously-seen) manifests are
// refused. Durability follows the P6 store law, reused verbatim:
// write-tmp -> fsync -> rename, so a kill mid-write can never produce a
// partial store (the T0-b drill re-proves this across the process
// boundary). Disposable sessions (no store dir) keep the set in memory and
// write ZERO bytes — P8's filesystem-diff assertion covers it.
#pragma once

#include <deque>
#include <set>
#include <string>

#include "update/core/json.h"

namespace xr::update {

class SeenSet {
 public:
  static constexpr size_t kMaxEntries = 4096;  // FIFO trim, bounded file

  explicit SeenSet(std::string store_dir);  // "" => disposable, memory only

  bool Contains(const std::string& manifest_id) const;
  // Insert + persist (durable mode). Returns false only on a persistence
  // failure — the caller must refuse an update it could not record
  // (otherwise the next process would accept the replay).
  bool Insert(const std::string& manifest_id, std::string* error);

  // Load from disk. A corrupt file is PRESERVED and reported (deny-preserve:
  // never silently rewritten or dropped); the caller then runs with an empty
  // in-memory set and refuses updates rather than trusting a torn store.
  bool Load(std::string* error);

  size_t size() const { return ids_.size(); }
  bool disposable() const { return store_dir_.empty(); }
  static std::string PathFor(const std::string& store_dir);

 private:
  std::string store_dir_;
  std::deque<std::string> order_;
  std::set<std::string> ids_;
};

}  // namespace xr::update
