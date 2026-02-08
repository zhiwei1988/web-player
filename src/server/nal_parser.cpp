#include "nal_parser.h"

#include <cstdio>
#include <fstream>

#include "sps_parser.h"

namespace server {

bool NalParser::LoadFile(const std::string& filePath, bool isH265) {
    isH265_ = isH265;
    frameRate_ = 25.0;  // Default
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

    // Parse frame rate from SPS
    uint8_t spsType = isH265_ ? 33 : 7;
    for (const auto& nal : nalUnits_) {
        if (GetNalType(nal) == spsType) {
            if (isH265_) {
                frameRate_ = SpsParser::ParseH265Fps(nal.data);
            } else {
                frameRate_ = SpsParser::ParseH264Fps(nal.data);
            }
            break;
        }
    }

    GroupIntoAccessUnits();

    std::printf("Loaded video file: %s\n", filePath.c_str());
    std::printf("NAL units count: %zu\n", nalUnits_.size());
    std::printf("Access units count: %zu\n", accessUnits_.size());
    std::printf("Detected frame rate: %.2f fps\n", frameRate_);
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

const AccessUnit* NalParser::GetAccessUnit(size_t index) const {
    if (index >= accessUnits_.size()) {
        return nullptr;
    }
    return &accessUnits_[index];
}

uint8_t NalParser::GetNalType(const NalUnit& nal) const {
    // Skip start code (3 or 4 bytes)
    size_t offset = 0;
    if (nal.data.size() >= 4 && nal.data[0] == 0 && nal.data[1] == 0) {
        if (nal.data[2] == 0 && nal.data[3] == 1) {
            offset = 4;
        } else if (nal.data[2] == 1) {
            offset = 3;
        }
    }

    if (offset == 0 || offset >= nal.data.size()) {
        return 0xFF;  // Invalid
    }

    if (isH265_) {
        // H.265: NAL type is bits 1-6 of first byte after start code
        return (nal.data[offset] >> 1) & 0x3F;
    } else {
        // H.264: NAL type is bits 0-4 of first byte after start code
        return nal.data[offset] & 0x1F;
    }
}

void NalParser::GroupIntoAccessUnits() {
    accessUnits_.clear();

    if (nalUnits_.empty()) {
        return;
    }

    AccessUnit currentAU;

    for (size_t i = 0; i < nalUnits_.size(); ++i) {
        uint8_t nalType = GetNalType(nalUnits_[i]);

        bool isNewAU = false;

        if (isH265_) {
            // H.265 NAL types:
            // 35 = AUD (Access Unit Delimiter)
            // 32 = VPS, 33 = SPS, 34 = PPS
            // 0-31 = VCL NAL units
            if (nalType == 35) {
                // AUD always starts new AU
                isNewAU = true;
            } else if (nalType >= 0 && nalType <= 31) {
                // VCL NAL: start new AU if current AU already has VCL NAL
                // Simplified: assume each VCL starts new AU unless it's the first NAL
                if (!currentAU.nalUnits.empty()) {
                    // Check if current AU already has a VCL NAL
                    for (const auto& nal : currentAU.nalUnits) {
                        uint8_t type = GetNalType(nal);
                        if (type >= 0 && type <= 31) {
                            isNewAU = true;
                            break;
                        }
                    }
                }
            }
        } else {
            // H.264 NAL types:
            // 9 = AUD
            // 7 = SPS, 8 = PPS
            // 1 = non-IDR slice, 5 = IDR slice
            if (nalType == 9) {
                // AUD always starts new AU
                isNewAU = true;
            } else if (nalType == 1 || nalType == 5) {
                // Slice NAL: start new AU if current AU already has a slice
                // Simplified: assume each slice starts new AU unless it's the first NAL
                if (!currentAU.nalUnits.empty()) {
                    for (const auto& nal : currentAU.nalUnits) {
                        uint8_t type = GetNalType(nal);
                        if (type == 1 || type == 5) {
                            isNewAU = true;
                            break;
                        }
                    }
                }
            }
        }

        if (isNewAU && !currentAU.nalUnits.empty()) {
            accessUnits_.push_back(std::move(currentAU));
            currentAU.nalUnits.clear();
        }

        currentAU.nalUnits.push_back(nalUnits_[i]);
    }

    // Add the last AU
    if (!currentAU.nalUnits.empty()) {
        accessUnits_.push_back(std::move(currentAU));
    }
}

}  // namespace server
