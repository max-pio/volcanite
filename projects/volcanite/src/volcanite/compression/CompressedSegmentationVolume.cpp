#include "volcanite/compression/CompressedSegmentationVolume.hpp"

#include <map>
#include <sstream>
#include <thread>

#include "volcanite/compression/bitpack.hpp"

namespace vvv {

void printBrick(const std::vector<uint32_t> &brick, uint32_t brick_size, int z_step, loglevel log) {
    static const std::string digits[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F"};
    std::string s;
    for (int i = static_cast<int>(brick_size) - 1; i >= -1; i--) {
        for (int z = 0; z < brick_size; z += z_step) {
            s += (i < 0) ? digits[z % 16] + "|" : digits[i % 16] + " ";
            for (int n = 0; n < brick_size; n++) {
                if (i < 0) {
                    s += digits[n % 16] + " ";
                    continue;
                }
                auto v = brick[sfc::Morton3D::p2i(glm::uvec3(n, i, z))];
                if (v == INVALID)
                    s += " ";
                else
                    s += std::to_string(v % 10);
                s += " ";
            }

            s += "   ";
        }
        Logger(log) << s;
        s = "";
    }
}

uint32_t CompressedSegmentationVolume::readNextLodOperationFromEncoding(const uint32_t* brick_encoding, ReadState &state) const {
    // read the next symbol
    if (m_rANS_mode == NO_RANS)
        return read4Bit(brick_encoding, 0u, state.idxE++);
    else {
        const RANS* rans = state.in_detail_lod ? &m_detail_rans : &m_rans;
        return rans->itr_nextSymbol(state.rans_state, state.idxE, brick_encoding);
    }
}

uint32_t encodingIndexOfNeighbor(const uint32_t index, const int neighbor_i) {
    throw std::runtime_error("not implemented");
}

// a little table to help you keep track of all these gruesome variable names:
//      child_index the index of the child of a paraent in 0 - 7
//      lod_dim     the number of voxels in each dimension of the current LOD of a brick
//      lod_width   the step size of the current LOD brick entries in each dimension measured in voxels of the finest LOD
//      index_step  the step size between output voxels in the current LOD as a number of morton indices, considering that one step forward equals one voxel step in the finest LOD
uint32_t CompressedSegmentationVolume::valueOfNeighbor(const MultiGridNode* grid, const MultiGridNode* parent_grid, const glm::uvec3 &brick_pos, const uint32_t child_index, const uint32_t lod_dim, const uint32_t brick_size,
                                        const int neighbor_i) {
    assert(lod_dim > 0);
    assert(child_index >= 0 && child_index < 8);
    // find the position of the neighbor
    glm::ivec3 neighbor_pos = glm::ivec3(brick_pos) + neighbor[child_index][neighbor_i];
    if (glm::any(glm::lessThan(neighbor_pos, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(neighbor_pos, glm::ivec3(static_cast<int>(lod_dim)))))
        return INVALID;

    // in case we want to access a neighbor that is not already existing on this level (neighbor_i > our_i or any element of neighbor[child_index][neighbor_i] is positive,
    // we have to look up the parent element.
    else if (glm::any(glm::greaterThan(neighbor[child_index][neighbor_i], glm::ivec3(0)))) {
        // technically, this computes the index on a wrong level of detail (if not in the finest one), but because Z-order is self-including, it works
        return parent_grid[voxel_pos2idx(glm::ivec3(brick_pos / 2u) + neighbor[child_index][neighbor_i],
                                         glm::uvec3(lod_dim / 2))].label;
    }
    // otherwise, lookup the neighbor
    else {
        return grid[voxel_pos2idx(neighbor_pos, glm::uvec3(lod_dim))].label;
    }
}

uint32_t CompressedSegmentationVolume::valueOfNeighbor(const uint32_t* brick, const glm::uvec3 &brick_pos, const uint32_t child_index, const uint32_t lod_width, const uint32_t brick_size,
                                        const int neighbor_i) {
    assert(lod_width > 0);
    assert(child_index >= 0 && child_index < 8);
    // find the position of the neighbor
    glm::ivec3 neighbor_pos = glm::ivec3(brick_pos) + neighbor[child_index][neighbor_i] * static_cast<int>(lod_width);
    if (glm::any(glm::lessThan(neighbor_pos, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(neighbor_pos, glm::ivec3(static_cast<int>(brick_size))))) {
        // this is only called during decompression in which case nothing outside the volume should be referenced
        assert(false && "Invalid neighbor reference!");
        return INVALID;
    }
    // find the index of the neighbor within the brick array
    uint32_t neighbor_index = indexOfBrickPos(glm::uvec3(neighbor_pos));

    // in case we want to access a neighbor that is not already existing on this level (neighbor_i > our_i or any element of neighbor[child_index][neighbor_i] is positive, we have to
    // round down to the parent element of this element (lod_width*8)
    if (glm::any(glm::greaterThan(neighbor[child_index][neighbor_i], glm::ivec3(0))))
        neighbor_index -= neighbor_index % (lod_width * lod_width * lod_width * 8);

    // since we don't check here if we're out of bounds of the volume, it CAN happen that a value is INVALID in the encoding
    // in the decoding, such a neighbor should never be accessed
    assert(brick[neighbor_index] != INVALID && "Trying to access a neighbor that was not yet set!");

    // return value of neighbor or parent neighbor in brick
    return brick[neighbor_index];
}


// BRICK MEMORY LAYOUT for L = log2(brick_size) LODs
// HEADER                 ENCODING:
// 4bit_encoding_start[0, 1, .. L-1], palette_start[0, 1 .. L], 4bit_encoding_padded_to32bit[0, 1, .. L], 32bit_palette[L, .., 1, 0]
//       header_size*8 ᒧ                always zero ᒧ  ∟ .. one  ∟ palette size
uint32_t CompressedSegmentationVolume::encodeBrick(const std::vector<uint32_t>& volume, std::vector<uint32_t>& out, const glm::uvec3 start, const glm::uvec3 volume_dim) {
    std::vector<uint32_t> palette;
    glm::uvec3 volume_pos, brick_pos;

    const uint32_t lod_count = getLodCountPerBrick();
    const uint32_t header_size = getHeaderSize();
    uint32_t out_i = header_size * 8u;  // write head position in out, counted as number of encoded 4 bit elements

    // we need to keep track of the current brick status from coarsest to finest level to determine the right operations
    // basically do an implicit decoding while we're encoding
    uint32_t parent_value;
    uint32_t value;
    uint32_t child_index; // index of all children with the same coarser parent element, in 0 - 7, used for parent_value and neighbor-lookup index

    // construct the multigrid on this brick that we want to represent in this encoding
    std::vector<MultiGridNode> multigrid;
    constructMultiGrid(multigrid, volume, volume_dim, start, m_brick_size);

    // we start with the coarsest LOD, which is always a PALETTE_ADV of the max occuring value in the whole brick
    // we handle this here because it allows us to skip some special handling (for example checking if the palette is empty) in the following loop
    // in theory, we could start with a finer level here too and skip the first levels (= Carsten's original idea)
    out[0] = out_i;                 // LoD start position
    out[header_size - 1] = 0u; // palette start position (from back)
    uint32_t muligrid_lod_start = multigrid.size() - 1;
    if (multigrid[muligrid_lod_start].constant_subregion) {             //isHomogeneousBrick(volume, volume_dim, glm::uvec3(0u), {brick_size, brick_size, brick_size})) {Z
        write4Bit(out, 0u, out_i++, PALETTE_ADV | STOP_BIT);
    }
    else {
        write4Bit(out, 0u, out_i++, PALETTE_ADV);
    }
    palette.push_back(multigrid[muligrid_lod_start].label);


    // DEBUG
    uint32_t parent_counter = 0;

    // now we iteratively refine from coarse (8 elements in the brick) to finest (brick_size^3 elements in the brick) levels
    uint32_t current_inv_lod = 1u;
    for (uint32_t lod_width = m_brick_size / 2u; lod_width > 0u; lod_width /= 2u) {
        // write to header: keep track of where the new LODs start as number of 4bit
        out[current_inv_lod] = out_i;
        out[lod_count + current_inv_lod] = static_cast<uint32_t>(palette.size());

        // in the multigrid, LODs are ordered from finest to coarsest, so we have to go through them in reverse.
        uint32_t lod_dim = (m_brick_size/lod_width);
        uint32_t parent_multigrid_lod_start = muligrid_lod_start;
        muligrid_lod_start -= lod_dim * lod_dim * lod_dim;

        bool in_detail_lod = (m_rANS_mode == DOUBLE_TABLE_RANS) && (current_inv_lod == lod_count - 1u);

        for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i += lod_width * lod_width * lod_width) {
            // we don't store any operations for a grid node that would lie completely outside the volume
            // if this is problematic, and we would like to always handle a full brick, we could output anything here and thus just write PARENT_STOP.
            brick_pos = enumBrickPos(i, m_brick_size);
            volume_pos = start + brick_pos;
            if (glm::any(glm::greaterThanEqual(volume_pos, volume_dim)))
                continue;

            // every 8th element (we span 2*2*2=8 elements of the coarse LOD above), we fetch the new parent
            child_index = (i % (lod_width * lod_width * lod_width * 8)) / (lod_width * lod_width * lod_width);
            if (child_index == 0) {
                assert(parent_counter <= 8 && "parent element would be used for more than 8 elements!");

                // if this subtree is already filled (because in a previous LOD we set a PARENT_STOP for this area), the last element of this block is set, and we can skip it
                // note that this will also happen if this grid node lies completely outside the volume because some parent would've been set to PARENT_STOP earlier
                // our parent spanned 8 elements of this finer current level, so we need to look at the element 7 indices further
                if (multigrid[parent_multigrid_lod_start +
                        voxel_pos2idx(brick_pos / lod_width / 2u, glm::uvec3(lod_dim / 2u))].constant_subregion) {
                    parent_counter = 0;
                    i += (lod_width * lod_width * lod_width * 7);
                    continue;
                }

                parent_counter = 0;
                parent_value = multigrid[parent_multigrid_lod_start +
                        voxel_pos2idx(brick_pos / lod_width / 2u, glm::uvec3(lod_dim / 2u))].label;
                assert(parent_value != INVALID && "parent element in brick was not set in previous LOD!");
            }
            parent_counter++;

            value = multigrid[muligrid_lod_start + voxel_pos2idx(brick_pos / lod_width, glm::uvec3(lod_dim))].label;
            assert(value != INVALID && "Original volume mustn't contain the INVALID magic value!");

            uint32_t operation = 0u;
            // if the whole subtree from here has this parent_value, we can set a stop sign and fill the whole brick area of the subtree
            // note that grid nodes outside the volume are by definition also homogeneous
            if (lod_width >= 1 && multigrid[muligrid_lod_start +
                    voxel_pos2idx(brick_pos / lod_width, glm::uvec3(lod_dim))].constant_subregion) {
                operation = STOP_BIT;
            }
            // determine operation for the next entry
            if (value == parent_value)
                operation |= PARENT;
            else if (valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 0) == value)
                operation |= NEIGHBOR_X;
            else if (valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 1) == value)
                operation |= NEIGHBOR_Y;
            else if (valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 2) == value)
                operation |= NEIGHBOR_Z;
            else if (palette.back() == value)
                operation |= PALETTE_LAST;
            else {
                // reuse the n-X palette value where 0 < X < 17
                uint32_t palette_delta = static_cast<uint32_t>(std::find(palette.rbegin(), palette.rend(), value) - palette.rbegin());
                if(palette_delta < 17u && palette_delta < palette.size()) {
                    assert(palette.at(palette.size() - palette_delta - 1u) == value && "Palette value does not fit!");
                    assert(palette_delta > 0u && "the palette delta 0 should've been caught by the palette_last value!");
                    write4Bit(out, 0u, out_i++, operation | PALETTE_D);
                    operation = palette_delta - 1u; // the "0" case is already handled by PALETTE_LAST, so we only consider case 1 - 16 in our 4 bits
                } else
                {  // if nothing helps, we add a completely new palette entry
                    palette.push_back(value);
                    operation |= PALETTE_ADV;
                }
            }
            assert(operation < 16u && "we only allow writing 4 bit operations!");
            write4Bit(out, 0u, out_i++, operation);

            assert(value != INVALID);
        }


        if(m_rANS_mode == DOUBLE_TABLE_RANS) {
            // pack all previous levels via rANS encoding if we're at the second last LoD (last LoD of non-detail encoding)
            // NOTE: the old out_i and header starts count in number of elements. the following out_i counts in 4bit
            if (current_inv_lod == lod_count - 2u) {
                out_i = m_rans.packRANS(out, out[0], out_i);
                // the detail encoding has to start at a new 32bit element (which is guaranteed by our rANS output)
                assert(out_i % 8u == 0u && "next element after rANS output should start at a new uint32_t element");
            }
            // pack the detail (=finest LOD) via rANS encoding.
            // We have a separate rANS encoder here because the detail level does not use stop bits => different operation frequencies
            else if (in_detail_lod) {
                out_i = m_detail_rans.packRANS(out, out[current_inv_lod], out_i);
            }
        }
        current_inv_lod++;
    }

    // if we did not apply the rANS packing before, because we are only using a single freq. table, we do it here
    if(m_rANS_mode == SINGLE_TABLE_RANS)
        out_i = m_rans.packRANS(out, out[0], out_i);


    // last entry of our header stores the palette size
    out[header_size - 1u] = palette.size();
    // now we calculate everything in 32 bit elements. round up to start the palette at an uint32_t index but AFTER the last encoding element
    while(out_i % 8u != 0u)
        write4Bit(out, 0u, out_i++, 0u);
    out_i /= 8u;
    // palette is added in reverse order at the end to be read from encoding back to front
    for(int i = static_cast<int>(palette.size()) - 1; i >= 0; i--) {
        out.at(out_i++) = palette.at(i);
    }

    if(out_i >= out.size())
        throw std::runtime_error("out doesn't provide enough memory for encoded brick, writing outside of allocated region");
    return out_i; // we return the number of uint32_t elements that we used
}

float CompressedSegmentationVolume::separateDetail() {
    if(!m_detail_encoding.empty() || m_separate_detail)
        throw std::runtime_error("Detail separation was already performed!");
    if(m_encodings.empty())
        throw std::runtime_error("Segmentation volume is not yet compressed! Call compress() before performing detail separation.");
    if(m_rANS_mode != DOUBLE_TABLE_RANS)
        throw std::runtime_error("Detail separation can only be used in combination with rANS in double table mode!");

    const uint32_t brick_idx_count = getBrickIndexCount();
    const uint32_t lod_count = getLodCountPerBrick();
    const uint32_t header_size = getHeaderSize();

    // First, construct the detail_starts buffer in a simple sequential pass and keep track of detail encoding sizes:
    std::vector<uint32_t> split_detail_encoding_sizes(1, 0u);
    uint32_t currentDetailStart = 0u;
    m_detail_starts.resize(brick_idx_count + 1);
    for(size_t i = 0; i < brick_idx_count; i++) {
        // Write the current "brick start" before the possible splitting of encodings as it is the "previous brick end"
        m_detail_starts[i] = currentDetailStart;

        // if a new split encoding starts, restart index counter and keep track of the previous detail array size
        if(i / m_brick_idx_to_enc_vector >= split_detail_encoding_sizes.size()) {
            split_detail_encoding_sizes.back() = currentDetailStart;
            split_detail_encoding_sizes.push_back(0u);
            currentDetailStart = 0u;
        }

        const uint32_t* base_encoding = getBrickEncoding(i);
        // brick detail size: brick encoding size       - palette size      - detail LOD start
        currentDetailStart += getBrickEncodingLength(i) - getBrickPaletteLength(i) - base_encoding[lod_count - 1u] / 8;
    }
    split_detail_encoding_sizes.back() = currentDetailStart;
    m_detail_starts[brick_idx_count] = currentDetailStart;

    // Second, cut the operation encoding arrays apart und update brick headers / base encoding starts.
    // The same brick_idx to split (detail) encoding vector is used for base and detail encodings.
    // Handle one brick after another, splitting encoding arrays if necessary:
    m_detail_encoding.resize(1);
    m_detail_encoding.back().resize(split_detail_encoding_sizes.at(0));
    // ToDo: it would be possible to process all split encoding arrays in parallel, but woudl drastically increase tmp memory
    for(uint32_t brick_idx = 0u; brick_idx < brick_idx_count; brick_idx++) {

        uint32_t detail_start = m_detail_starts[brick_idx];
        // Check if we have to start a new split encoding "vector" before writing the next brick's encoding.
        if(brick_idx / m_brick_idx_to_enc_vector > m_detail_encoding.size() - 1u) {
            // start a new detail encoding array
            m_detail_encoding.emplace_back(split_detail_encoding_sizes.at(brick_idx / m_brick_idx_to_enc_vector));
            detail_start = 0u;
        }
        uint32_t detail_encoding_size = m_detail_starts[brick_idx+1] - detail_start;

        // operate directly on the current brick base encoding array
        uint32_t* mut_encoding = m_encodings[brick_idx / m_brick_idx_to_enc_vector].data();
        uint32_t old_brick_start = getBrickStart(brick_idx);
        uint32_t old_brick_encoding_size = getBrickEncodingLength(brick_idx);
        uint32_t palette_size = getBrickPaletteLength(brick_idx);

        // changes for one brick's encoding:
        // - one uint32 element is removed from the header (LoD start position of the detail) --> now in m_detail_starts
        // - the operation stream is shortened by the detail level's encoding --> now in m_detail_encodings
        // to ensure that the base encodings are packed tight again, encodings are moved to the front

        // determine the new output position of this brick in the base encoding output array (overwriting old content)
        // we are only allowed to read from m_brick_starts[i], m_brick_starts[i+1] is undefined!
        uint32_t* new_base_encoding_start = mut_encoding + old_brick_start         // old start position of the brick
                                            - detail_start                              // uint32 elements that were removed from the base encoding
                                            - (brick_idx % m_brick_idx_to_enc_vector);  // uint32 elements that were removed from the header

        uint32_t* old_brick_encoding = mut_encoding + old_brick_start;
        // length (in uint32 elements) of the operation stream of base levels only
        size_t base_op_stream_length = mut_encoding[old_brick_start + lod_count - 1] / 8 - header_size;

        // copy the detail encoding to the detail buffer
        memcpy(&(m_detail_encoding.at(brick_idx / m_brick_idx_to_enc_vector).at(detail_start)), old_brick_encoding + header_size + base_op_stream_length, detail_encoding_size * sizeof(uint32_t));

        // copy the first part of the header (LOD starts from 0 to L-2 without the detail level), to the base encoding buffer.
        // the header is missing one element (start pos. of the detail layer) now, so we have to adjust the lod start entries.
        memmove(new_base_encoding_start, old_brick_encoding, (lod_count - 1u) * sizeof(uint32_t));
        for(int l = 0; l < (lod_count - 1u); l++)
            new_base_encoding_start[l] -= 8u;
        // move the palette part of the encoding header one element to the front (because the encoding_start entry for the detail buffer is now missing in between)
        memmove(new_base_encoding_start + (lod_count - 1u), old_brick_encoding + lod_count, (lod_count + 1u) * sizeof(uint32_t));
        // move the base encoding
        memmove(new_base_encoding_start + (header_size - 1u), old_brick_encoding + header_size, base_op_stream_length * sizeof(uint32_t));
        // move the palette
        memmove(new_base_encoding_start + (header_size - 1u) + base_op_stream_length, old_brick_encoding + header_size + base_op_stream_length + detail_encoding_size, palette_size * sizeof(uint32_t));

        // Update brick start index: The brick start entries move to the front. In addition to the detail encoding,
        // brick headers are missing one element (detail LoD start) each. Up to brick brick_idx, these sum up to
        // (brick_idx % m_brick_idx_to_enc_vector). If a new split encoding was started here, we store the previous
        // brick end. Thus subtract all m_brick_idx_to_enc_vector missing header elements from the previous encoding:
        if(brick_idx > 0 && brick_idx % m_brick_idx_to_enc_vector == 0) {
            m_brick_starts[brick_idx] = m_brick_starts[brick_idx] - m_detail_starts[brick_idx] - m_brick_idx_to_enc_vector;
            // the previous split encoding array was processed: shrink it down to a tight fit
            m_encodings.at((brick_idx - 1u) / m_brick_idx_to_enc_vector).resize(m_brick_starts[brick_idx]);
        } else {
            m_brick_starts[brick_idx] = m_brick_starts[brick_idx] - m_detail_starts[brick_idx] - brick_idx % m_brick_idx_to_enc_vector;
        }
    }
    // store end index of brick starts
    m_brick_starts[brick_idx_count] = m_brick_starts[brick_idx_count] - m_detail_starts[brick_idx_count] - (brick_idx_count % m_brick_idx_to_enc_vector);
    m_separate_detail = true;
    // shrink last encoding buffer
    m_encodings.back().resize(m_brick_starts[brick_idx_count]);

    if(!verifyCompression())
        throw std::runtime_error("Corrupt CSGV after detail separation");

    // return the ratio of detail encoding size to total encoding size
    return (static_cast<float>(m_detail_starts[brick_idx_count]) / static_cast<float>(m_brick_starts[brick_idx_count] + m_detail_starts[brick_idx_count]));
}

bool CompressedSegmentationVolume::verifyCompression() const {
    if(m_encodings.empty())
        throw std::runtime_error("Segmentation volume is not yet compressed!");

    bool is_ok = true;
    glm::uvec3 brick_count = getBrickCount();
    size_t last_brick = getBrickIndexCount() - 1ul;
    uint32_t lod_count = getLodCountPerBrick();
    uint32_t header_size = getHeaderSize();
    uint32_t header_start_lods = lod_count - (isUsingSeparateDetail() ? 1 : 0);

    // check that all encodings have the size that is tracked in the brick starts arrays
    for(int i = 0; i < m_encodings.size(); i++) {
        // any m_brick_idx_to_enc_vector-th entry in brick_starts is the end of the last brick in the previous array
        uint32_t size_from_brick_starts = m_brick_starts[std::min(static_cast<uint32_t>(last_brick + 1), (i+1) * m_brick_idx_to_enc_vector)];
        if (m_encodings.at(i).size() != size_from_brick_starts) {
            Logger(ERROR) << "Found errors: split encoding array [" << i << "] size differs from size tracked in brick starts (is "
                          << m_encodings.at(i).size() << " expected " << size_from_brick_starts << ").";
            return false;
        }
    }

    #pragma omp parallel for collapse(3) default(none) shared(is_ok, brick_count, header_size, header_start_lods, lod_count, m_brick_starts, m_encodings, m_detail_starts, m_detail_encoding)
    for(uint32_t z = 0u; z < brick_count.z; z++) {
        for (uint32_t y = 0u; y < brick_count.y; y++) {
            for (uint32_t x = 0u; x < brick_count.x; x++) {

                if(!is_ok)
                    continue;

                glm::uvec3 brick(x, y, z);
                std::stringstream error = {};
                uint32_t brick1D = brick_pos2idx(brick, brick_count);

                // check brick having an encoding length greater than header size + 1 operation + 1 palette entry
                uint32_t brick_encoding_length = getBrickEncodingLength(brick1D);
                if(brick_encoding_length < header_size + 1u + 1u)
                    error << " brick encoding is shorter than minimum. (header size + 1 encoding + 1 palette)=" << (header_size+2) <<" but is " << brick_encoding_length << "\n";

                // check first header entry being header_size * 8
                const uint32_t* brick_encoding = getBrickEncoding(brick1D);
                if(brick_encoding[0] != header_size * 8u)
                    error << "  first encoding starts 4bit must be header*8=" << (header_size * 8u) << " but is "  << brick_encoding[0] << "\n";

                // check encoding starts being in ascending order
                // note: the header count the number of entries, except the last entry when using double table rANS
                // for which this entry refers to the raw 4 bit index at which the detail encoding starts AFTER packing the earlier LoDs
                for(int l = 1; l < header_start_lods - (isUsingDetailFreq() ? 1 : 0); l++) {
                    long distance = static_cast<long>(brick_encoding[l]) - static_cast<long>(brick_encoding[l - 1]);
                    if(distance < 0l) {
                        error << "  encoding starts are not in ascending order (distance " << distance << " for LoD " << l << ")\n";
                        break;
                    }
                    else if(distance > m_brick_size * m_brick_size * m_brick_size) {
                        error << "  encoding starts between LoDs are too far away\n";
                        break;
                    }
                }

                // check palette start of first LoD being 0 and second LoD being 1
                if(brick_encoding[header_start_lods] != 0u)
                    error << "  first palette start must be 0 but is " << brick_encoding[header_start_lods];
                if(brick_encoding[header_start_lods + 1u] != 1u)
                    error << "  second palette start must be 1 but is " << brick_encoding[header_start_lods + 1u];

                // check palette starts being in ascending order
                for(int l = 2u; l <= lod_count + 1; l++) {
                    if(brick_encoding[header_start_lods + l] < brick_encoding[header_start_lods + l - 1]) {
                        error << "  palette starts are not in ascending order\n";
                        break;
                    }
                }

                uint32_t palette_size = brick_encoding[getPaletteSizeHeaderIndex()];
                // check palette size not being zero
                if(palette_size == 0u) {
                    error << "  palette size is zero\n";
                }

                // check palette size + encoding start of last LoD being shorter than the brick encoding
                if(palette_size + brick_encoding[header_start_lods]/8u > brick_encoding_length) {
                    error << "  palette size and encoding of first (L-1) levels are longer than the total brick encoding\n";
                }

                // check detail encoding having at least 1 entry
                if(isUsingSeparateDetail()) {
                    long detail_encoding_length = getBrickDetailEncodingLength(brick1D);//static_cast<long>(m_detail_starts[brick1D + 1u]) - static_cast<long>(m_detail_starts[brick1D]);
                    if (detail_encoding_length < 1l) {
                        error << "  brick detail encoding is missing with length " << detail_encoding_length << "\n";
                    }
                }

                // check for 32 Bit overflow if bytes are indexed in the buffers
                // if(glm::all(glm::equal(brick, brick_count - glm::uvec3(1))))
                {
//                        size_t encoding_bytes = static_cast<size_t>((m_brick_starts[brick1D + 1u] - m_brick_starts[brick1D])
//                                                                   - palette_size - header_size) * 4ul;

                    if (brick1D > 0 && m_brick_starts[brick1D + 1u] == 0) {
                        error << "  brick start index array contains invalid zero after first entry";
                    }
                    if (static_cast<size_t>(m_brick_starts[brick1D + 1u]) > (~0u)) {
                        error << "  encoding contains more 32 bit entries ("
                              << (static_cast<size_t>(m_brick_starts[brick1D + 1u]))
                              << ") than 32 bit indices can index (" << (~0u) << ")\n";
                    }

                    if (isUsingSeparateDetail()) {
                        if (m_detail_starts[brick1D + 1u] == 0) {
                            error << "  brick detail start index array contains invalid zero after first entry";
                        }
                        if (static_cast<size_t>(m_detail_starts[brick1D + 1u]) > (~0u)) {
                            error << "  detail encoding contains more 32 bit entries ("
                                  << (static_cast<size_t>(m_detail_starts[brick1D + 1u]))
                                  << ") than 32 bit indices can index (" << (~0u) << ")\n";
                        }
                    }
                }

                // print error message
                if(!error.str().empty()) {
                    #pragma omp critical
                    {
                        if(is_ok) {
                            Logger(ERROR) << "Found errors for brick " << str(brick) << " #" << brick_pos2idx(brick, getBrickCount()) << ":\n" << error.str() << "---";
                            // ToDo: loglevel ERROR does not work on windows. this workaround outputs to INFO in that case (ERROR is defined as 0 in windows.h)
                            printBrickInfo(brick, vvv::loglevel(ERROR));
                            is_ok = false;
                        }
                    }
                }

            }
        }
    }
    return is_ok;
}

void CompressedSegmentationVolume::decodeBrick(uint32_t brick_idx, uint32_t* output_brick, glm::uvec3 valid_brick_size, int inv_lod) const {
    const uint32_t* brick_encoding = getBrickEncoding(brick_idx);
    // the palette starts at the end of the encoding block
    uint32_t paletteE = getBrickEncodingLength(brick_idx) - 1u;
    const uint32_t* brick_palette = brick_encoding;

    // first: read the header (= first header entry is the start positions of the inv. LoD 0)
    uint32_t lod_count = getLodCountPerBrick();
    ReadState readState = {.idxE=brick_encoding[0], .in_detail_lod=false};
    if(m_rANS_mode != NO_RANS) {
        // idxE counts in bytes for rANS state instead of number of 4 bit entries
        readState.idxE = (readState.idxE / 8) * 4;
        m_rans.itr_initDecoding(readState.rans_state, readState.idxE, brick_encoding);
    }

    uint32_t index_step = m_brick_size * m_brick_size * m_brick_size;
    uint32_t lod_width = m_brick_size;
    uint32_t parent_value;
    uint32_t child_index;   // index of all children with the same coarser parent element, in 0 - 7, used for parent_value and neighbor-lookup index

    // first, set the whole brick to INVALID, so we know later which elements and LOD blocks were already processed
    for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i++)
        output_brick[i] = INVALID;

    for (int lod = 0; lod <= inv_lod; lod++) {

        // check if we ran into the detail layer and change the readState accordingly
        if(m_rANS_mode == DOUBLE_TABLE_RANS && lod == lod_count-1) {
            readState.in_detail_lod = true;
            if(m_separate_detail) {
                // we now read from the separated detail encoding buffer
                brick_encoding = getBrickDetailEncoding(brick_idx);
                readState.idxE = 0u;
                m_detail_rans.itr_initDecoding(readState.rans_state, readState.idxE, brick_encoding);
            }
            else {
                // Read the lod start from the brick header to start reading at the right encoding buffer index.
                // We have to start at a fully padded uint32, because we switch the rANS decoder.
                readState.idxE = (brick_encoding[lod] / 8) * 4;
                m_detail_rans.itr_initDecoding(readState.rans_state, readState.idxE, brick_encoding);
            }
        }

        for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i += index_step) {
            // if a grid node is completely outside the volume (i.e. it's first element is not within the volume) we skip it as it won't have any entries in the encoding
            if (glm::any(glm::greaterThanEqual(enumBrickPos(i, m_brick_size), valid_brick_size)))
                continue;

            // every 8th element (we span 2*2*2=8 elements of the coarse LOD above), we fetch the new parent
            child_index = (i % (index_step * 8)) / index_step;
            if (lod > 0 && i % (index_step * 8) == 0) {

                // if this subtree is already filled (because in a previous LOD we had a PARENT_STOP for this area), the last element of this block is set and we can skip it
                if (output_brick[i + (index_step * 7)] != INVALID) {
                    i += (index_step * 7);
                    continue;
                }

                parent_value = output_brick[i];
                assert(parent_value != INVALID && "parent element in brick was not set in previous LOD!");
            }

            // get the next operation and apply it (either progress in the current RLE or read the next entry)
            uint32_t operation = readNextLodOperationFromEncoding(brick_encoding, readState);

            uint32_t operation_lsb = operation & 7u; // extract least significant 3 bits
            if (operation_lsb == PARENT)
                output_brick[i] = parent_value;
            else if (operation_lsb == NEIGHBOR_X)
                output_brick[i] = valueOfNeighbor(output_brick, enumBrickPos(i, m_brick_size), child_index, lod_width, m_brick_size, 0);
            else if (operation_lsb == NEIGHBOR_Y)
                output_brick[i] = valueOfNeighbor(output_brick, enumBrickPos(i, m_brick_size), child_index, lod_width, m_brick_size, 1);
            else if (operation_lsb == NEIGHBOR_Z)
                output_brick[i] = valueOfNeighbor(output_brick, enumBrickPos(i, m_brick_size), child_index, lod_width, m_brick_size, 2);
            else if (operation_lsb == PALETTE_ADV) { // read palette entry and advance palette pointer to the next entry
                output_brick[i] = brick_palette[paletteE--];
            }
            else if (operation_lsb == PALETTE_LAST) {
                output_brick[i] = brick_palette[paletteE + 1];
            }
            else if (operation_lsb == PALETTE_D) {
                uint32_t palette_delta = readNextLodOperationFromEncoding(brick_encoding, readState) + 2u;
                output_brick[i] = brick_palette[paletteE + palette_delta];
            }
            else
                assert(false && "unrecognized compression operation");

            // stop traversal: fill all other parts of the brick with this value
            if ((operation & STOP_BIT) > 0u) {
                // fill the whole subtree with the parent value
                for (uint32_t n = i; n < i + index_step; n++) {
                    output_brick[n] = output_brick[i];
                }
            }

            assert(output_brick[i] != INVALID && "Set output element brick to forbidden magic value INVALID!");
        }

        // move to the next LOD block with half the block width and an eight of the index_step respectively
        index_step /= 8;
        lod_width /= 2;
    }
}

void CompressedSegmentationVolume::compress(const std::vector<uint32_t> &volume, const glm::uvec3 volume_dim, bool verbose) {
    if(m_brick_size == 0u)
        throw std::runtime_error("Compression parameters are not initialized!");

    m_volume_dim = volume_dim;
    glm::uvec3 brickCount = getBrickCount();
    if(verbose) {
        Logger(DEBUG) << " running with " << m_cpu_threads << " threads on " << std::thread::hardware_concurrency() << " CPU cores";
        Logger(DEBUG) << " brick count: " << str(brickCount) << " = " << getBrickIndexCount() << " with brick size " << m_brick_size << "^3";
    }

    // m_encodings contains > 0 vectors storing the brick encoding. For any brick with 1D index i, the corresponding
    // encoding vector index in m_encodings is obtained through (i / m_brick_idx_to_enc_vector).
    // m_brick_idx_to_enc_vector is set to UINT32_MAX initially and reduced during the compression aiming to store
    // m_enc_vector_limit many uint32_t entries in the first encoding vector.
    m_encodings.clear();
    size_t reserved_size = std::min(static_cast<size_t>(m_target_uints_per_split_encoding), static_cast<size_t>(volume_dim.x) * volume_dim.y * volume_dim.z / 12ul / 4ul); // assume that we have a compression rate below 1/12
    if(reserved_size > UINT32_MAX) {
        Logger(WARN) << "Volume is large, potentially creating a Compressed Segmentation Volume that does not fit into 32bit address!";
        reserved_size = UINT32_MAX;
    }
    // Start with one encoding vector. Once it is filled up to the target size m_enc_vector_limit,
    // m_brick_idx_to_enc_vector is updated to start a new encoding vector for the next brick index.
    m_encodings.emplace_back();
    m_encodings[0].reserve(reserved_size);
    uint32_t brick_index_count = getBrickIndexCount();
    m_brick_starts.resize(brick_index_count + 1, INVALID);
    // reset brick to split encoding vector mapping, and max. palette entry count
    m_brick_idx_to_enc_vector = ~0u;
    m_max_brick_palette_count = 0u;

    // detail buffers can only be filled with a subsequent call to separateDetail()
    m_separate_detail = false;
    m_detail_encoding.clear();
    m_detail_starts.clear();


    if(verbose)
        Logger(INFO, true) << " Progress 0.0%";
    MiniTimer progressTimer;
    MiniTimer totalTimer;
    uint32_t bricks_since_last_update = 0;

    // compute the next m_cpu_threads brick encodings in parallel
    // we assume that the worst case compression rate is 100% and allocate encoding buffers accordingly
    const uint32_t encoded_brick_buffer_size = m_brick_size * m_brick_size * m_brick_size;
    std::vector<uint32_t> encodedBrick[m_cpu_threads];
    uint32_t encoded_element_count[m_cpu_threads];
    uint32_t encoded_element_count_prefix_sum[m_cpu_threads];
    for (int thread_id = 0; thread_id < m_cpu_threads; thread_id++) {
        encodedBrick[thread_id].resize(encoded_brick_buffer_size);
        encoded_element_count[thread_id] = 0u;
        encoded_element_count_prefix_sum[thread_id] = 0u;
    }

    // compress one brick after another (but m_cpu_threads of them in parallel) in brick_index order
    for(uint32_t brick_index = 0u; brick_index < brick_index_count; brick_index += m_cpu_threads) {

        #pragma omp parallel num_threads(m_cpu_threads) default(none) shared(brick_index, brickCount, brick_index_count, volume, encodedBrick, encoded_element_count)
        {
            unsigned int thread_id = omp_get_thread_num();
            encoded_element_count[thread_id] = 0u;
            if (brick_index + thread_id < brick_index_count) {
                glm::uvec3 brick = brick_idx2pos(brick_index + thread_id, brickCount);
                // compress the current brick
                encoded_element_count[thread_id] = encodeBrick(volume, encodedBrick[thread_id], brick * m_brick_size, m_volume_dim);
            }
        }

        // an exclusive prefix sum of the element counts tells us the local offsets in the encoding buffer.
        // encoded_element_count_prefix_sum[0] is always 0. We also count how many new elements we need in total.
        for (int thread_id = 1; thread_id < m_cpu_threads; thread_id++) {
                encoded_element_count_prefix_sum[thread_id] = encoded_element_count_prefix_sum[thread_id - 1]
                                                              + encoded_element_count[thread_id - 1];
        }
        size_t old_encoding_size = m_encodings.back().size();
        size_t new_encoding_size = old_encoding_size + encoded_element_count_prefix_sum[m_cpu_threads - 1] + encoded_element_count[m_cpu_threads - 1];

        // Check if we have to start a new encoding vector here. As m_brick_idx_to_enc_vector is always a multiple of
        // m_cpu_threads, either all or none of the new bricks belong to a new split encoding array.
        if(std::min(brick_index_count, (brick_index + m_cpu_threads - 1u)) / m_brick_idx_to_enc_vector >= m_encodings.size()) {
            m_encodings.emplace_back();
            m_encodings.back().reserve(reserved_size);
            old_encoding_size = 0ul;
            new_encoding_size = encoded_element_count_prefix_sum[m_cpu_threads - 1] + encoded_element_count[m_cpu_threads - 1];
        }
        // Check if the initial split must happen here (when the uint32_t element count exceeds m_target_uints_per_split_encoding)
        else if(new_encoding_size > m_target_uints_per_split_encoding) {
            if(brick_index == 0u) {
                Logger(WARN) << "Requested split encoding size is too small. Using minimal size.";
            }
            // We can not reduce m_brick_idx_to_enc_vector further if it was already used for splitting encoding vectors.
            // Otherwise, the old split may become invalid.
            else if(m_encodings.size() == 1) {
                // To make things easier, we always split at an index that is a multiple of m_cpu_threads
                m_brick_idx_to_enc_vector = brick_index;
                uint32_t split_encoding_count = (brick_index_count - 1u) / m_brick_idx_to_enc_vector + 1u;
                m_encodings.emplace_back();
                m_encodings.back().reserve(reserved_size);
                old_encoding_size = 0ul;
                new_encoding_size = encoded_element_count_prefix_sum[m_cpu_threads - 1] + encoded_element_count[m_cpu_threads - 1];
            } else {
                Logger(WARN) << "Brick index to encoding vector mapping is underestimating sizes.";
            }
        }

        // append the results
        m_encodings.back().resize(new_encoding_size);
        #pragma omp parallel num_threads(m_cpu_threads) default(none) shared(brick_index, encoded_element_count, encoded_element_count_prefix_sum, encodedBrick, old_encoding_size)
        for (int thread_id = 0; thread_id < m_cpu_threads; thread_id++) {
            if (encoded_element_count[thread_id] == 0u)
                continue;

            assert((brick_index + thread_id) / m_brick_idx_to_enc_vector == m_encodings.size() - 1 && "Writing brick encoding to false split encoding array.");

            // store the start index of the brick within the encoding array
            m_brick_starts[brick_index + thread_id] = static_cast<uint32_t>(old_encoding_size + encoded_element_count_prefix_sum[thread_id]);
            // copy the encoded brick to the current encoding array. std::copy is sometimes slightly faster than memcpy.
            std::copy(encodedBrick[thread_id].begin(), encodedBrick[thread_id].begin() +  encoded_element_count[thread_id],
                      m_encodings.back().begin() + static_cast<long>(old_encoding_size + encoded_element_count_prefix_sum[thread_id]));
        }

        // The first brick start of an encoding array is zero per default. Instead of zero, we store the total size of
        // the previous split encoding vector. This way, brick_starts[i + 1] - brick_starts[i] still yields the size of
        // the encoding of the last brick i in the previous split encoding.
        // Note that we have to handle the special case of brick_starts[j] = 0 for any brick at the start of a split
        // vector. An easy check for this case is brick_starts[j] > brick_starts[j+1].
        if(m_encodings.size() > 1 && old_encoding_size == 0ul) {
            m_brick_starts[brick_index] = static_cast<uint32_t>(m_encodings[m_encodings.size() - 2].size());
        }

        // update the maximum palette size
        for (int thread_id = 0; thread_id < m_cpu_threads; thread_id++) {
            if (encoded_element_count[thread_id] > 0u && encodedBrick[thread_id][getPaletteSizeHeaderIndex()] > m_max_brick_palette_count) {
                m_max_brick_palette_count = encodedBrick[thread_id][getPaletteSizeHeaderIndex()];
            }
        }

        // output a progress update
        if(verbose) {
            bricks_since_last_update += m_cpu_threads;
            constexpr const double PROGRESS_UPDATE_INTERVAL = 2.;
            if (progressTimer.elapsed() >= PROGRESS_UPDATE_INTERVAL) {
                float bricks_per_second = static_cast<float>(bricks_since_last_update / progressTimer.elapsed());
                uint32_t last_brick_index = glm::min(brick_index + m_cpu_threads - 1, brick_index_count);
                float remaining_seconds = static_cast<float>(brick_index_count - last_brick_index) / bricks_per_second;
                std::stringstream stream;
                stream << " Progress " << std::fixed << std::setprecision(1) << static_cast<float>(last_brick_index) / static_cast<float>(brick_index_count) * 100.f << "%"
                       << " (" << std::setprecision(2) << (bricks_per_second * static_cast<float>(m_brick_size * m_brick_size * m_brick_size) / 1000000.f)
                       << " million voxels/second), remaining: " << static_cast<int>(remaining_seconds / 60.f) << "m" << (static_cast<int>(remaining_seconds) % 60) << "s";
                Logger(INFO, true) << stream.str();
                progressTimer.restart();
                bricks_since_last_update = 0;
            }
        }

        // Our brickStarts-Array stores start positions as indices within the uint32_t encoding array.
        // If there are more than 2^32 uints in there, we can't store the start position.
        // Set a lower m_enc_vector_limit value to split the encoding into more, smaller arrays.
        if(m_encodings.back().size() > UINT32_MAX)
            throw std::runtime_error("Compressed Segmentation Volume size exceeds 32 bit address space!");
    }

    // one last dummy entry to be able to query an "end" index for the last brick
    m_brick_starts[brick_index_count] = static_cast<uint32_t>(m_encodings.back().size());

    m_last_total_encoding_seconds = static_cast<float>(totalTimer.elapsed());
    Logger(INFO) << " Progress 100% in " << std::fixed << std::setprecision(3) << m_last_total_encoding_seconds << "s (" << (static_cast<float>(volume.size()) / m_last_total_encoding_seconds / 1000000.f) << " million voxels/second) " << getEncodingInfoString();
}

//#define NO_BRICK_DECODE_INDEX_REMAP

void CompressedSegmentationVolume::decompressLOD(int target_lod, std::vector<uint32_t>& out) const {
    const glm::uvec3 brickCount = getBrickCount();
    int inv_lod = getLodCountPerBrick() - 1u - target_lod;
    assert(inv_lod >= 0);

    // this would run in parallel on the GPU later!
    glm::uvec3 brick_pos;
#ifndef NO_BRICK_DECODE_INDEX_REMAP
    std::vector<uint32_t> brick_cache[m_cpu_threads]; // in morton order
    for (auto &bc : brick_cache)
        bc.resize(m_brick_size * m_brick_size * m_brick_size);
#else
    void* brick_cache = nullptr; // just for OpenMP
#endif
    #pragma omp parallel for num_threads(m_cpu_threads) default(none) private(brick_pos) shared(brickCount, brick_cache, out, inv_lod)
    for (uint32_t z = 0; z < brickCount.z; z++) {
        unsigned int thread_id = omp_get_thread_num();
        brick_pos.z = z; // we need that for omp...
        for (brick_pos.y = 0; brick_pos.y < brickCount.y; brick_pos.y++) {
            for (brick_pos.x = 0; brick_pos.x < brickCount.x; brick_pos.x++) {
                size_t brick_idx = brick_pos2idx(brick_pos, brickCount);
#ifndef NO_BRICK_DECODE_INDEX_REMAP
                // decode brick
                decodeBrick(brick_idx, brick_cache[thread_id].data(), glm::clamp(m_volume_dim - brick_pos * m_brick_size, glm::uvec3(0u), glm::uvec3(m_brick_size)), inv_lod);
                // fill output array with decoded brick entries
                for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i++) {
                    glm::uvec3 out_pos = brick_pos * m_brick_size + enumBrickPos(i, m_brick_size);
                    if (glm::all(glm::lessThan(out_pos, m_volume_dim))) {
                        out[voxel_pos2idx(out_pos, m_volume_dim)] = brick_cache[thread_id][i];
                    }
                }
#else
                decodeBrick(brick_idx, &(out[pos2idx(brick_pos * m_brick_size, m_volume_dim)]), glm::clamp(m_volume_dim - brick_pos * m_brick_size, glm::uvec3(0u), glm::uvec3(m_brick_size)), inv_lod);
#endif
            }
        }
    }
}

void CompressedSegmentationVolume::decompressBrickTo(uint32_t* out, glm::uvec3 brick_pos, int inverse_lod, uint32_t* out_encoding_debug, std::vector<glm::uvec4>* out_palette_debug) const {
    const glm::uvec3 brickCount = getBrickCount();
    size_t brick_idx = brick_pos2idx(brick_pos, brickCount);
    // decode brick
    if(out_encoding_debug) {
        // parallel decode is not supporting the output encoding
        decodeBrickWithDebugEncoding(brick_idx, out, out_encoding_debug, out_palette_debug,
                                     glm::clamp(m_volume_dim - brick_pos * m_brick_size, glm::uvec3(0u),
                                                glm::uvec3(m_brick_size)), inverse_lod);
    }
    else {
            decodeBrick(brick_idx, out,
                        glm::clamp(m_volume_dim - brick_pos * m_brick_size, glm::uvec3(0u), glm::uvec3(m_brick_size)),
                        inverse_lod);
    }
}

bool CompressedSegmentationVolume::testLOD(const std::vector<uint32_t> &volume, const glm::uvec3 volume_dim) const {
    assert(volume.size() == volume_dim.x * volume_dim.y * volume_dim.z && "volume size does not match dimension");

    Logger(INFO) << "Running LOD compression test";

    MiniTimer timer;

    static constexpr int max_error_lines = 8;
    size_t error_count = 0; // reset for each LOD, but we return once any LOD has errors
    std::vector<uint32_t> out;
    out.resize(static_cast<size_t>(m_volume_dim.x) * static_cast<size_t>(m_volume_dim.y) * static_cast<size_t>(m_volume_dim.z));

    bool allgood = true;

    // check the LODs over increasing LOD brick size
    // we check the LAST element in each brick. Because later we may just write the LOD entry to this single element in cached bricks.
    // we skip LOD level 0, because that's already tested in the 'test' method of VolumeCompressionBase and is technically the volume without any LOD.
    int lod = 1;
    uint32_t multigrid_lod_start = m_brick_size * m_brick_size * m_brick_size;
    for (uint32_t width = 2; width <= m_brick_size; width *= 2) {
        timer.restart();
        Logger(INFO, true) << "Decode LOD " << lod << " with block width " << width;
        decompressLOD(lod, out);
        Logger(INFO) << "Decode LOD " << lod << " with block width " << width << " in " << timer.elapsed() << "s done. Test:";
        if (volume.size() != out.size()) {
            Logger(ERROR) << "Compressed in and out sizes don't match";
            Logger(ERROR) << "skipping other LODs...";
            Logger(INFO) << "-------------------------------------------------------------";
            return false;
        }

        // iterate over all bricks but only check this one LOD
        error_count = 0;
        const glm::uvec3 brickCount = getBrickCount();
        glm::uvec3 brick;
        size_t brick_idx = 0;
        #pragma omp parallel for default(none) private(brick) shared(width, volume_dim, volume, out, error_count, max_error_lines, brickCount, multigrid_lod_start)
        for (uint32_t z = 0u; z < brickCount.z; z++) {
            brick.z = z;
            for (brick.y = 0u; brick.y < brickCount.y; brick.y++) {
                for (brick.x = 0u; brick.x < brickCount.x; brick.x += m_cpu_threads) {

                    // construct target multigrid for this brick (a bit efficient since we only test one level here..)
                    std::vector<MultiGridNode> multigrid;
                    constructMultiGrid(multigrid, volume, volume_dim, brick * m_brick_size, m_brick_size);

                    // check all elements of this LoD
                    glm::uvec3 pos_in_brick;
                    for (pos_in_brick.z = 0; pos_in_brick.z < m_brick_size/width; pos_in_brick.z++) {
                        for (pos_in_brick.y = 0; pos_in_brick.y < m_brick_size/width; pos_in_brick.y += width) {
                            for (pos_in_brick.x = 0; pos_in_brick.x < m_brick_size/width; pos_in_brick.x += width) {
                                if(glm::any(glm::greaterThanEqual(brick * m_brick_size + pos_in_brick * width, volume_dim)))
                                    continue;

                                uint32_t i = voxel_pos2idx(brick * m_brick_size + pos_in_brick * width, volume_dim);
                                uint32_t expected_value = multigrid[multigrid_lod_start +
                                        voxel_pos2idx(pos_in_brick, glm::uvec3(m_brick_size / width))].label;

                                if (expected_value != out[i]) {
                                    #pragma omp critical
                                    {
                                        error_count++;
                                        if (error_count <= max_error_lines)
                                            Logger(ERROR) << "error at " << str(voxel_idx2pos(i, volume_dim)) << " in " << volume[i] << " != out " << out[i] << " multigrid lod start " << multigrid_lod_start;
                                        else if (error_count == max_error_lines + 1)
                                            Logger(ERROR) << "[...] skipping additional errors";
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }


        size_t lod_total_number_of_elements = ((volume_dim.x + width - 1) / width) * ((volume_dim.y + width - 1) / width) * ((volume_dim.z + width - 1) / width);
        Logger(INFO) << "finished with " << error_count << " / " << lod_total_number_of_elements << " errors ("
                     << (100.f * static_cast<float>(error_count) / static_cast<float>(lod_total_number_of_elements)) << "%)";

        allgood &= (error_count == 0);
        lod++;
        multigrid_lod_start += (m_brick_size / width) * (m_brick_size / width) * (m_brick_size / width);
    }

    if(allgood)
        Logger(DEBUG) << "no errors!";
    else
        Logger(ERROR) << "encountered errors!";

    Logger(INFO) << "-------------------------------------------------------------";
    return error_count == 0;
}

void CompressedSegmentationVolume::exportToFile(const std::string &path, bool verbose) {
    if (m_encodings.empty()) {
        Logger(ERROR) << "Compression was not yet computed. Call compress(..) first. Skipping.";
        return;
    }
    if (std::filesystem::exists(path)) {
        Logger(WARN) << "File " << path << " already exist. Skipping.";
        return;
    }
    try {
        // ToDo: if the path is only one file in root it has no parent_path() which causes an invalid argument
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    } catch(const std::filesystem::filesystem_error& e) {
        throw std::runtime_error("Filesystem error: could not create parent directories for path " + std::filesystem::path(path).string());
    }
    std::ofstream file(path, std::ios_base::out | std::ios::binary);
    if (!file.is_open()) {
        Logger(ERROR) << "Unable to open export file " << path << ". Skipping.";
        return;
    }

    // write header: 8 chars CMPSGVOL + 4 chars version number
    const char *magic_header = "CMPSGVOL";
    const char *version = "0013";
    /* VERSION HISTORY
     * 0001: initial version
     * 0002: adds booleans if RLE and rANS are used, as well as frequency tables for rANS
     * 0003: allows separating the detail buffer
     * 0004: remove RLE flag
     * 0010: paper release version
     * 0011: use rANS_mode instead of use_rANS, allow detail separation only with DOUBLE_TABLE_RANS
     * 0012: store max. brick palette size
     * 0013: split encoding buffers
     */
    file.write(magic_header, 8);
    file.write(version, 4);

    // write general info
    file.write(reinterpret_cast<char *>(&m_brick_size), sizeof(uint32_t));
    file.write(reinterpret_cast<char *>(&m_volume_dim), sizeof(glm::uvec3));
    file.write(reinterpret_cast<char *>(&m_rANS_mode), sizeof(RANSMode)); // since 0011
    file.write(reinterpret_cast<char *>(&m_max_brick_palette_count), sizeof(uint32_t)); // since 012
    if(m_rANS_mode != NO_RANS) {  // since 0002
        auto freq_table = m_rans.getFrequencyArray();
        for(int i = 0; i < 16; i++)
            file.write(reinterpret_cast<char *>(&freq_table[i]), sizeof(uint32_t));
    }
    if(m_rANS_mode == DOUBLE_TABLE_RANS) {
        auto freq_table = m_detail_rans.getFrequencyArray();
        for(int i = 0; i < 16; i++)
            file.write(reinterpret_cast<char *>(&freq_table[i]), sizeof(uint32_t));
    }

    // mapping of brick indices to encoding arrays
    file.write(reinterpret_cast<char *>(&m_brick_idx_to_enc_vector), sizeof(uint32_t)); // since 0013
    // write brick starts buffer
    size_t size = m_brick_starts.size();
    file.write(reinterpret_cast<char *>(&size), sizeof(size_t));
    file.write(reinterpret_cast<char *>(&m_brick_starts[0]), static_cast<long>(size * sizeof(m_brick_starts[0])));
    // write number of split encoding arrays, all split encodings, and index to split array mapping
    size = m_encodings.size();
    file.write(reinterpret_cast<char *>(&size), sizeof(size_t));  // since 0013
    for(const auto& enc : m_encodings) {  // since 0013
        size = enc.size();
        file.write(reinterpret_cast<char *>(&size), sizeof(size_t));
        file.write(reinterpret_cast<const char *>(&enc[0]), static_cast<long>(size * sizeof(enc[0])));
    }
    // write detail encoding if it is separated
    file.write(reinterpret_cast<char *>(&m_separate_detail), sizeof(bool)); // since 0003
    if(m_separate_detail) { // since 0003
        size = m_detail_starts.size(); // same as brickstarts
        file.write(reinterpret_cast<char *>(&size), sizeof(size_t));
        file.write(reinterpret_cast<char *>(&m_detail_starts[0]), static_cast<long>(size * sizeof(m_detail_starts[0])));

        // write number of split encoding buffers, all split encodings, and index to split array mapping
        size = m_detail_encoding.size();
        file.write(reinterpret_cast<char *>(&size), sizeof(size_t));  // since 0013
        for(const auto& enc : m_detail_encoding) {  // since 0013
            size = enc.size();
            file.write(reinterpret_cast<char *>(&size), sizeof(size_t));
            file.write(reinterpret_cast<const char *>(&enc[0]), static_cast<long>(size * sizeof(enc[0])));
        }
    }
    file.close();
    if(verbose)
        Logger(DEBUG) << "Exported Compressed Segmentation Volume to " << path;
}

bool CompressedSegmentationVolume::importFromFile(const std::string &path, bool verbose, bool verify) {
    std::ifstream fin(path, std::ios::in | std::ios::binary);
    if (!fin.is_open()) {
        if(verbose)
            Logger(ERROR) << "Unable to open import file " << path << ". Skipping.";
        return false;
    }

    clear();
    m_label = path;

    // check header and version
    char magic_header[9];
    char _version[5];
    fin.read(reinterpret_cast<char *>(magic_header), 8);
    magic_header[8] = '\0';
    if (std::string(magic_header) != "CMPSGVOL") {
        Logger(ERROR) << "File " << path << " is not a Compressed Segmentation Volume export. Missing header CMPSGVOL (is " << magic_header << "). Skipping.";
        return false;
    }
    fin.read(reinterpret_cast<char *>(_version), 4);
    _version[4] = '\0';
    int _numeric_version = std::stoi(std::string(_version));

    // backwards compatibility code:
    if (std::string(_version) == "0011") {
        Logger(WARN) << "Reading deprecated csgv file version " << _version << ". May lead to reduced rendering performance.";
    } else if (std::string(_version) != "0012" && std::string(_version) != "0013") {
        Logger(ERROR) << "Import does not support version " << _version << " of Compressed Segmentation Volume file " << path << ". Skipping.";
        return false;
    }

    // read the general data set info
    fin.read(reinterpret_cast<char *>(&m_brick_size), sizeof(uint32_t));
    fin.read(reinterpret_cast<char *>(&m_volume_dim), sizeof(glm::uvec3));
    fin.read(reinterpret_cast<char *>(&m_rANS_mode), sizeof(RANSMode));
    if (_numeric_version >= 12)
        fin.read(reinterpret_cast<char *>(&m_max_brick_palette_count), sizeof(uint32_t));
    else
        m_max_brick_palette_count = ~0u;
    if(m_rANS_mode != NO_RANS) {
        uint32_t freq_table[16];
        for(int i = 0; i < 16; i++)
            fin.read(reinterpret_cast<char *>(&freq_table[i]), sizeof(uint32_t));
        m_rans.recomputeFrequencyTables(freq_table);
    }
    if(m_rANS_mode == DOUBLE_TABLE_RANS) {
        uint32_t freq_table[16];
        for(int i = 0; i < 16; i++)
            fin.read(reinterpret_cast<char *>(&freq_table[i]), sizeof(uint32_t));
        m_detail_rans.recomputeFrequencyTables(freq_table);
    }
    if(_numeric_version >= 13)
        fin.read(reinterpret_cast<char *>(&m_brick_idx_to_enc_vector), sizeof(uint32_t));
    else
        m_brick_idx_to_enc_vector = ~0u;
    // read the data directly to our members
    size_t size;
    fin.read(reinterpret_cast<char *>(&size), sizeof(size_t));
    m_brick_starts.resize(size);
    fin.read(reinterpret_cast<char *>(&m_brick_starts[0]), static_cast<long>(size * sizeof(uint32_t)));
    // read split encoding count
    if(_numeric_version >= 13)
        fin.read(reinterpret_cast<char *>(&size), sizeof(size_t));
    else
        size = 1;
    m_encodings.resize(size);
    // read all single split encoding arrays
    for(int i = 0; i < m_encodings.size(); i++) {
        fin.read(reinterpret_cast<char *>(&size), sizeof(size_t));
        m_encodings[i].resize(size);
        fin.read(reinterpret_cast<char *>(&m_encodings[i][0]), static_cast<long>(size * sizeof(uint32_t)));
    }
    // if detail is separated, read buffers
    fin.read(reinterpret_cast<char *>(&m_separate_detail), sizeof(bool));
    if (m_separate_detail) {
        fin.read(reinterpret_cast<char *>(&size), sizeof(size_t));
        m_detail_starts.resize(size);
        if(m_detail_starts.size() != m_brick_starts.size())
            throw std::runtime_error("error importing file: brickstarts and detailstarts buffers must have equal size");
        fin.read(reinterpret_cast<char *>(&m_detail_starts[0]), static_cast<long>(size * sizeof(uint32_t)));

        if(_numeric_version >= 13)
            fin.read(reinterpret_cast<char *>(&size), sizeof(size_t));
        else
            size = 1;
        m_detail_encoding.resize(size);
        // read all single split encoding arrays
        for(int i = 0; i < m_detail_encoding.size(); i++) {
            fin.read(reinterpret_cast<char *>(&size), sizeof(size_t));
            m_detail_encoding[i].resize(size);
            fin.read(reinterpret_cast<char *>(&m_detail_encoding[i][0]), static_cast<long>(size * sizeof(uint32_t)));
        }
    }
    else {
        m_detail_starts.clear();
        m_detail_encoding.clear();
    }

    char single_byte;
    fin.read(&single_byte, 1);
    if(verbose && !fin.eof())
        Logger(WARN) << "Unexpected end of file during Compressed Segmentation Volume import!";
    fin.close();
    if(verbose)
        Logger(DEBUG) << "Imported Compressed Segmentation Volume from " << path << " with " << str(m_volume_dim)
        << " voxels and " << str(getBrickCount()) << " = " << getBrickIndexCount()
                      << " bricks for brick size " << m_brick_size << "^3"
                      << (isUsingSeparateDetail() ? " with seperated detail LoD" : "");

    if(verify) {
        Logger(DEBUG, true) << "verifying..";
        MiniTimer verifyTimer;
        if (!verifyCompression()) {
            Logger(DEBUG) << "verifying: FAILURE (" << verifyTimer.elapsed() << "s)";
            return false;
        } else {
            Logger(DEBUG) << "verifying: ok (" << verifyTimer.elapsed() << "s)";
            return true;
        }
    }
    return true;
}


void CompressedSegmentationVolume::freqEncodeBrick(const std::vector<uint32_t>& volume, size_t* brick_freq, glm::uvec3 start, glm::uvec3 volume_dim, bool detail_freq) const {
    std::vector<uint32_t> palette;
    palette.reserve(32);
    glm::uvec3 volume_pos, brick_pos;

    const uint32_t lod_count = getLodCountPerBrick();

    // we need to keep track of the current brick status from coarsest to finest level to determine the right operations
    // basically do an implicit decoding while we're encoding
    uint32_t parent_value;
    uint32_t value;
    uint32_t child_index;   // index of all children with the same coarser parent element, in 0 - 7, used for parent_value and neighbor-lookup index

    // construct the multigrid on this brick that we want to represent in this encoding
    std::vector<MultiGridNode> multigrid;
    constructMultiGrid(multigrid, volume, volume_dim, start, m_brick_size);

    // we start with the coarsest LOD, which is always a PALETTE_ADV of the max occuring value in the whole brick
    // we handle this here because it allows us to skip some special handling (for example checking if the palette is empty) in the following loop
    // in theory, we could start with a finer level here too and skip the first levels (= Carsten's original idea)
    uint32_t muligrid_lod_start = multigrid.size() - 1;
    if (multigrid[muligrid_lod_start].constant_subregion) {
        brick_freq[PALETTE_ADV | STOP_BIT]++;
    }
    else {
        brick_freq[PALETTE_ADV]++;
    }
    palette.push_back(multigrid[muligrid_lod_start].label);

    // now we iteratively refine from coarse (8 elements in the brick) to finest (brick_size^3 elements in the brick) levels
    uint32_t current_inv_lod = 1u;
    for (uint32_t lod_width = m_brick_size / 2u; lod_width > 0u; lod_width /= 2u) {
        // in the multigrid, LODs are ordered from finest to coarsest, so we have to go through them in reverse.
        uint32_t lod_dim = (m_brick_size/lod_width);
        uint32_t parent_multigrid_lod_start = muligrid_lod_start;
        muligrid_lod_start -= lod_dim * lod_dim * lod_dim;

        bool in_separate_detail = m_separate_detail && (current_inv_lod == lod_count - 1u);
        int current_lod_palette = 0;

        for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i += lod_width * lod_width * lod_width) {
            // we don't store any operations for grid nodes that would lie completely outside the volume
            // if this is problematic, and we would like to always handle a full brick, we could output anything here and thus just write PARENT_STOP.
            brick_pos = enumBrickPos(i, m_brick_size);
            volume_pos = start + brick_pos;
            if (glm::any(glm::greaterThanEqual(volume_pos, volume_dim)))
                continue;

            // every 8th element (we span 2*2*2=8 elements of the coarse LOD above), we fetch the new parent
            child_index = (i % (lod_width * lod_width * lod_width * 8)) / (lod_width * lod_width * lod_width);
            if (child_index == 0) {
                // if this subtree is already filled (because in a previous LOD we set a PARENT_STOP for this area), the last element of this block is set and we can skip it
                // note that this will also happen if this LOD block lies completely outside the volume because some parent would've been set to PARENT_STOP earlier
                // our parent spanned 8 elements of this finer current level, so we need to look at the element 7 indices further
                if (multigrid[parent_multigrid_lod_start +
                        voxel_pos2idx(brick_pos / lod_width / 2u, glm::uvec3(lod_dim / 2u))].constant_subregion) { //tmpBrick[i + (lod_width * lod_width * lod_width * 7)] != INVALID) {
                    i += (lod_width * lod_width * lod_width * 7);
                    continue;
                }
                parent_value = multigrid[parent_multigrid_lod_start +
                        voxel_pos2idx(brick_pos / lod_width / 2u, glm::uvec3(lod_dim / 2u))].label; //tmpBrick[i];
                assert(parent_value != INVALID && "parent element in brick was not set in previous LOD!");
            }

            value = multigrid[muligrid_lod_start + voxel_pos2idx(brick_pos / lod_width, glm::uvec3(lod_dim))].label;
            assert(value != INVALID && "Original volume mustn't contain the INVALID magic value!");

            uint32_t operation = 0u;
            // if the whole subtree from here has this parent_value, we can set a stop sign and fill the whole brick area of the subtree
            // note that grid nodes outside the volume are by definition also homogeneous
            if (lod_width >= 1 && multigrid[muligrid_lod_start +
                    voxel_pos2idx(brick_pos / lod_width, glm::uvec3(lod_dim))].constant_subregion) { //lod_width > 1 && isHomogeneousBrick(volume, volume_dim, volume_pos, {lod_width, lod_width, lod_width})) {
                operation = STOP_BIT;
            }
            // determine operation for the next entry
            if (value == parent_value)
                operation |= PARENT;
            else if (valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 0) == value)
                operation |= NEIGHBOR_X;
            else if (valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 1) == value)
                operation |= NEIGHBOR_Y;
            else if (valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 2) == value)
                operation |= NEIGHBOR_Z;
            else if (palette.back() == value)
                operation |= PALETTE_LAST;
            else {
                // reuse the n-X palette value where 0 < X < 17
                uint32_t palette_delta = static_cast<uint32_t>(std::find(palette.rbegin(), palette.rend(), value) - palette.rbegin());
                if(palette_delta < 17u && palette_delta < palette.size()) {
                    assert(palette.at(palette.size() - palette_delta - 1u) == value && "Palette value does not fit!");
                    assert(palette_delta > 0u && "the palette delta 0 should've been caught by the palette_last value!");
                    if(detail_freq && (current_inv_lod == lod_count - 1u))
                        brick_freq[16 + (operation | PALETTE_D)]++;
                    else
                        brick_freq[operation | PALETTE_D]++;
                    operation = palette_delta - 1u; // the "0" case is already handled by PALETTE_LAST, so we only consider case 1 - 16 in our 4 bits
                } else
                {  // if nothing helps, we add a completely new palette entry
                    current_lod_palette++;
                    palette.push_back(value);
                    operation |= PALETTE_ADV;
                }
            }
            assert(operation < 16u && "we only allow writing 4 bit operations!");
            if(detail_freq && (current_inv_lod == lod_count - 1u))
                brick_freq[16 + operation]++;
            else
                brick_freq[operation]++;

            assert(value != INVALID);
        }
        current_inv_lod++;
    }
}

void CompressedSegmentationVolume::compressForFrequencyTable(const std::vector<uint32_t>& volume, glm::uvec3 volume_dim, size_t freq_out[32], uint32_t subsampling_factor, bool detail_freq, bool verbose) {
    assert(m_brick_size > 0u && "brick size must be a power of 2 > 0");
    m_volume_dim = volume_dim;
    glm::uvec3 brickCount = getBrickCount();
    if(verbose) {
        Logger(INFO) << " running with " << m_cpu_threads << " threads on " << std::thread::hardware_concurrency() << " CPU cores";
        Logger(INFO) << " brick count: " << str(brickCount) << " = " << getBrickIndexCount() << " with brick size " << m_brick_size << "^3";
    }

    Logger(INFO, true) << " Prepass Progress 0.0%";
    MiniTimer progressTimer;
    MiniTimer totalTimer;
    int bricks_since_last_update = 0;

    // compute the next m_cpu_threads brick encodings in parallel
    size_t brick_freq[m_cpu_threads][32]; // last 16 elements are detail frequencies, if detail separation is used
    //std::vector<uint32_t> tmpBrick[m_cpu_threads];
    for (int thread_id = 0; thread_id < m_cpu_threads; thread_id++) {
        for(int i = 0; i < 32; i++) {
            brick_freq[thread_id][i] = 0ul;
        }
    }

    glm::uvec3 brick;
    size_t brick_idx = 0;
    for (brick.z = 0u; brick.z < brickCount.z; brick.z+=subsampling_factor) {
        for (brick.y = 0u; brick.y < brickCount.y; brick.y+=subsampling_factor) {
            for (brick.x = 0u; brick.x < brickCount.x; brick.x+=(subsampling_factor * m_cpu_threads)) {

                #pragma omp parallel num_threads(m_cpu_threads) default(none) shared(brick, brickCount, volume, brick_freq, subsampling_factor, detail_freq)
                {
                    unsigned int thread_id = omp_get_thread_num();
                    if (brick.x + thread_id*subsampling_factor < brickCount.x) {
                        // dummy compress the current brick
                        freqEncodeBrick(volume, brick_freq[thread_id],
                                        glm::uvec3(brick.x + thread_id * subsampling_factor, brick.y, brick.z) *
                                        m_brick_size, m_volume_dim, detail_freq);
                    }
                }

                // output a progress update
                bricks_since_last_update += m_cpu_threads;
                constexpr const double PROGRESS_UPDATE_INTERVAL = 2.;
                if (progressTimer.elapsed() >= PROGRESS_UPDATE_INTERVAL) {
                    float bricks_per_second = static_cast<float>(bricks_since_last_update / progressTimer.elapsed());
                    std::stringstream stream;
                    stream << " Prepass Progress " << std::fixed << std::setprecision(1) << static_cast<float>(brick_idx) / static_cast<float>(getBrickIndexCount()/ subsampling_factor / subsampling_factor / subsampling_factor) * 100.f << "%"
                           << " (" << std::setprecision(2) << (bricks_per_second * m_brick_size * m_brick_size * m_brick_size / 1000000.f)
                           << " million voxels/second)";
                    Logger(INFO, true) << stream.str();
                    progressTimer.restart();
                    bricks_since_last_update = 0;
                }
            }
        }
    }


    // sum up the frequencies
    #pragma omp parallel for default(none) shared(freq_out, brick_freq)
    for(int i = 0; i < 32; i++) {
        freq_out[i] = 0ul;
        for (int thread_id = 0; thread_id < m_cpu_threads; thread_id++) {
            freq_out[i] += brick_freq[thread_id][i];
        }
    }


    float total_seconds = totalTimer.elapsed();
    m_last_total_freq_prepass_seconds = total_seconds;
    if(verbose)
        Logger(INFO) << " Prepass Progress 100% in " << std::fixed << std::setprecision(3) << total_seconds << "s operation freq: " <<  arrayToString(freq_out, 16) << " | " << arrayToString(freq_out + 16, 16) ;
    else
        Logger(INFO) << " Prepass Progress 100% in " << std::fixed << std::setprecision(3) << total_seconds << "s";
}

}; // namespace vvv
