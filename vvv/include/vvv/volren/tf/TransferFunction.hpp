#pragma once

#include <vvv/core/preamble.hpp>
#include <vvv/core/Texture.hpp>

namespace vvv {

    /**
     * A common interface for all transfer functions.
     *
     * The idea is that all our current transfer functions can be represented by a single texture
     * that is either uploaded or created by a preprocessing step. Reading of this texture depends on the
     * type of the transfer function, which is why we expose a unique ID and a unique Label for shaders to
     * use as a preprocessor switch.
     */
    // Note: Tobias Rapp had numerical issues with rgba8 and used rgba16
    class TransferFunction : public WithGpuContext {

    public:

        [[nodiscard]] Texture &texture() const { return *m_texture; }

        // TODO(Reiner): add more variants that allow specification of the queue, command buffer etc
        [[nodiscard]] virtual std::pair<vvv::AwaitableHandle, std::shared_ptr<vvv::Buffer>> upload() = 0;

        virtual std::string preprocessorLabel() = 0;

    protected:

        explicit TransferFunction(GpuContextPtr ctx) : WithGpuContext(ctx) {}

        /** preintegrated transfer function
         *  Implementers are should at least support vk::ImageUsageFlagBits::eSampled.
         * */
        std::shared_ptr<Texture> m_texture;
    };

}
