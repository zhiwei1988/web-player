#include "bitstream_reader.h"

namespace server {

BitstreamReader::BitstreamReader(const uint8_t* data, size_t size)
    : data_(data), size_(size), bytePos_(0), bitPos_(0) {
}

uint32_t BitstreamReader::ReadBits(uint32_t n) {
    uint32_t value = 0;
    for (uint32_t i = 0; i < n; ++i) {
        value = (value << 1) | ReadBit();
    }
    return value;
}

uint32_t BitstreamReader::ReadBit() {
    if (bytePos_ >= size_) {
        return 0;
    }

    uint32_t bit = (data_[bytePos_] >> (7 - bitPos_)) & 1;
    bitPos_++;

    if (bitPos_ == 8) {
        bitPos_ = 0;
        bytePos_++;
    }

    return bit;
}

uint32_t BitstreamReader::ReadUE() {
    // Count leading zeros
    uint32_t leadingZeros = 0;
    while (ReadBit() == 0 && HasMoreData()) {
        leadingZeros++;
    }

    if (leadingZeros == 0) {
        return 0;
    }

    // Read remaining bits
    uint32_t value = (1 << leadingZeros) - 1 + ReadBits(leadingZeros);
    return value;
}

int32_t BitstreamReader::ReadSE() {
    uint32_t ue = ReadUE();
    // Map to signed: 0->0, 1->1, 2->-1, 3->2, 4->-2, ...
    if (ue & 1) {
        return static_cast<int32_t>((ue + 1) >> 1);
    } else {
        return -static_cast<int32_t>(ue >> 1);
    }
}

void BitstreamReader::SkipBits(uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        ReadBit();
    }
}

bool BitstreamReader::HasMoreData() const {
    return bytePos_ < size_;
}

}  // namespace server
