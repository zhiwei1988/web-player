#ifndef SPS_PARSER_H
#define SPS_PARSER_H

#include <cstdint>
#include <vector>

namespace server {

/**
 * @brief SPS parser for extracting frame rate from H.264/H.265 bitstreams
 */
class SpsParser {
public:
    /**
     * @brief Parse frame rate from H.264 SPS NAL unit
     * @param spsData SPS NAL unit data (including start code)
     * @return frame rate in fps, or 25.0 if parsing fails
     */
    static double ParseH264Fps(const std::vector<uint8_t>& spsData);

    /**
     * @brief Parse frame rate from H.265 SPS NAL unit
     * @param spsData SPS NAL unit data (including start code)
     * @return frame rate in fps, or 25.0 if parsing fails
     */
    static double ParseH265Fps(const std::vector<uint8_t>& spsData);

private:
    /**
     * @brief Remove emulation prevention bytes (0x03) from RBSP
     * @param data NAL unit data
     * @return RBSP data
     */
    static std::vector<uint8_t> RemoveEmulationPrevention(const std::vector<uint8_t>& data);
};

}  // namespace server

#endif  // SPS_PARSER_H
