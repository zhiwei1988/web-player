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
 * @brief Access Unit (one video frame)
 */
struct AccessUnit {
    std::vector<NalUnit> nalUnits;
};

/**
 * @brief H.264/H.265 NAL unit parser
 */
class NalParser {
public:
    /**
     * @brief Load video file and parse NAL units
     * @param filePath path to video file
     * @param isH265 true for H.265/HEVC, false for H.264/AVC
     * @return true on success
     */
    bool LoadFile(const std::string& filePath, bool isH265);

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
     * @brief Get number of Access Units
     */
    size_t GetAccessUnitCount() const { return accessUnits_.size(); }

    /**
     * @brief Get Access Unit by index
     * @param index Access Unit index
     * @return pointer to Access Unit, nullptr if out of range
     */
    const AccessUnit* GetAccessUnit(size_t index) const;

    /**
     * @brief Get total file size
     */
    size_t GetFileSize() const { return fileSize_; }

    /**
     * @brief Get detected frame rate
     * @return frame rate in fps, or 25.0 if not detected
     */
    double GetFrameRate() const { return frameRate_; }

private:
    /**
     * @brief Parse NAL units from buffer
     * @param buffer raw video data
     */
    void ParseNalUnits(const std::vector<uint8_t>& buffer);

    /**
     * @brief Group NAL units into Access Units
     */
    void GroupIntoAccessUnits();

    /**
     * @brief Get NAL unit type (H.264/H.265 aware)
     * @param nal NAL unit data
     * @return NAL type
     */
    uint8_t GetNalType(const NalUnit& nal) const;

    std::vector<NalUnit> nalUnits_;
    std::vector<AccessUnit> accessUnits_;
    size_t fileSize_;
    bool isH265_;
    double frameRate_;
};

}  // namespace server

#endif  // NAL_PARSER_H
