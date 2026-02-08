#include "sps_parser.h"

#include <cstdio>

#include "bitstream_reader.h"

namespace server {

static const double DEFAULT_FPS = 25.0;

std::vector<uint8_t> SpsParser::RemoveEmulationPrevention(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> rbsp;
    rbsp.reserve(data.size());

    for (size_t i = 0; i < data.size(); ++i) {
        // Skip 0x03 byte if preceded by 0x00 0x00
        if (i >= 2 && data[i] == 0x03 && data[i - 1] == 0x00 && data[i - 2] == 0x00) {
            continue;
        }
        rbsp.push_back(data[i]);
    }

    return rbsp;
}

double SpsParser::ParseH264Fps(const std::vector<uint8_t>& spsData) {
    // Skip start code (3 or 4 bytes) and NAL header (1 byte)
    size_t offset = 0;
    if (spsData.size() >= 4 && spsData[0] == 0 && spsData[1] == 0) {
        if (spsData[2] == 0 && spsData[3] == 1) {
            offset = 4;  // 4-byte start code
        } else if (spsData[2] == 1) {
            offset = 3;  // 3-byte start code
        }
    }

    if (offset == 0 || offset + 1 >= spsData.size()) {
        std::fprintf(stderr, "[SpsParser] Invalid H.264 SPS: no start code\n");
        return DEFAULT_FPS;
    }

    // Remove emulation prevention bytes
    std::vector<uint8_t> nalPayload(spsData.begin() + offset, spsData.end());
    std::vector<uint8_t> rbsp = RemoveEmulationPrevention(nalPayload);

    if (rbsp.size() < 4) {
        std::fprintf(stderr, "[SpsParser] H.264 SPS too short\n");
        return DEFAULT_FPS;
    }

    // Skip NAL header (1 byte)
    BitstreamReader reader(rbsp.data() + 1, rbsp.size() - 1);

    try {
        // profile_idc (8), constraint_setX_flag (8), level_idc (8)
        uint32_t profile_idc = reader.ReadBits(8);
        reader.SkipBits(8);  // constraints
        reader.SkipBits(8);  // level_idc

        // seq_parameter_set_id
        reader.ReadUE();

        // High profile complexity: chroma_format_idc, bit_depth, etc.
        if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
            profile_idc == 244 || profile_idc == 44 || profile_idc == 83 ||
            profile_idc == 86 || profile_idc == 118 || profile_idc == 128) {
            uint32_t chroma_format_idc = reader.ReadUE();
            if (chroma_format_idc == 3) {
                reader.SkipBits(1);  // separate_colour_plane_flag
            }
            reader.ReadUE();  // bit_depth_luma_minus8
            reader.ReadUE();  // bit_depth_chroma_minus8
            reader.SkipBits(1);  // qpprime_y_zero_transform_bypass_flag

            // seq_scaling_matrix_present_flag
            if (reader.ReadBit()) {
                uint32_t count = (chroma_format_idc != 3) ? 8 : 12;
                for (uint32_t i = 0; i < count; ++i) {
                    if (reader.ReadBit()) {  // seq_scaling_list_present_flag
                        uint32_t sizeOfScalingList = (i < 6) ? 16 : 64;
                        uint32_t lastScale = 8;
                        uint32_t nextScale = 8;
                        for (uint32_t j = 0; j < sizeOfScalingList; ++j) {
                            if (nextScale != 0) {
                                int32_t delta_scale = reader.ReadSE();
                                nextScale = (lastScale + delta_scale + 256) % 256;
                            }
                            lastScale = (nextScale == 0) ? lastScale : nextScale;
                        }
                    }
                }
            }
        }

        // log2_max_frame_num_minus4
        reader.ReadUE();

        // pic_order_cnt_type
        uint32_t pic_order_cnt_type = reader.ReadUE();
        if (pic_order_cnt_type == 0) {
            reader.ReadUE();  // log2_max_pic_order_cnt_lsb_minus4
        } else if (pic_order_cnt_type == 1) {
            reader.SkipBits(1);  // delta_pic_order_always_zero_flag
            reader.ReadSE();  // offset_for_non_ref_pic
            reader.ReadSE();  // offset_for_top_to_bottom_field
            uint32_t num_ref_frames_in_pic_order_cnt_cycle = reader.ReadUE();
            for (uint32_t i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; ++i) {
                reader.ReadSE();  // offset_for_ref_frame
            }
        }

        // max_num_ref_frames
        reader.ReadUE();

        // gaps_in_frame_num_value_allowed_flag
        reader.SkipBits(1);

        // pic_width_in_mbs_minus1, pic_height_in_map_units_minus1
        reader.ReadUE();
        reader.ReadUE();

        // frame_mbs_only_flag
        uint32_t frame_mbs_only_flag = reader.ReadBit();
        if (!frame_mbs_only_flag) {
            reader.SkipBits(1);  // mb_adaptive_frame_field_flag
        }

        // direct_8x8_inference_flag
        reader.SkipBits(1);

        // frame_cropping_flag
        if (reader.ReadBit()) {
            reader.ReadUE();  // frame_crop_left_offset
            reader.ReadUE();  // frame_crop_right_offset
            reader.ReadUE();  // frame_crop_top_offset
            reader.ReadUE();  // frame_crop_bottom_offset
        }

        // vui_parameters_present_flag
        if (!reader.ReadBit()) {
            std::fprintf(stderr, "[SpsParser] H.264 SPS: VUI not present, using default fps\n");
            return DEFAULT_FPS;
        }

        // aspect_ratio_info_present_flag
        if (reader.ReadBit()) {
            uint32_t aspect_ratio_idc = reader.ReadBits(8);
            if (aspect_ratio_idc == 255) {  // Extended_SAR
                reader.SkipBits(16);  // sar_width
                reader.SkipBits(16);  // sar_height
            }
        }

        // overscan_info_present_flag
        if (reader.ReadBit()) {
            reader.SkipBits(1);  // overscan_appropriate_flag
        }

        // video_signal_type_present_flag
        if (reader.ReadBit()) {
            reader.SkipBits(3);  // video_format
            reader.SkipBits(1);  // video_full_range_flag
            if (reader.ReadBit()) {  // colour_description_present_flag
                reader.SkipBits(8);  // colour_primaries
                reader.SkipBits(8);  // transfer_characteristics
                reader.SkipBits(8);  // matrix_coefficients
            }
        }

        // chroma_loc_info_present_flag
        if (reader.ReadBit()) {
            reader.ReadUE();  // chroma_sample_loc_type_top_field
            reader.ReadUE();  // chroma_sample_loc_type_bottom_field
        }

        // timing_info_present_flag
        if (!reader.ReadBit()) {
            std::fprintf(stderr, "[SpsParser] H.264 SPS: timing_info not present, using default fps\n");
            return DEFAULT_FPS;
        }

        uint32_t num_units_in_tick = reader.ReadBits(32);
        uint32_t time_scale = reader.ReadBits(32);

        if (num_units_in_tick == 0 || time_scale == 0) {
            std::fprintf(stderr, "[SpsParser] H.264 SPS: invalid timing_info, using default fps\n");
            return DEFAULT_FPS;
        }

        double fps = static_cast<double>(time_scale) / (2.0 * num_units_in_tick);
        return fps;

    } catch (...) {
        std::fprintf(stderr, "[SpsParser] H.264 SPS parsing failed, using default fps\n");
        return DEFAULT_FPS;
    }
}

double SpsParser::ParseH265Fps(const std::vector<uint8_t>& spsData) {
    // Skip start code (3 or 4 bytes) and NAL header (2 bytes for HEVC)
    size_t offset = 0;
    if (spsData.size() >= 4 && spsData[0] == 0 && spsData[1] == 0) {
        if (spsData[2] == 0 && spsData[3] == 1) {
            offset = 4;
        } else if (spsData[2] == 1) {
            offset = 3;
        }
    }

    if (offset == 0 || offset + 2 >= spsData.size()) {
        std::fprintf(stderr, "[SpsParser] Invalid H.265 SPS: no start code\n");
        return DEFAULT_FPS;
    }

    std::vector<uint8_t> nalPayload(spsData.begin() + offset, spsData.end());
    std::vector<uint8_t> rbsp = RemoveEmulationPrevention(nalPayload);

    if (rbsp.size() < 15) {
        std::fprintf(stderr, "[SpsParser] H.265 SPS too short\n");
        return DEFAULT_FPS;
    }

    // Skip NAL header (2 bytes)
    BitstreamReader reader(rbsp.data() + 2, rbsp.size() - 2);

    try {
        // sps_video_parameter_set_id (4 bits)
        reader.SkipBits(4);

        // sps_max_sub_layers_minus1 (3 bits)
        uint32_t sps_max_sub_layers_minus1 = reader.ReadBits(3);

        // sps_temporal_id_nesting_flag (1 bit)
        reader.SkipBits(1);

        // profile_tier_level
        reader.SkipBits(2);  // general_profile_space
        reader.SkipBits(1);  // general_tier_flag
        reader.SkipBits(5);  // general_profile_idc
        reader.SkipBits(32);  // general_profile_compatibility_flag[32]
        reader.SkipBits(1);  // general_progressive_source_flag
        reader.SkipBits(1);  // general_interlaced_source_flag
        reader.SkipBits(1);  // general_non_packed_constraint_flag
        reader.SkipBits(1);  // general_frame_only_constraint_flag
        reader.SkipBits(44);  // general_reserved_zero_44bits
        reader.SkipBits(8);  // general_level_idc

        // sub_layer_profile_present_flag, sub_layer_level_present_flag
        std::vector<uint32_t> sub_layer_profile_present_flag;
        std::vector<uint32_t> sub_layer_level_present_flag;
        for (uint32_t i = 0; i < sps_max_sub_layers_minus1; ++i) {
            sub_layer_profile_present_flag.push_back(reader.ReadBit());
            sub_layer_level_present_flag.push_back(reader.ReadBit());
        }

        if (sps_max_sub_layers_minus1 > 0) {
            for (uint32_t i = sps_max_sub_layers_minus1; i < 8; ++i) {
                reader.SkipBits(2);  // reserved_zero_2bits
            }
        }

        for (uint32_t i = 0; i < sps_max_sub_layers_minus1; ++i) {
            if (sub_layer_profile_present_flag[i]) {
                reader.SkipBits(88);  // sub_layer_profile_info
            }
            if (sub_layer_level_present_flag[i]) {
                reader.SkipBits(8);  // sub_layer_level_idc
            }
        }

        // sps_seq_parameter_set_id
        reader.ReadUE();

        // chroma_format_idc
        uint32_t chroma_format_idc = reader.ReadUE();

        if (chroma_format_idc == 3) {
            reader.SkipBits(1);  // separate_colour_plane_flag
        }

        // pic_width_in_luma_samples, pic_height_in_luma_samples
        reader.ReadUE();
        reader.ReadUE();

        // conformance_window_flag
        if (reader.ReadBit()) {
            reader.ReadUE();  // conf_win_left_offset
            reader.ReadUE();  // conf_win_right_offset
            reader.ReadUE();  // conf_win_top_offset
            reader.ReadUE();  // conf_win_bottom_offset
        }

        // bit_depth_luma_minus8, bit_depth_chroma_minus8
        reader.ReadUE();
        reader.ReadUE();

        // log2_max_pic_order_cnt_lsb_minus4
        reader.ReadUE();

        // sps_sub_layer_ordering_info_present_flag
        uint32_t sps_sub_layer_ordering_info_present_flag = reader.ReadBit();
        uint32_t start = sps_sub_layer_ordering_info_present_flag ? 0 : sps_max_sub_layers_minus1;
        for (uint32_t i = start; i <= sps_max_sub_layers_minus1; ++i) {
            reader.ReadUE();  // sps_max_dec_pic_buffering_minus1
            reader.ReadUE();  // sps_max_num_reorder_pics
            reader.ReadUE();  // sps_max_latency_increase_plus1
        }

        // log2_min_luma_coding_block_size_minus3
        reader.ReadUE();
        // log2_diff_max_min_luma_coding_block_size
        reader.ReadUE();
        // log2_min_luma_transform_block_size_minus2
        reader.ReadUE();
        // log2_diff_max_min_luma_transform_block_size
        reader.ReadUE();
        // max_transform_hierarchy_depth_inter
        reader.ReadUE();
        // max_transform_hierarchy_depth_intra
        reader.ReadUE();

        // scaling_list_enabled_flag
        if (reader.ReadBit()) {
            // sps_scaling_list_data_present_flag
            if (reader.ReadBit()) {
                // Skip scaling list data (complex, simplified here)
                for (uint32_t sizeId = 0; sizeId < 4; ++sizeId) {
                    uint32_t count = (sizeId == 3) ? 2 : 6;
                    for (uint32_t matrixId = 0; matrixId < count; ++matrixId) {
                        if (!reader.ReadBit()) {  // scaling_list_pred_mode_flag
                            reader.ReadUE();  // scaling_list_pred_matrix_id_delta
                        } else {
                            uint32_t coefNum = (1 << (4 + (sizeId << 1)));
                            if (coefNum > 64) coefNum = 64;
                            if (sizeId > 1) {
                                reader.ReadSE();  // scaling_list_dc_coef_minus8
                            }
                            for (uint32_t i = 0; i < coefNum; ++i) {
                                reader.ReadSE();  // scaling_list_delta_coef
                            }
                        }
                    }
                }
            }
        }

        // amp_enabled_flag, sample_adaptive_offset_enabled_flag
        reader.SkipBits(1);
        reader.SkipBits(1);

        // pcm_enabled_flag
        if (reader.ReadBit()) {
            reader.SkipBits(4);  // pcm_sample_bit_depth_luma_minus1
            reader.SkipBits(4);  // pcm_sample_bit_depth_chroma_minus1
            reader.ReadUE();  // log2_min_pcm_luma_coding_block_size_minus3
            reader.ReadUE();  // log2_diff_max_min_pcm_luma_coding_block_size
            reader.SkipBits(1);  // pcm_loop_filter_disabled_flag
        }

        // num_short_term_ref_pic_sets
        uint32_t num_short_term_ref_pic_sets = reader.ReadUE();

        // Skip short_term_ref_pic_set (complex, simplified)
        for (uint32_t i = 0; i < num_short_term_ref_pic_sets; ++i) {
            // Simplified: just skip based on flags
            if (i != 0 && reader.ReadBit()) {  // inter_ref_pic_set_prediction_flag
                if (i == num_short_term_ref_pic_sets) {
                    reader.ReadUE();  // delta_idx_minus1
                }
                reader.SkipBits(1);  // delta_rps_sign
                reader.ReadUE();  // abs_delta_rps_minus1
                // Simplified: assume small num_delta_pocs
                for (uint32_t j = 0; j < 16; ++j) {
                    if (reader.ReadBit()) {  // used_by_curr_pic_flag
                        reader.SkipBits(1);
                    }
                }
            } else {
                uint32_t num_negative_pics = reader.ReadUE();
                uint32_t num_positive_pics = reader.ReadUE();
                for (uint32_t j = 0; j < num_negative_pics; ++j) {
                    reader.ReadUE();  // delta_poc_s0_minus1
                    reader.SkipBits(1);  // used_by_curr_pic_s0_flag
                }
                for (uint32_t j = 0; j < num_positive_pics; ++j) {
                    reader.ReadUE();  // delta_poc_s1_minus1
                    reader.SkipBits(1);  // used_by_curr_pic_s1_flag
                }
            }
        }

        // long_term_ref_pics_present_flag
        if (reader.ReadBit()) {
            uint32_t num_long_term_ref_pics_sps = reader.ReadUE();
            for (uint32_t i = 0; i < num_long_term_ref_pics_sps; ++i) {
                reader.SkipBits(reader.ReadUE() + 4);  // Simplified
                reader.SkipBits(1);  // used_by_curr_pic_lt_sps_flag
            }
        }

        // sps_temporal_mvp_enabled_flag
        reader.SkipBits(1);

        // strong_intra_smoothing_enabled_flag
        reader.SkipBits(1);

        // vui_parameters_present_flag
        if (!reader.ReadBit()) {
            std::fprintf(stderr, "[SpsParser] H.265 SPS: VUI not present, using default fps\n");
            return DEFAULT_FPS;
        }

        // aspect_ratio_info_present_flag
        if (reader.ReadBit()) {
            uint32_t aspect_ratio_idc = reader.ReadBits(8);
            if (aspect_ratio_idc == 255) {  // EXTENDED_SAR
                reader.SkipBits(16);  // sar_width
                reader.SkipBits(16);  // sar_height
            }
        }

        // overscan_info_present_flag
        if (reader.ReadBit()) {
            reader.SkipBits(1);  // overscan_appropriate_flag
        }

        // video_signal_type_present_flag
        if (reader.ReadBit()) {
            reader.SkipBits(3);  // video_format
            reader.SkipBits(1);  // video_full_range_flag
            if (reader.ReadBit()) {  // colour_description_present_flag
                reader.SkipBits(8);  // colour_primaries
                reader.SkipBits(8);  // transfer_characteristics
                reader.SkipBits(8);  // matrix_coeffs
            }
        }

        // chroma_loc_info_present_flag
        if (reader.ReadBit()) {
            reader.ReadUE();  // chroma_sample_loc_type_top_field
            reader.ReadUE();  // chroma_sample_loc_type_bottom_field
        }

        // neutral_chroma_indication_flag, field_seq_flag, frame_field_info_present_flag
        reader.SkipBits(1);
        reader.SkipBits(1);
        reader.SkipBits(1);

        // default_display_window_flag
        if (reader.ReadBit()) {
            reader.ReadUE();  // def_disp_win_left_offset
            reader.ReadUE();  // def_disp_win_right_offset
            reader.ReadUE();  // def_disp_win_top_offset
            reader.ReadUE();  // def_disp_win_bottom_offset
        }

        // vui_timing_info_present_flag
        if (!reader.ReadBit()) {
            std::fprintf(stderr, "[SpsParser] H.265 SPS: timing_info not present, using default fps\n");
            return DEFAULT_FPS;
        }

        uint32_t vui_num_units_in_tick = reader.ReadBits(32);
        uint32_t vui_time_scale = reader.ReadBits(32);

        if (vui_num_units_in_tick == 0 || vui_time_scale == 0) {
            std::fprintf(stderr, "[SpsParser] H.265 SPS: invalid timing_info, using default fps\n");
            return DEFAULT_FPS;
        }

        double fps = static_cast<double>(vui_time_scale) / vui_num_units_in_tick;
        return fps;

    } catch (...) {
        std::fprintf(stderr, "[SpsParser] H.265 SPS parsing failed, using default fps\n");
        return DEFAULT_FPS;
    }
}

}  // namespace server
