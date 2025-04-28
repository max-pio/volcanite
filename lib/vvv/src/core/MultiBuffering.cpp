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

#include <utility>
#include <vvv/core/MultiBuffering.hpp>

#include <vvv/core/Texture.hpp>

namespace vvv {

MultiBufferedTexture::MultiBufferedTexture(std::shared_ptr<MultiBuffering> m, const std::shared_ptr<Texture> &value) : MultiBufferedResource(std::move(m), value) {
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