// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include "video_core/nx2_debug_control.h"
#include "video_core/page_filter.h"
#include "common/container/unordered_map.h"
#include <utility>

#include "common/assert.h"
#include "common/bit_field.h"
#include "common/common_types.h"
#include "video_core/control/channel_state_cache.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/query_cache/query_base.h"
#include "video_core/query_cache/types.h"

namespace VideoCore {
class RasterizerInterface;
}

namespace Tegra {
class GPU;
}

namespace VideoCommon {

struct LookupData {
    VAddr address;
    QueryBase* found_query;
};

template <typename Traits>
class QueryCacheBase : public VideoCommon::ChannelSetupCaches<VideoCommon::ChannelInfo> {
    using RuntimeType = typename Traits::RuntimeType;

public:
    /// Device pages this cache ever registered objects on (InnerInvalidation skips the rest).
    VideoCommon::PageFilter page_filter;

    union QueryLocation {
        BitField<27, 5, u32> stream_id;
        BitField<0, 27, u32> query_id;
        u32 raw;

        std::pair<size_t, size_t> unpack() const {
            return {static_cast<size_t>(stream_id.Value()), static_cast<size_t>(query_id.Value())};
        }
    };

    explicit QueryCacheBase(Tegra::GPU& gpu, VideoCore::RasterizerInterface& rasterizer_,
                            Tegra::MaxwellDeviceMemoryManager& device_memory_,
                            RuntimeType& runtime_);

    ~QueryCacheBase();

    void InvalidateRegion(VAddr addr, std::size_t size) {
        IterateCache<true>(addr, size,
                           [this](QueryLocation location) { InvalidateQuery(location); });
    }

    void FlushRegion(VAddr addr, std::size_t size) {
        if (SemiFlushRegion(addr, size)) {
            RequestGuestHostSync();
        }
    }

    /// Writes out the finalized reports in the region. True when some still wait for host results.
    bool SemiFlushRegion(VAddr addr, std::size_t size) {
        bool result = false;
        // Visits every query in the range: each final value not written yet is written here.
        IterateCache<false>(addr, size, [this, &result](QueryLocation location) {
            result |= SemiFlushQueryDirty(location);
        });
        return result;
    }

    static u64 BuildMask(std::span<const QueryType> types) {
        u64 mask = 0;
        for (auto query_type : types) {
            mask |= 1ULL << (static_cast<u64>(query_type));
        }
        return mask;
    }

    /// Whether a report in the region is still listed (pending host results, or written and not
    /// unregistered yet). Lock-free via atomic page bloom when clean under EXP65; falls back to
    /// the cache mutex otherwise. Callable from the split front.
    [[nodiscard]] bool HasCachedReports(VAddr addr, size_t size) {
        if (size == 0) {
            return false;
        }
        if (VideoCore::Nx2Debug::Experiment(65) != 0) {
            if (cached_query_count.load(std::memory_order_acquire) == 0) {
                return false;
            }
            const u64 page_begin = addr >> Core::DEVICE_PAGEBITS;
            const u64 page_end = (addr + size - 1) >> Core::DEVICE_PAGEBITS;
            bool any_page = page_end < page_begin || (page_end - page_begin) >= PageBloomBits;
            if (!any_page) {
                for (u64 page = page_begin; page <= page_end; ++page) {
                    const size_t h = PageBloomIndex(page);
                    if ((cached_query_pages[h >> 6].load(std::memory_order_acquire) &
                         (u64{1} << (h & 63))) != 0) {
                        any_page = true;
                        break;
                    }
                }
            }
            if (!any_page) {
                return false;
            }
        }
        bool result = false;
        IterateCache<false>(addr, size, [&result](QueryLocation) { result = true; });
        return result;
    }

    /// Return true when a CPU region is modified from the GPU
    [[nodiscard]] bool IsRegionGpuModified(VAddr addr, size_t size) {
        bool result = false;
        IterateCache<false>(addr, size, [this, &result](QueryLocation location) {
            result |= IsQueryDirty(location);
            return result;
        });
        return result;
    }

    void CounterEnable(QueryType counter_type, bool is_enabled);

    void CounterReset(QueryType counter_type);

    void CounterClose(QueryType counter_type);

    void CounterReport(GPUVAddr addr, QueryType counter_type, QueryPropertiesFlags flags,
                       u32 payload, u32 subreport);

    void NotifyWFI();

    bool AccelerateHostConditionalRendering();

    // Async downloads
    void CommitAsyncFlushes();

    bool HasUncommittedFlushes() const;

    /// Whether a new fence has queries to flush or host writes to sync for this cache.
    /// `ignore_payload_sync` (EDEN_NX2_EXP28 & 1, zortos293/Lichii#717 FENCE_TRIM): payload
    /// releases' host buffer syncs don't count; they wait for a later fence or wait for idle.
    bool HasPendingFenceWork(bool ignore_payload_sync = false) const;

    /// EXP28 & 1: a fence was skipped although payload releases wait for their host sync.
    void NotePayloadSyncDeferred();

    bool ShouldWaitAsyncFlushes();

    void PopAsyncFlushes();

    void NotifySegment(bool resume);

    void BindToChannel(s32 id) override;

protected:
    template <bool remove_from_cache, typename Func>
    void IterateCache(VAddr addr, std::size_t size, Func&& func) {
        // invoke_result_t: with invoke_result (the trait, never bool) callbacks returning bool
        // never ended the walk early.
        static constexpr bool RETURNS_BOOL =
            std::is_same_v<std::invoke_result_t<Func, QueryLocation>, bool>;
        const u64 addr_begin = addr;
        const u64 addr_end = addr_begin + size;

        const u64 page_end = addr_end >> Core::DEVICE_PAGEBITS;
        std::scoped_lock lock(cache_mutex);
        for (u64 page = addr_begin >> Core::DEVICE_PAGEBITS; page <= page_end; ++page) {
            const u64 page_start = page << Core::DEVICE_PAGEBITS;
            const auto in_range = [page_start, addr_begin, addr_end](const u32 query_location) {
                const u64 cache_begin = page_start + query_location;
                const u64 cache_end = cache_begin + sizeof(u32);
                return cache_begin < addr_end && addr_begin < cache_end;
            };
            const auto& it = cached_queries.find(page);
            if (it == std::end(cached_queries)) {
                continue;
            }
            auto& contents = it->second;
            bool matched = false;
            for (auto& query : contents) {
                if (!in_range(query.first)) {
                    continue;
                }
                matched = true;
                if constexpr (RETURNS_BOOL) {
                    if (func(query.second)) {
                        return;
                    }
                } else {
                    func(query.second);
                }
            }
            // Only pages with a query in the range: the scan above already found none elsewhere.
            if constexpr (remove_from_cache) {
                if (!matched) {
                    continue;
                }
                const auto in_range2 = [&](const std::pair<u32, QueryLocation>& pair) {
                    return in_range(pair.first);
                };
                const size_t before = contents.size();
                erase_if(contents, in_range2);
                if (const size_t removed = before - contents.size(); removed > 0) {
                    NoteQueryRemoved(page, static_cast<u32>(removed));
                }
            }
        }
    }

    using ContentCache = ::Common::unordered_map<u64, ::Common::unordered_map<u32, QueryLocation>>;

    void InvalidateQuery(QueryLocation location);
    bool IsQueryDirty(QueryLocation location);
    bool SemiFlushQueryDirty(QueryLocation location);
    void RequestGuestHostSync();
    /// Before a report's bytes go to guest memory: a download for a guest read still on its way
    /// would write the GPU's older copy over them later (as MemoryManager::Write; EXP49).
    void FinishReads(DAddr address, size_t size);
    void UnregisterPending();

    static constexpr size_t PageBloomBits = 2048;
    static constexpr size_t PageBloomWords = PageBloomBits / 64;
    [[nodiscard]] static constexpr size_t PageBloomIndex(u64 page) noexcept {
        return static_cast<size_t>((page ^ (page >> 12)) & (PageBloomBits - 1));
    }
    void NoteQueryAdded(u64 page) noexcept {
        const size_t h = PageBloomIndex(page);
        if (++cached_query_page_counts[h] == 1) {
            cached_query_pages[h >> 6].fetch_or(u64{1} << (h & 63), std::memory_order_release);
        }
        cached_query_count.fetch_add(1, std::memory_order_release);
    }
    void NoteQueryRemoved(u64 page, u32 count = 1) noexcept {
        cached_query_count.fetch_sub(count, std::memory_order_release);
        const size_t h = PageBloomIndex(page);
        cached_query_page_counts[h] -= count;
        if (cached_query_page_counts[h] == 0) {
            cached_query_pages[h >> 6].fetch_and(~(u64{1} << (h & 63)), std::memory_order_release);
        }
    }

    std::atomic<u32> cached_query_count{};
    std::array<std::atomic<u64>, PageBloomWords> cached_query_pages{};
    std::array<u32, PageBloomBits> cached_query_page_counts{};
    ::Common::unordered_map<u64, ::Common::unordered_map<u32, QueryLocation>> cached_queries;
    std::mutex cache_mutex;

    struct QueryCacheBaseImpl;
    friend struct QueryCacheBaseImpl;
    friend RuntimeType;

    std::unique_ptr<QueryCacheBaseImpl> impl;
};

} // namespace VideoCommon
