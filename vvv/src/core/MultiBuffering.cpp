#include <vvv/core/MultiBuffering.hpp>

#include <vvv/core/Texture.hpp>

namespace vvv {

    MultiBufferedTexture::MultiBufferedTexture(std::shared_ptr<MultiBuffering> m, const std::shared_ptr<Texture> &value) : MultiBufferedResource(m, value) {
        for (size_t i = 0; i < size(); i++) {
            (*this)[i] = std::make_shared<Texture>(*value);
            (*this)[i]->setName((*this)[i]->getName() + "." + std::to_string(i));
        }
    }

    MultiBufferedTexture::MultiBufferedTexture(std::shared_ptr<MultiBuffering> m, std::shared_ptr<Texture> &&args) : MultiBufferedResource(m, args) {
        for (size_t i = 0; i < size(); i++) {
            (*this)[i] = std::make_shared<Texture>(*args);
            (*this)[i]->setName((*this)[i]->getName() + "." + std::to_string(i));
        }
    }

}; // namespace vvv