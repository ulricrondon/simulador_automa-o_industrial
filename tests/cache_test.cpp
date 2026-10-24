#include "sim/runtime/asset_cache.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <type_traits>
using namespace sim::runtime;
namespace {
std::string key(char digit) { return std::string(64, digit); }
AssetData bytes(std::size_t size) { return AssetData(size, std::byte{42}); }
}
TEST(Cache, BudgetEvictsCheapEntryFromColdWindow) {
    AssetCache cache{8, 16, 2};
    cache.put(key('a'), bytes(4), 100);
    cache.put(key('b'), bytes(4), 1);
    cache.put(key('c'), bytes(4), 10);
    EXPECT_EQ(cache.stats().retained_bytes, 8);
    EXPECT_FALSE(cache.get(key('b')));
    EXPECT_TRUE(cache.get(key('a')));
    EXPECT_TRUE(cache.get(key('c')));
}
TEST(Cache, LruAccessProtectsRecentlyUsedEntry) {
    AssetCache cache{8, 16, 1};
    cache.put(key('a'), bytes(4), 1);
    cache.put(key('b'), bytes(4), 1);
    cache.get(key('a'));
    cache.put(key('c'), bytes(4), 1);
    EXPECT_FALSE(cache.get(key('b')));
    EXPECT_TRUE(cache.get(key('a')));
}
TEST(Cache, EvictionPreservesExternalOwnershipAndWeakRecovery) {
    AssetCache cache{4};
    const auto original = cache.put(key('a'), bytes(4), 1);
    cache.put(key('b'), bytes(4), 1);
    EXPECT_EQ(original->size(), 4);
    EXPECT_EQ(cache.get(key('a')), original);
    EXPECT_LE(cache.stats().retained_bytes, 4);
}
TEST(Cache, OversizeAssetIsWeakOnlyAndExpires) {
    AssetCache cache{2};
    auto external = cache.put(key('a'), bytes(4), 1);
    EXPECT_EQ(cache.stats().retained_bytes, 0);
    EXPECT_EQ(cache.get(key('a')), external);
    external.reset();
    EXPECT_FALSE(cache.get(key('a')));
    EXPECT_EQ(cache.stats().metadata_entries, 0);
}
TEST(Cache, MetadataIsBoundedEvenWhenClientsKeepAssetsAlive) {
    AssetCache cache{0, 2};
    std::vector<AssetPtr> held;
    for (char c : std::string("abcdef")) { held.push_back(cache.put(key(c), bytes(1), 1)); }
    EXPECT_EQ(cache.stats().metadata_entries, 2);
    EXPECT_EQ(cache.stats().retained_bytes, 0);
    EXPECT_EQ(held.front()->size(), 1);
}
TEST(Cache, BudgetReductionReleasesRetentionButNotClients) {
    AssetCache cache{100};
    auto external = cache.put(key('a'), bytes(10), 1);
    cache.put(key('b'), bytes(10), 1);
    cache.set_budget(0);
    EXPECT_EQ(cache.stats().retained_bytes, 0);
    EXPECT_EQ(cache.get(key('a')), external);
    EXPECT_FALSE(cache.get(key('b')));
}
TEST(Cache, ImmutableIdentityAndConflictingContent) {
    static_assert(std::is_const_v<AssetPtr::element_type>);
    AssetCache cache{100};
    const auto first = cache.put(key('a'), bytes(4), 1);
    EXPECT_EQ(cache.put(key('a'), bytes(4), 2), first);
    EXPECT_THROW(cache.put(key('a'), bytes(5), 1), std::invalid_argument);
    EXPECT_THROW(cache.put("invalid", bytes(4), 1), std::invalid_argument);
}
TEST(Cache, ConcurrentReadersAndPublishersKeepBudget) {
    AssetCache cache{16};
    std::vector<std::jthread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&cache, i] {
            const auto digest = key(static_cast<char>('a' + i));
            for (int n = 0; n < 200; ++n) {
                const auto value = cache.put(digest, bytes(8), 1);
                EXPECT_EQ(value->size(), 8);
                const auto found = cache.get(digest);
                ASSERT_TRUE(found);
                EXPECT_EQ(found, value);
            }
        });
    }
    workers.clear();
    EXPECT_LE(cache.stats().retained_bytes, 16);
}
