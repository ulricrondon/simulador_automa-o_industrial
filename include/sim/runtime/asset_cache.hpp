#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sim::runtime {
using AssetData = std::vector<std::byte>;
using AssetPtr = std::shared_ptr<const AssetData>;
struct CacheStats {
    std::size_t retained_bytes{};
    std::size_t metadata_entries{};
    std::uint64_t hits{};
    std::uint64_t misses{};
    std::uint64_t evictions{};
};
// CPU payload cache only. Caller supplies a SHA-256 already verified by the loader.
// No Qt/VTK/GPU resources may be stored here.
class AssetCache {
public:
    explicit AssetCache(std::size_t budget_bytes, std::size_t max_entries = 1024,
                        std::size_t cold_window = 4);
    AssetPtr put(std::string digest, AssetData data, std::uint32_t rebuild_cost);
    AssetPtr get(const std::string& digest);
    void set_budget(std::size_t bytes);
    CacheStats stats() const;
private:
    struct Entry {
        std::weak_ptr<const AssetData> weak;
        AssetPtr retained;
        std::size_t bytes{};
        std::uint32_t cost{};
        std::uint64_t access{};
    };
    using Entries = std::map<std::string, Entry>;
    void trim(std::size_t target);
    void reserve_metadata();
    void retain(Entry& entry, const AssetPtr& data);
    std::uint64_t next_access();
    mutable std::mutex mutex_;
    Entries entries_;
    std::size_t budget_;
    const std::size_t max_entries_;
    const std::size_t cold_window_;
    std::size_t retained_bytes_{};
    std::uint64_t clock_{};
    std::uint64_t hits_{};
    std::uint64_t misses_{};
    std::uint64_t evictions_{};
};
}
