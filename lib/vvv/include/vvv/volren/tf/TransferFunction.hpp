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

#include <vvv/core/Texture.hpp>
#include <vvv/core/preamble.hpp>

namespace vvv {

/// @brief A common interface for all transfer functions.
///
/// The idea is that all our current transfer functions can be represented by a single texture
/// that is either uploaded or created by a preprocessing step. Reading of this texture depends on the
/// type of the transfer function, which is why we expose a unique ID and a unique Label for shaders to
/// use as a preprocessor switch.
// Note: Tobias Rapp had numerical issues with rgba8 and used rgba16
class TransferFunction : public WithGpuContext {

  public:
    [[nodiscard]] Texture &texture() const { return *m_texture; }

    // TODO(Reiner): add more variants that allow specification of the queue, command buffer etc
    [[nodiscard]] virtual std::pair<vvv::AwaitableHandle, std::shared_ptr<vvv::Buffer>> upload() = 0;

    virtual std::string preprocessorLabel() = 0;

  protected:
    explicit TransferFunction(GpuContextPtr ctx) : WithGpuContext(ctx) {}
    virtual ~TransferFunction() = default;

    /// Preintegrated transfer function. Implementers are should at least support vk::ImageUsageFlagBits::eSampled.
    std::shared_ptr<Texture> m_texture;
};

} // namespace vvv
