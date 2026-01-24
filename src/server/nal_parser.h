#ifndef NAL_PARSER_H
#define NAL_PARSER_H

#include <cstdint>
#include <string>
#include <vector>

namespace server {

/**
 * @brief NAL unit with start code
 */
struct NalUnit {
    std::vector<uint8_t> data;
};

/**
 * @brief H.264/H.265 NAL unit parser
 */
class NalParser {
public:
    /**
     * @brief Load video file and parse NAL units
     * @param filePath path to video file
     * @return true on success
     */
    bool LoadFile(const std::string& filePath);

    /**
     * @brief Get number of NAL units
     */
    size_t GetNalCount() const { return nalUnits_.size(); }

    /**
     * @brief Get NAL unit by index
     * @param index NAL unit index
     * @return pointer to NAL unit data, nullptr if out of range
     */
    const NalUnit* GetNalUnit(size_t index) const;

    /**
     * @brief Get total file size
     */
    size_t GetFileSize() const { return fileSize_; }

private:
    /**
     * @brief Parse NAL units from buffer
     * @param buffer raw video data
     */
    void ParseNalUnits(const std::vector<uint8_t>& buffer);

    std::vector<NalUnit> nalUnits_;
    size_t fileSize_;
};

}  // namespace server

#endif  // NAL_PARSER_H
