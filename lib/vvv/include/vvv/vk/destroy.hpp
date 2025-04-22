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

#include <type_traits>

#define VK_DEVICE_DESTROY(device, thing)                                               \
    if (thing != static_cast<std::remove_reference<decltype(thing)>::type>(nullptr)) { \
        device.destroy(thing);                                                         \
        thing = nullptr;                                                               \
    }

#define VK_DESTROY(thing)                                                              \
    if (thing != static_cast<std::remove_reference<decltype(thing)>::type>(nullptr)) { \
        thing.destroy();                                                               \
        thing = nullptr;                                                               \
    }

#define VK_DEVICE_DESTROY_ALL(device, things)                                              \
    for (const auto &thing : things) {                                                     \
        if (thing != static_cast<std::remove_reference<decltype(thing)>::type>(nullptr)) { \
            device.destroy(thing);                                                         \
        }                                                                                  \
    }                                                                                      \
    things.clear();

#define VK_DEVICE_FREE(device, pool, thing)                                            \
    if ((pool != static_cast<std::remove_reference<decltype(pool)>::type>(nullptr))) { \
        device.free(pool, thing);                                                      \
    }                                                                                  \
    thing = nullptr;

#define VK_DEVICE_FREE_ALL(device, pool, things)                                       \
    if ((pool != static_cast<std::remove_reference<decltype(pool)>::type>(nullptr))) { \
        device.free(pool, things);                                                     \
    }                                                                                  \
    things.clear();

#define VK_DEVICE_FREE_MEMORY(device, thing)                                           \
    if (thing != static_cast<std::remove_reference<decltype(thing)>::type>(nullptr)) { \
        device.free(thing);                                                            \
    }

#define VK_DEVICE_FREE_ALL_MEMORY(device, things)                                          \
    for (const auto &thing : things) {                                                     \
        if (thing != static_cast<std::remove_reference<decltype(thing)>::type>(nullptr)) { \
            device.free(thing);                                                            \
        }                                                                                  \
    }                                                                                      \
    things.clear();

#define VK_DESTROY_SHADER(device, shader) \
    if (shader != nullptr) {              \
        shader->destroyModule(device);    \
        delete shader;                    \
        shader = nullptr;                 \
    }
