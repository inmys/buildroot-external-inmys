/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

static int FUNC(rbsp_trailing_bits)(CodedBitstreamContext *ctx, RWContext *rw)
{
    int err;

    fixed(1, rbsp_stop_one_bit, 1);
    while (byte_alignment(rw) != 0)
        fixed(1, rbsp_alignment_zero_bit, 0);

    return 0;
}

static int FUNC(nal_unit_header)(CodedBitstreamContext *ctx, RWContext *rw,
                                 H264RawNALUnitHeader *current,
                                 uint32_t valid_type_mask)
{
    int err;

    fixed(1, forbidden_zero_bit, 0);
    ub(2, nal_ref_idc);
    ub(5, nal_unit_type);

    if (!(1 << current->nal_unit_type & valid_type_mask)) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid NAL unit type %d.\n",
               current->nal_unit_type);
        return AVERROR_INVALIDDATA;
    }

    if (current->nal_unit_type == 14 ||
        current->nal_unit_type == 20 ||
        current->nal_unit_type == 21) {
        if (current->nal_unit_type != 21)
            flag(svc_extension_flag);
        else
            flag(avc_3d_extension_flag);

        if (current->svc_extension_flag) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "SVC not supported.\n");
            return AVERROR_PATCHWELCOME;

        } else if (current->avc_3d_extension_flag) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "3DAVC not supported.\n");
            return AVERROR_PATCHWELCOME;

        } else {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "MVC not supported.\n");
            return AVERROR_PATCHWELCOME;
        }
    }

    return 0;
}

static int FUNC(scaling_list)(CodedBitstreamContext *ctx, RWContext *rw,
                              H264RawScalingList *current,
                              int size_of_scaling_list)
{
    int err, i, scale;

    scale = 8;
    for (i = 0; i < size_of_scaling_list; i++) {
        ses(delta_scale[i], -128, +127, 1, i);
        scale = (scale + current->delta_scale[i] + 256) % 256;
        if (scale == 0)
            break;
    }

    return 0;
}

static int FUNC(hrd_parameters)(CodedBitstreamContext *ctx, RWContext *rw,
                                H264RawHRD *current)
{
    int err, i;

    ue(cpb_cnt_minus1, 0, 31);
    ub(4, bit_rate_scale);
    ub(4, cpb_size_scale);

    for (i = 0; i <= current->cpb_cnt_minus1; i++) {
        ues(bit_rate_value_minus1[i], 0, UINT32_MAX - 1, 1, i);
        ues(cpb_size_value_minus1[i], 0, UINT32_MAX - 1, 1, i);
        flags(cbr_flag[i], 1, i);
    }

    ub(5, initial_cpb_removal_delay_length_minus1);
    ub(5, cpb_removal_delay_length_minus1);
    ub(5, dpb_output_delay_length_minus1);
    ub(5, time_offset_length);

    return 0;
}

static int FUNC(vui_parameters)(CodedBitstreamContext *ctx, RWContext *rw,
                                H264RawVUI *current, H264RawSPS *sps)
{
    int err;

    flag(aspect_ratio_info_present_flag);
    if (current->aspect_ratio_info_present_flag) {
        ub(8, aspect_ratio_idc);
        if (current->aspect_ratio_idc == 255) {
            ub(16, sar_width);
            ub(16, sar_height);
        }
    } else {
        infer(aspect_ratio_idc, 0);
    }

    flag(overscan_info_present_flag);
    if (current->overscan_info_present_flag)
        flag(overscan_appropriate_flag);

    flag(video_signal_type_present_flag);
    if (current->video_signal_type_present_flag) {
        ub(3, video_format);
        flag(video_full_range_flag);
        flag(colour_description_present_flag);
        if (current->colour_description_present_flag) {
            ub(8, colour_primaries);
            ub(8, transfer_characteristics);
            ub(8, matrix_coefficients);
        } else {
            infer(colour_primaries,         2);
            infer(transfer_characteristics, 2);
            infer(matrix_coefficients,      2);
        }
    } else {
        infer(video_format,             5);
        infer(video_full_range_flag,    0);
        infer(colour_primaries,         2);
        infer(transfer_characteristics, 2);
        infer(matrix_coefficients,      2);
    }

    flag(chroma_loc_info_present_flag);
    if (current->chroma_loc_info_present_flag) {
        ue(chroma_sample_loc_type_top_field,    0, 5);
        ue(chroma_sample_loc_type_bottom_field, 0, 5);
    } else {
        infer(chroma_sample_loc_type_top_field,    0);
        infer(chroma_sample_loc_type_bottom_field, 0);
    }

    flag(timing_info_present_flag);
    if (current->timing_info_present_flag) {
        u(32, num_units_in_tick, 1, UINT32_MAX);
        u(32, time_scale,        1, UINT32_MAX);
        flag(fixed_frame_rate_flag);
    } else {
        infer(fixed_frame_rate_flag, 0);
    }

    flag(nal_hrd_parameters_present_flag);
    if (current->nal_hrd_parameters_present_flag)
        CHECK(FUNC(hrd_parameters)(ctx, rw, &current->nal_hrd_parameters));

    flag(vcl_hrd_parameters_present_flag);
    if (current->vcl_hrd_parameters_present_flag)
        CHECK(FUNC(hrd_parameters)(ctx, rw, &current->vcl_hrd_parameters));

    if (current->nal_hrd_parameters_present_flag ||
        current->vcl_hrd_parameters_present_flag)
        flag(low_delay_hrd_flag);
    else
        infer(low_delay_hrd_flag, 1 - current->fixed_frame_rate_flag);

    flag(pic_struct_present_flag);

    flag(bitstream_restriction_flag);
    if (current->bitstream_restriction_flag) {
        flag(motion_vectors_over_pic_boundaries_flag);
        ue(max_bytes_per_pic_denom, 0, 16);
        ue(max_bits_per_mb_denom,   0, 16);
        // The current version of the standard constrains this to be in
        // [0,15], but older versions allow 16.
        ue(log2_max_mv_length_horizontal, 0, 16);
        ue(log2_max_mv_length_vertical,   0, 16);
        ue(max_num_reorder_frames,  0, H264_MAX_DPB_FRAMES);
        ue(max_dec_frame_buffering, 0, H264_MAX_DPB_FRAMES);
    } else {
        infer(motion_vectors_over_pic_boundaries_flag, 1);
        infer(max_bytes_per_pic_denom, 2);
        infer(max_bits_per_mb_denom,   1);
        infer(log2_max_mv_length_horizontal, 15);
        infer(log2_max_mv_length_vertical,   15);

        if ((sps->profile_idc ==  44 || sps->profile_idc ==  86 ||
             sps->profile_idc == 100 || sps->profile_idc == 110 ||
             sps->profile_idc == 122 || sps->profile_idc == 244) &&
            sps->constraint_set3_flag) {
            infer(max_num_reorder_frames,  0);
            infer(max_dec_frame_buffering, 0);
        } else {
            infer(max_num_reorder_frames,  H264_MAX_DPB_FRAMES);
            infer(max_dec_frame_buffering, H264_MAX_DPB_FRAMES);
        }
    }

    return 0;
}

static int FUNC(vui_parameters_default)(CodedBitstreamContext *ctx,
                                        RWContext *rw, H264RawVUI *current,
                                        H264RawSPS *sps)
{
    infer(aspect_ratio_idc, 0);

    infer(video_format,             5);
    infer(video_full_range_flag,    0);
    infer(colour_primaries,         2);
    infer(transfer_characteristics, 2);
    infer(matrix_coefficients,      2);

    infer(chroma_sample_loc_type_top_field,    0);
    infer(chroma_sample_loc_type_bottom_field, 0);

    infer(fixed_frame_rate_flag, 0);
    infer(low_delay_hrd_flag,    1);

    infer(pic_struct_present_flag, 0);

    infer(motion_vectors_over_pic_boundaries_flag, 1);
    infer(max_bytes_per_pic_denom, 2);
    infer(max_bits_per_mb_denom,   1);
    infer(log2_max_mv_length_horizontal, 15);
    infer(log2_max_mv_length_vertical,   15);

    if ((sps->profile_idc ==  44 || sps->profile_idc ==  86 ||
         sps->profile_idc == 100 || sps->profile_idc == 110 ||
         sps->profile_idc == 122 || sps->profile_idc == 244) &&
        sps->constraint_set3_flag) {
        infer(max_num_reorder_frames,  0);
        infer(max_dec_frame_buffering, 0);
    } else {
        infer(max_num_reorder_frames,  H264_MAX_DPB_FRAMES);
        infer(max_dec_frame_buffering, H264_MAX_DPB_FRAMES);
    }

    return 0;
}

static int FUNC(sps)(CodedBitstreamContext *ctx, RWContext *rw,
                     H264RawSPS *current)
{
    int err, i;

    HEADER("Sequence Parameter Set");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header,
                                1 << H264_NAL_SPS));

    ub(8, profile_idc);

    flag(constraint_set0_flag);
    flag(constraint_set1_flag);
    flag(constraint_set2_flag);
    flag(constraint_set3_flag);
    flag(constraint_set4_flag);
    flag(constraint_set5_flag);

    u(2, reserved_zero_2bits,  0, 0);

    ub(8, level_idc);

    ue(seq_parameter_set_id, 0, 31);

    if (current->profile_idc == 100 || current->profile_idc == 110 ||
        current->profile_idc == 122 || current->profile_idc == 244 ||
        current->profile_idc ==  44 || current->profile_idc ==  83 ||
        current->profile_idc ==  86 || current->profile_idc == 118 ||
        current->profile_idc == 128 || current->profile_idc == 138) {
        ue(chroma_format_idc, 0, 3);

        if (current->chroma_format_idc == 3)
            flag(separate_colour_plane_flag);
        else
            infer(separate_colour_plane_flag, 0);

        ue(bit_depth_luma_minus8,   0, 6);
        ue(bit_depth_chroma_minus8, 0, 6);

        flag(qpprime_y_zero_transform_bypass_flag);

        flag(seq_scaling_matrix_present_flag);
        if (current->seq_scaling_matrix_present_flag) {
            for (i = 0; i < ((current->chroma_format_idc != 3) ? 8 : 12); i++) {
                flags(seq_scaling_list_present_flag[i], 1, i);
                if (current->seq_scaling_list_present_flag[i]) {
                    if (i < 6)
                        CHECK(FUNC(scaling_list)(ctx, rw,
                                                 &current->scaling_list_4x4[i],
                                                 16));
                    else
                        CHECK(FUNC(scaling_list)(ctx, rw,
                                                 &current->scaling_list_8x8[i - 6],
                                                 64));
                }
            }
        }
    } else {
        infer(chroma_format_idc, current->profile_idc == 183 ? 0 : 1);

        infer(separate_colour_plane_flag, 0);
        infer(bit_depth_luma_minus8,      0);
        infer(bit_depth_chroma_minus8,    0);
    }

    ue(log2_max_frame_num_minus4, 0, 12);
    ue(pic_order_cnt_type, 0, 2);

    if (current->pic_order_cnt_type == 0) {
        ue(log2_max_pic_order_cnt_lsb_minus4, 0, 12);
    } else if (current->pic_order_cnt_type == 1) {
        flag(delta_pic_order_always_zero_flag);
        se(offset_for_non_ref_pic,         INT32_MIN + 1, INT32_MAX);
        se(offset_for_top_to_bottom_field, INT32_MIN + 1, INT32_MAX);
        ue(num_ref_frames_in_pic_order_cnt_cycle, 0, 255);

        for (i = 0; i < current->num_ref_frames_in_pic_order_cnt_cycle; i++)
            ses(offset_for_ref_frame[i], INT32_MIN + 1, INT32_MAX, 1, i);
    }

    ue(max_num_ref_frames, 0, H264_MAX_DPB_FRAMES);
    flag(gaps_in_frame_num_allowed_flag);

    ue(pic_width_in_mbs_minus1,        0, H264_MAX_MB_WIDTH);
    ue(pic_height_in_map_units_minus1, 0, H264_MAX_MB_HEIGHT);

    flag(frame_mbs_only_flag);
    if (!current->frame_mbs_only_flag)
        flag(mb_adaptive_frame_field_flag);

    flag(direct_8x8_inference_flag);

    flag(frame_cropping_flag);
    if (current->frame_cropping_flag) {
        ue(frame_crop_left_offset,   0, H264_MAX_WIDTH);
        ue(frame_crop_right_offset,  0, H264_MAX_WIDTH);
        ue(frame_crop_top_offset,    0, H264_MAX_HEIGHT);
        ue(frame_crop_bottom_offset, 0, H264_MAX_HEIGHT);
    }

    flag(vui_parameters_present_flag);
    if (current->vui_parameters_present_flag)
        CHECK(FUNC(vui_parameters)(ctx, rw, &current->vui, current));
    else
        CHECK(FUNC(vui_parameters_default)(ctx, rw, &current->vui, current));

    CHECK(FUNC(rbsp_trailing_bits)(ctx, rw));

    return 0;
}

static int FUNC(pps)(CodedBitstreamContext *ctx, RWContext *rw,
                     H264RawPPS *current, const H264RawSPS *sps)
{
    int err, i;

    HEADER("Picture Parameter Set");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header,
                                1 << H264_NAL_PPS));

    ue(pic_parameter_set_id, 0, 255);
    ue(seq_parameter_set_id, 0, 31);

    flag(entropy_coding_mode_flag);
    flag(bottom_field_pic_order_in_frame_present_flag);

    ue(num_slice_groups_minus1, 0, 7);
    if (current->num_slice_groups_minus1 > 0) {
        unsigned int pic_size;
        int iGroup;

        pic_size = (sps->pic_width_in_mbs_minus1 + 1) *
                   (sps->pic_height_in_map_units_minus1 + 1);

        ue(slice_group_map_type, 0, 6);

        if (current->slice_group_map_type == 0) {
            for (iGroup = 0; iGroup <= current->num_slice_groups_minus1; iGroup++)
                ues(run_length_minus1[iGroup], 0, pic_size - 1, 1, iGroup);

        } else if (current->slice_group_map_type == 2) {
            for (iGroup = 0; iGroup < current->num_slice_groups_minus1; iGroup++) {
                ues(top_left[iGroup],       0, pic_size - 1, 1, iGroup);
                ues(bottom_right[iGroup],
                    current->top_left[iGroup], pic_size - 1, 1, iGroup);
            }
        } else if (current->slice_group_map_type == 3 ||
                   current->slice_group_map_type == 4 ||
                   current->slice_group_map_type == 5) {
            flag(slice_group_change_direction_flag);
            ue(slice_group_change_rate_minus1, 0, pic_size - 1);
        } else if (current->slice_group_map_type == 6) {
            ue(pic_size_in_map_units_minus1, pic_size - 1, pic_size - 1);

            allocate(current->slice_group_id,
                     current->pic_size_in_map_units_minus1 + 1);
            for (i = 0; i <= current->pic_size_in_map_units_minus1; i++)
                us(av_log2(2 * current->num_slice_groups_minus1 + 1),
                   slice_group_id[i], 0, current->num_slice_groups_minus1, 1, i);
        }
    }

    ue(num_ref_idx_l0_default_active_minus1, 0, 31);
    ue(num_ref_idx_l1_default_active_minus1, 0, 31);

    flag(weighted_pred_flag);
    u(2, weighted_bipred_idc, 0, 2);

    se(pic_init_qp_minus26, -26 - 6 * sps->bit_depth_luma_minus8, +25);
    se(pic_init_qs_minus26, -26, +25);
    se(chroma_qp_index_offset, -12, +12);

    flag(deblocking_filter_control_present_flag);
    flag(constrained_intra_pred_flag);
    flag(redundant_pic_cnt_present_flag);

    if (more_rbsp_data(current->more_rbsp_data))
    {
        flag(transform_8x8_mode_flag);

        flag(pic_scaling_matrix_present_flag);
        if (current->pic_scaling_matrix_present_flag) {
            for (i = 0; i < 6 + (((sps->chroma_format_idc != 3) ? 2 : 6) *
                                 current->transform_8x8_mode_flag); i++) {
                flags(pic_scaling_list_present_flag[i], 1, i);
                if (current->pic_scaling_list_present_flag[i]) {
                    if (i < 6)
                        CHECK(FUNC(scaling_list)(ctx, rw,
                                                 &current->scaling_list_4x4[i],
                                                 16));
                    else
                        CHECK(FUNC(scaling_list)(ctx, rw,
                                                 &current->scaling_list_8x8[i - 6],
                                                 64));
                }
            }
        }

        se(second_chroma_qp_index_offset, -12, +12);
    } else {
        infer(transform_8x8_mode_flag, 0);
        infer(pic_scaling_matrix_present_flag, 0);
        infer(second_chroma_qp_index_offset, current->chroma_qp_index_offset);
    }

    CHECK(FUNC(rbsp_trailing_bits)(ctx, rw));

    return 0;
}
