#ifndef BITSTREAM_READER_H
#define BITSTREAM_READER_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace server {

/**
 * @brief Bit-level bitstream reader for SPS parsing
 */
class BitstreamReader {
public:
    /**
     * @brief Construct reader from byte buffer
     * @param data pointer to data
     * @param size data size in bytes
     */
    BitstreamReader(const uint8_t* data, size_t size);

    /**
     * @brief Read n bits (up to 32)
     * @param n number of bits to read
     * @return value read
     */
    uint32_t ReadBits(uint32_t n);

    /**
     * @brief Read 1 bit
     * @return 0 or 1
     */
    uint32_t ReadBit();

    /**
     * @brief Read unsigned Exp-Golomb coded value
     * @return decoded value
     */
    uint32_t ReadUE();

    /**
     * @brief Read signed Exp-Golomb coded value
     * @return decoded value
     */
    int32_t ReadSE();

    /**
     * @brief Skip n bits
     * @param n number of bits to skip
     */
    void SkipBits(uint32_t n);

    /**
     * @brief Check if more data available
     * @return true if more data can be read
     */
    bool HasMoreData() const;

private:
    const uint8_t* data_;
    size_t size_;
    size_t bytePos_;
    uint32_t bitPos_;
};

}  // namespace server

#endif  // BITSTREAM_READER_H
