#include "nal_parser.h"

#include <cstdio>
#include <fstream>

namespace server {

bool NalParser::LoadFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::fprintf(stderr, "Failed to open file: %s\n", filePath.c_str());
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        std::fprintf(stderr, "Failed to read file: %s\n", filePath.c_str());
        return false;
    }

    fileSize_ = static_cast<size_t>(size);
    ParseNalUnits(buffer);

    std::printf("Loaded video file: %s\n", filePath.c_str());
    std::printf("NAL units count: %zu\n", nalUnits_.size());
    std::printf("File size: %.2f KB\n", fileSize_ / 1024.0);

    // Log first few NAL sizes
    std::printf("\nFirst 5 NAL units:\n");
    for (size_t i = 0; i < 5 && i < nalUnits_.size(); ++i) {
        std::printf("  NAL %zu: %zu bytes\n", i, nalUnits_[i].data.size());
    }

    return true;
}

void NalParser::ParseNalUnits(const std::vector<uint8_t>& buffer) {
    nalUnits_.clear();

    if (buffer.size() < 4) {
        return;
    }

    size_t start = 0;
    bool firstNalFound = false;

    for (size_t i = 0; i < buffer.size() - 3; ++i) {
        // Check for 4-byte start code: 0x00 0x00 0x00 0x01
        bool is4ByteStart = (buffer[i] == 0 && buffer[i + 1] == 0 &&
                             buffer[i + 2] == 0 && buffer[i + 3] == 1);

        // Check for 3-byte start code: 0x00 0x00 0x01
        // Make sure it's not part of a 4-byte start code
        bool is3ByteStart = !is4ByteStart && i > 0 &&
                            (buffer[i] == 0 && buffer[i + 1] == 0 && buffer[i + 2] == 1);

        if (is4ByteStart || is3ByteStart) {
            if (firstNalFound) {
                // Save the previous NAL unit
                NalUnit nal;
                nal.data.assign(buffer.begin() + start, buffer.begin() + i);
                nalUnits_.push_back(std::move(nal));
            }
            start = i;
            firstNalFound = true;

            // Skip start code bytes to avoid re-detecting
            i += is4ByteStart ? 3 : 2;
        }
    }

    // Add the last NAL unit
    if (firstNalFound && start < buffer.size()) {
        NalUnit nal;
        nal.data.assign(buffer.begin() + start, buffer.end());
        nalUnits_.push_back(std::move(nal));
    }
}

const NalUnit* NalParser::GetNalUnit(size_t index) const {
    if (index >= nalUnits_.size()) {
        return nullptr;
    }
    return &nalUnits_[index];
}

}  // namespace server
