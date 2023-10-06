# Development

[ToDo]

See also the [Volcanite Project ReadMe](../projects/volcanite/ReadMe.md)

Notes to include:

* CMake build
  * setting a VOLCANITE_DEFAULT_DATA_PATH
  * selecting optional libraries
  * packaging / installing
* VVV Framework
  * Pass abstraction
  * Synchronization Primitives
  * Shader management: reflections, SPIRV compilation,
  * GUI / parameter interface
  * Shader utilities: random numbers and noise, transfer functions,
  * Volume resources and readers
* Volcanite
  * general architecture, functionality and principles
  * compressed segmentation volumes and GPU caches (Paper)
  * csgv_renderer Shader walkthrough
* Other executables
  * compression development renderer
  * brick viewer
* Debugging and Analysis
  * Enabling the Address Sanitizer
  * Using NVIDIA nsight
  * debugPrintfEXT in shaders
  * Performance Analysis
    * MiniTimer
    * GPU: nsight, ctx->debugMarker, Performance Counters, ..
    * Automation: quick and dirty tips and tricks 
