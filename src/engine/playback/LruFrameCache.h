// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QSize>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

namespace vedit::engine {

enum class VideoPixelFormat {
    Rgba8888,
    Nv12,
    P010
};

struct GpuFramePlane {
    QByteArray data;
    const uint8_t *rawBytes = nullptr;
    int stride = 0;
    int width = 0;
    int height = 0;

    const uint8_t *constData() const {
        return rawBytes ? rawBytes : reinterpret_cast<const uint8_t *>(data.constData());
    }

    int size() const {
        return rawBytes ? (stride * height) : data.size();
    }
};

// Represents a decoded video frame. Holds either RGB32/RGBA8888 or native UMA hardware planes (NV12, P010).
// Employs a custom lifecycle token to ensure controlled release and UMA memory safety.
class VideoFrame {
public:
    VideoFrame() = default;

    // Construct from QImage (RGBA8888 / RGB32)
    explicit VideoFrame(const QImage &image, int position = -1, int64_t pts = 0)
        : m_format(VideoPixelFormat::Rgba8888)
        , m_image(image)
        , m_size(image.size())
        , m_position(position)
        , m_pts(pts)
    {
    }

    // Construct from native semi-planar hardware format (NV12 or P010)
    VideoFrame(VideoPixelFormat format, QSize size, GpuFramePlane yPlane, GpuFramePlane uvPlane,
               int position = -1, int64_t pts = 0, std::shared_ptr<void> lifecycleToken = nullptr)
        : m_format(format)
        , m_size(size)
        , m_planes{std::move(yPlane), std::move(uvPlane)}
        , m_position(position)
        , m_pts(pts)
        , m_lifecycleToken(std::move(lifecycleToken))
    {
    }

    bool isNull() const {
        return m_format == VideoPixelFormat::Rgba8888 ? m_image.isNull() : m_size.isEmpty();
    }

    VideoPixelFormat format() const { return m_format; }
    QSize size() const { return m_size; }
    int width() const { return m_size.width(); }
    int height() const { return m_size.height(); }
    int position() const { return m_position; }
    int64_t pts() const { return m_pts; }

    const QImage &image() const { return m_image; }
    const GpuFramePlane &planeY() const { return m_planes[0]; }
    const GpuFramePlane &planeUV() const { return m_planes[1]; }

    // Fallback CPU conversion for software rendering surfaces or thumbnails
    QImage toImage() const;

private:
    VideoPixelFormat m_format = VideoPixelFormat::Rgba8888;
    QImage m_image;
    QSize m_size;
    GpuFramePlane m_planes[2];
    int m_position = -1;
    int64_t m_pts = 0;
    std::shared_ptr<void> m_lifecycleToken;
};

// Thread-safe Bounded LRU Cache for scrubbing frames (SPEC §5.3, §6 and FASE 2)
// Default capacity: 24 frames (bounded strictly between 15 and 30 frames to protect UMA system RAM).
template <typename Key = int64_t, typename Value = VideoFrame>
class BoundedLruCache {
public:
    explicit BoundedLruCache(size_t maxCapacity = 24)
        : m_capacity(std::clamp<size_t>(maxCapacity, 15, 30))
    {
    }

    size_t capacity() const {
        QMutexLocker lock(&m_mutex);
        return m_capacity;
    }

    void setCapacity(size_t cap) {
        QMutexLocker lock(&m_mutex);
        m_capacity = std::clamp<size_t>(cap, 15, 30);
        trim_locked();
    }

    size_t size() const {
        QMutexLocker lock(&m_mutex);
        return m_map.size();
    }

    bool contains(const Key &key) const {
        QMutexLocker lock(&m_mutex);
        return m_map.find(key) != m_map.end();
    }

    std::optional<Value> get(const Key &key) {
        QMutexLocker lock(&m_mutex);
        auto it = m_map.find(key);
        if (it == m_map.end()) {
            ++m_misses;
            return std::nullopt;
        }
        // Move to front of LRU list (most recently used)
        m_list.splice(m_list.begin(), m_list, it->second.listIter);
        ++m_hits;
        return it->second.value;
    }

    void insert(const Key &key, Value value) {
        QMutexLocker lock(&m_mutex);
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            it->second.value = std::move(value);
            m_list.splice(m_list.begin(), m_list, it->second.listIter);
            return;
        }
        m_list.push_front(key);
        m_map[key] = Entry{std::move(value), m_list.begin()};
        trim_locked();
    }

    void clear() {
        QMutexLocker lock(&m_mutex);
        m_map.clear();
        m_list.clear();
    }

    uint64_t hits() const { return m_hits.load(); }
    uint64_t misses() const { return m_misses.load(); }
    uint64_t evictions() const { return m_evictions.load(); }

private:
    struct Entry {
        Value value;
        typename std::list<Key>::iterator listIter;
    };

    void trim_locked() {
        while (m_map.size() > m_capacity && !m_list.empty()) {
            Key oldestKey = m_list.back();
            m_list.pop_back();
            m_map.erase(oldestKey);
            ++m_evictions;
        }
    }

    mutable QMutex m_mutex;
    size_t m_capacity;
    std::list<Key> m_list;
    std::unordered_map<Key, Entry> m_map;
    std::atomic<uint64_t> m_hits{0};
    std::atomic<uint64_t> m_misses{0};
    std::atomic<uint64_t> m_evictions{0};
};

using LruFrameCache = BoundedLruCache<int64_t, VideoFrame>;

} // namespace vedit::engine
