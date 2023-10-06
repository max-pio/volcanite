#ifndef CSGV_CONSTANTS_HPP
#define CSGV_CONSTANTS_HPP


// We use this header because we can in include it for compile time CompressedSegmentationVolume constants in the CPU encoding/decoding C++ classes as well as in the GLSL shaders for GPU decoding.

#define STOP_BIT 8u     // 1000 bit set if we stop the "octree" refinement here
// 3 bit codes using the stop bit:
#define PARENT 0u       // use entry from parent
#define NEIGHBOR_X 1u    //
#define NEIGHBOR_Y 2u    // } use value of a neighboring cell (pointing outwards)
#define NEIGHBOR_Z 3u    //
#define PALETTE_D 4u    // reuse the palette value from DELTA+1 entries earlier, DELTA is the next value in the stream
#define PALETTE_ADV 5u  // use palette value and advance palette pointer
#define PALETTE_LAST 6u // use the previous palette value

#define INVALID 0xFFFFFFFFu


#endif // CSGV_CONSTANTS_HPP
