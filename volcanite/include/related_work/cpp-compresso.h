#include "vvv/volren/Volume.hpp"
#include <memory>

namespace compresso {

enum Encoding {
    PLAIN = 0,
    LZMA = 1,
    LZMA_EXTREME = 2,
};

unsigned long *Compress(unsigned long *data, int zres, int yres, int xres, int zstep, int ystep, int xstep, int &out_size);

void Compress(const std::string &path, int zstep, int ystep, int xstep, Encoding lzma, float &compression_rate, float &compression_rate_lzma, double &seconds, double &lzma_seconds);
unsigned long *Decompress(unsigned long *compressed_data);

bool test(const std::string &path, int zstep, int ystep, int xstep, Encoding lzma);
} // namespace compresso
