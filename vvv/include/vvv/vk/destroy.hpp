#pragma once

#define VK_DEVICE_DESTROY(device, thing)                                                                                                                                                               \
    if (thing != static_cast<typeof thing>(nullptr)) {                                                                                                                                                 \
        device.destroy(thing);                                                                                                                                                                         \
        thing = nullptr;                                                                                                                                                                               \
    }

#define VK_DESTROY(thing)                                                                                                                                                                              \
    if (thing != static_cast<typeof thing>(nullptr)) {                                                                                                                                                 \
        thing.destroy();                                                                                                                                                                               \
        thing = nullptr;                                                                                                                                                                               \
    }

#define VK_DEVICE_DESTROY_ALL(device, things)                                                                                                                                                          \
    for (const auto &thing : things) {                                                                                                                                                                 \
        if (thing != static_cast<typeof thing>(nullptr)) {                                                                                                                                             \
            device.destroy(thing);                                                                                                                                                                     \
        }                                                                                                                                                                                              \
    }                                                                                                                                                                                                  \
    things.clear();

#define VK_DEVICE_FREE(device, pool, thing)                                                                                                                                                            \
    if ((pool != static_cast<typeof pool>(nullptr))) {                                                                                                                                                 \
        device.free(pool, thing);                                                                                                                                                                      \
    }                                                                                                                                                                                                  \
    thing = nullptr;

#define VK_DEVICE_FREE_ALL(device, pool, things)                                                                                                                                                       \
    if ((pool != static_cast<typeof pool>(nullptr))) {                                                                                                                                                 \
        device.free(pool, things);                                                                                                                                                                     \
    }                                                                                                                                                                                                  \
    things.clear();

#define VK_DEVICE_FREE_MEMORY(device, thing)                                                                                                                                                           \
    if (thing != static_cast<typeof thing>(nullptr)) {                                                                                                                                                 \
        device.free(thing);                                                                                                                                                                            \
    }

#define VK_DEVICE_FREE_ALL_MEMORY(device, things)                                                                                                                                                      \
    for (const auto &thing : things) {                                                                                                                                                                 \
        if (thing != static_cast<typeof thing>(nullptr)) {                                                                                                                                             \
            device.free(thing);                                                                                                                                                                        \
        }                                                                                                                                                                                              \
    }                                                                                                                                                                                                  \
    things.clear();

#define VK_DESTROY_SHADER(device, shader)                                                                                                                                                              \
    if (shader != nullptr) {                                                                                                                                                                           \
        shader->destroyModule(device);                                                                                                                                                                 \
        delete shader;                                                                                                                                                                                 \
        shader = nullptr;                                                                                                                                                                              \
    }
