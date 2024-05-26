#ifndef CSGV_CONSTANTS_HPP
#define CSGV_CONSTANTS_HPP

#ifdef GL_core_profile
    #define CSGV_UINT uint
    #define NO_RANS 0
    #define SINGLE_TABLE_RANS 1
    #define DOUBLE_TABLE_RANS 2
#else
    #define CSGV_UINT uint32_t
    namespace vvv {
        enum RANSMode {NO_RANS=0, SINGLE_TABLE_RANS=1, DOUBLE_TABLE_RANS=2};
    }
#endif

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


// Material definition

#define LABEL_AS_ATTRIBUTE 0xFFFFFFFFu

struct GPUSegmentedVolumeMaterial {
    CSGV_UINT discrAttributeStart;  // start attribute read location in g_attributes. a value < 0 means to use the label directly (csgv_id)
    float discrIntervalMin;         // discrAttribute values within this interval [min, max) assign the label to this material
    float discrIntervalMax;         // discrAttribute values within this interval [min, max) assign the label to this material
    CSGV_UINT tfAttributeStart;     // start attribute read location in g_attributes
    float tfIntervalMin;            // attribute min / max values mapped to the TF interval [0, 1]
    float tfIntervalMax;            // attribute min / max values mapped to the TF interval [0, 1]
    float opacity;                  // opacity of the material, for < 1 is a semi-transparent volume, for >= 1 is a surface
    float emission;                 // how emissive the material is
    int wrapping;                   // wrapping mode: 0 = clamp, 1 = repeat
};

#endif // CSGV_CONSTANTS_HPP
