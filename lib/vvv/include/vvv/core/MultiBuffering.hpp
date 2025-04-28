//  Copyright (C) 2024, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <memory>
#include <utility>

#include "preamble_forward_decls.hpp"

#include <vvv/util/Logger.hpp>

namespace vvv {

using ResourceId = uint32_t;
using BufferCopyId = uint32_t;

/// Stores number of copies and current index. Is referenced by WithMultiBuffering for tracing current state of e.g. frames in flight
class MultiBuffering {
  public:
    explicit MultiBuffering(BufferCopyId copies) : m_count(copies), m_maxIdx(copies - 1), m_currIdx(0) {
        assert(copies > 0);
        assert(copies <= 31); // we use a uint32_t to track pending updates to multibuffered data. Shifts with 32 are undefined, so we exclude this case to avoid a special case.

        m_resourcesToKeepAlive.resize(m_count);
    }

    void setActiveIndex(BufferCopyId idx) { m_currIdx = idx; }
    void incrementIndex() {
        m_currIdx = getNextIndex();
    }

    [[nodiscard]] BufferCopyId getActiveIndex() const { return m_currIdx; }
    [[nodiscard]] BufferCopyId getPreviousIndex() const { return (m_currIdx + m_count - 1) % m_count; }
    [[nodiscard]] BufferCopyId getNextIndex() const { return (m_currIdx + 1) % m_count; }
    [[nodiscard]] BufferCopyId getMaxIndex() const { return m_maxIdx; }
    [[nodiscard]] BufferCopyId getIndexCount() const { return m_count; }

    /// will keep the resource alive until this copy is done. Make sure that the owner of this MultiBuffering is calling cleanKeepAlives() in the correct place.
    void keepAlive(std::shared_ptr<void> resource) {
        if (m_resourcesToKeepAlive[m_currIdx].size() >= 50)
            Logger(Warn) << "MultiBuffering::keepAlive(): more than 50 resources are kept alive for this frame. Please check that cleanKeepAlives() is called on this MultiBuffering!";
        m_resourcesToKeepAlive[m_currIdx].emplace_back(std::move(resource));
    }

    void cleanKeepAlives(BufferCopyId idx) {
        m_resourcesToKeepAlive[idx].clear();
    }

  private:
    BufferCopyId m_count;
    BufferCopyId m_maxIdx;
    BufferCopyId m_currIdx;

    std::vector<std::vector<std::shared_ptr<void>>> m_resourcesToKeepAlive;
};

const std::shared_ptr<MultiBuffering> NoMultiBuffering = std::make_shared<MultiBuffering>(1);

/// inherit from this to track multi buffering state. State is contained in MultiBuffering
class WithMultiBuffering {
  protected:
    explicit WithMultiBuffering(std::shared_ptr<MultiBuffering> m) : m_multiBuffering(std::move(m)) {}

  public:
    [[nodiscard]] BufferCopyId getActiveIndex() const { return m_multiBuffering->getActiveIndex(); }
    [[nodiscard]] BufferCopyId getPreviousIndex() const { return m_multiBuffering->getPreviousIndex(); }
    [[nodiscard]] BufferCopyId getNextIndex() const { return m_multiBuffering->getNextIndex(); }
    [[nodiscard]] BufferCopyId getMaxIndex() const { return m_multiBuffering->getMaxIndex(); }
    [[nodiscard]] BufferCopyId getIndexCount() const { return m_multiBuffering->getIndexCount(); }

    [[nodiscard]] std::shared_ptr<MultiBuffering> getMultiBuffering() const { return m_multiBuffering; };

  private:
    std::shared_ptr<MultiBuffering> m_multiBuffering;
};

template <typename T>
class MultiBufferedResource : public WithMultiBuffering {
  public:
    MultiBufferedResource() : WithMultiBuffering(nullptr) {}
    explicit MultiBufferedResource(std::shared_ptr<MultiBuffering> m) : WithMultiBuffering(m), m_resources(m->getIndexCount()) {}
    MultiBufferedResource(std::shared_ptr<MultiBuffering> m, const T &value) : WithMultiBuffering(m), m_resources(m->getIndexCount(), value) {}
    MultiBufferedResource(std::shared_ptr<MultiBuffering> m, T &&args) : WithMultiBuffering(m), m_resources(m->getIndexCount(), std::forward<T>(args)) {}
    MultiBufferedResource(std::shared_ptr<MultiBuffering> m, std::vector<T> values) : WithMultiBuffering(m), m_resources(std::move(values)) {
        assert(m->getIndexCount() == m_resources.size());
    }

    ~MultiBufferedResource() {
        m_resources.clear();
    }

    T &getActive() { return m_resources[getActiveIndex()]; }
    const T &getActive() const { return m_resources[getActiveIndex()]; }

    constexpr auto begin() noexcept { return m_resources.begin(); }
    constexpr auto begin() const noexcept { return m_resources.begin(); }
    constexpr auto end() noexcept { return m_resources.end(); }
    constexpr auto end() const noexcept { return m_resources.end(); }
    constexpr auto &at(size_t pos) { return m_resources.at(pos); }
    constexpr auto &at(size_t pos) const { return m_resources.at(pos); }
    constexpr auto &operator[](size_t pos) { return m_resources[pos]; }
    constexpr auto &operator[](size_t pos) const { return m_resources[pos]; }
    constexpr auto size() const noexcept { return m_resources.size(); }

  private:
    std::vector<T> m_resources;
};

class Texture;
class MultiBufferedTexture : public MultiBufferedResource<std::shared_ptr<Texture>> {
  public:
    MultiBufferedTexture() : MultiBufferedResource() {}
    MultiBufferedTexture(std::shared_ptr<MultiBuffering> m, const std::shared_ptr<Texture> &value);
    MultiBufferedTexture(std::shared_ptr<MultiBuffering> m, std::shared_ptr<Texture> &&args);
};

}; // namespace vvv