#include "sim/runtime/asset_cache.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace sim::runtime {
namespace {
void check_digest(const std::string& digest) {
    if (digest.size() != 64 || !std::all_of(digest.begin(), digest.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        })) {
        throw std::invalid_argument("Expected normalized lowercase SHA-256 digest");
    }
}
}
AssetCache::AssetCache(std::size_t budget_bytes, std::size_t max_entries, std::size_t cold_window)
    : budget_(budget_bytes), max_entries_(max_entries), cold_window_(cold_window) {
    if (max_entries == 0 || cold_window == 0) {
        throw std::invalid_argument("Cache metadata limit and cold window must be positive");
    }
}
std::uint64_t AssetCache::next_access() {
    if (clock_ == std::numeric_limits<std::uint64_t>::max()) {
        std::vector<Entries::iterator> order;
        for (auto it = entries_.begin(); it != entries_.end(); ++it) { order.push_back(it); }
        std::sort(order.begin(), order.end(), [](auto a, auto b) {
            return std::tie(a->second.access, a->first) < std::tie(b->second.access, b->first);
        });
        clock_ = 0;
        for (auto it : order) { it->second.access = ++clock_; }
    }
    return ++clock_;
}
void AssetCache::trim(std::size_t target) {
    while (retained_bytes_ > target) {
        std::vector<Entries::iterator> cold;
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.retained) { cold.push_back(it); }
        }
        std::sort(cold.begin(), cold.end(), [](auto a, auto b) {
            return std::tie(a->second.access, a->first) < std::tie(b->second.access, b->first);
        });
        if (cold.size() > cold_window_) { cold.resize(cold_window_); }
        const auto victim = *std::min_element(cold.begin(), cold.end(), [](auto a, auto b) {
            return std::tie(a->second.cost, a->second.access, a->first)
                 < std::tie(b->second.cost, b->second.access, b->first);
        });
        retained_bytes_ -= victim->second.bytes;
        victim->second.retained.reset();
        ++evictions_;
    }
}
void AssetCache::reserve_metadata() {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.weak.expired()) { it = entries_.erase(it); } else { ++it; }
    }
    if (entries_.size() < max_entries_) { return; }
    const auto victim = std::min_element(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
        // Forget unretained metadata first, then the least recently used entry.
        return std::tuple{bool(a.second.retained), a.second.access, a.first}
             < std::tuple{bool(b.second.retained), b.second.access, b.first};
    });
    if (victim->second.retained) {
        retained_bytes_ -= victim->second.bytes;
        ++evictions_;
    }
    entries_.erase(victim);
}
void AssetCache::retain(Entry& entry, const AssetPtr& data) {
    if (entry.retained || entry.bytes > budget_ || budget_ == 0) { return; }
    trim(budget_ - entry.bytes);
    entry.retained = data;
    retained_bytes_ += entry.bytes;
}
AssetPtr AssetCache::put(std::string digest, AssetData data, std::uint32_t rebuild_cost) {
    check_digest(digest);
    std::lock_guard lock(mutex_);
    if (auto it = entries_.find(digest); it != entries_.end()) {
        if (auto existing = it->second.weak.lock()) {
            if (*existing != data) { throw std::invalid_argument("Conflicting content for asset digest"); }
            it->second.access = next_access();
            retain(it->second, existing);
            return existing;
        }
        entries_.erase(it);
    }
    // Construct immutable storage internally so callers cannot retain a mutable vector alias.
    const auto payload = std::make_shared<const AssetData>(std::move(data));
    reserve_metadata();
    auto [it, inserted] = entries_.emplace(std::move(digest),
        Entry{payload, {}, payload->size(), rebuild_cost, next_access()});
    (void)inserted;
    retain(it->second, payload);
    return payload;
}
AssetPtr AssetCache::get(const std::string& digest) {
    check_digest(digest);
    std::lock_guard lock(mutex_);
    const auto it = entries_.find(digest);
    if (it == entries_.end()) { ++misses_; return {}; }
    auto result = it->second.weak.lock();
    if (!result) {
        entries_.erase(it);
        ++misses_;
        return {};
    }
    ++hits_;
    it->second.access = next_access();
    retain(it->second, result);
    return result;
}
void AssetCache::set_budget(std::size_t bytes) {
    std::lock_guard lock(mutex_);
    budget_ = bytes;
    trim(bytes);
    if (bytes == 0) {
        // Also release zero-byte payloads.
        for (auto& [key, entry] : entries_) {
            (void)key;
            if (entry.retained) { entry.retained.reset(); ++evictions_; }
        }
    }
}
CacheStats AssetCache::stats() const {
    std::lock_guard lock(mutex_);
    return {retained_bytes_, entries_.size(), hits_, misses_, evictions_};
}
}
