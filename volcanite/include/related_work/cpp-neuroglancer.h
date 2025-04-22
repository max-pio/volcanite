#include "vvv/volren/Volume.hpp"
#include <memory>
#include <string>

#include "related_work/compress_segmentation.h"

namespace neuroglancer {
unsigned long *Compress(uint32_t *data, int zres, int yres, int xres, int bz, int by, int bx, int origz, int origy, int origx);
unsigned long *Compress(unsigned long *data, int zres, int yres, int xres, int bz, int by, int bx, int origz, int origy, int origx);

unsigned long *Decompress(unsigned long *compressed_data, int bz, int by, int bx);

bool test(const std::string &path, int brick_size);

void Compress_(const std::string &path, int brick_size, float &compression_rate, double &seconds);

void Compress(const std::string &path, int brick_size, float &compression_rate, double &seconds);
} // namespace neuroglancer