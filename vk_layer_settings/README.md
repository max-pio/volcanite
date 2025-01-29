# Vulkan Validation Layer Settings

This directory contains some useful debug layer configurations. You can change the configuration without recompilation
by setting the environment variable `VK_LAYER_SETTINGS_PATH` to one of the `.txt` files in this directory.

LunarG
hosts [documentation regarding layer configuration](https://vulkan.lunarg.com/doc/view/latest/windows/layer_configuration.html)
without recompilation of the host application.

To verify your configuration has been loaded, you can enable info-level debug messages and look for the startup message
of the validation layer, which will -- depending on your debug message callback -- look something like the following:

```
Info: { Validation }:
	messageIDName   = <UNASSIGNED-khronos-validation-createinstance-status-message>
	messageIdNumber = -671457468
	message         = <Validation Information: [ UNASSIGNED-khronos-validation-createinstance-status-message ] Object 0: handle = 0x12d3ff0, type = VK_OBJECT_TYPE_INSTANCE; | MessageID = 0xd7fa5f44 | Khronos Validation Layer Active:
    Settings File: None. Default location is /home/rd/repo/vvv/cmake-build-debug/vvv-glfw-app\vk_layer_settings.txt.
    Current Enables: None.
    Current Disables: None.
>
	Objects:
		Object  
			objectType   = Instance
			objectHandle = 19742704

```

Note the substring: `Settings File: None.`, indicating that we did not modify the settings file or that we had a typo
in `VK_LAYER_SETTINGS_PATH`.

## How to Debug the Validation Layers

This is a short tutorial on how to either patch a bug in the debug layers or how to locally build the debug layers to
investigate a SEGFAULT within the layers.

1. Clone the repository at `https://github.com/KhronosGroup/Vulkan-ValidationLayers`.
2. Install dependencies and build the layers as explained in the `BUILD.md`.
3. Set the following environment variables:
   ```
   export VK_LAYER_PATH=/mnt/ssd/repo/Vulkan-ValidationLayers/build/layers
   export VK_LIBRARY_PATH=/mnt/ssd/repo/Vulkan-ValidationLayers/build/layers
   export VK_LAYER_SETTINGS_PATH=/mnt/ssd/repo/vvv/vk_layer_settings/debug-validation-layer.txt
   ```
4. Running the app should now print the following to stdout, indicating that the locally build validation layer is used:
   ```
   UNASSIGNED-khronos-Validation-debug-build-warning-message(WARN / PERF): msgNum: 648835635 - Validation Performance Warning: [ UNASSIGNED-khronos-Validation-debug-build-warning-message ] Object 0: handle = 0xf3b630, type = VK_OBJECT_TYPE_INSTANCE; | MessageID = 0x26ac7233 | VALIDATION LAYERS WARNING: Using debug builds of the validation layers *will* adversely affect performance.
    Objects: 1
        [0] 0xf3b630, type: 1, name: NULL
   ```