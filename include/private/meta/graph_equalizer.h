/*
 * Copyright (C) 2021 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2021 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-graph-equalizer
 * Created on: 3 авг. 2021 г.
 *
 * lsp-plugins-graph-equalizer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * lsp-plugins-graph-equalizer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with lsp-plugins-graph-equalizer. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef PRIVATE_META_GRAPH_EQUALIZER_H_
#define PRIVATE_META_GRAPH_EQUALIZER_H_

#include <lsp-plug.in/plug-fw/meta/types.h>
#include <lsp-plug.in/plug-fw/const.h>

#include <lsp-plug.in/dsp-units/misc/windows.h>
#include <lsp-plug.in/dsp-units/misc/envelope.h>

namespace lsp
{
    namespace meta
    {
        struct graph_equalizer_metadata
        {
            static constexpr size_t         SLOPE_MIN           = 2;
            static constexpr size_t         SLOPE_DFL           = 0;
            static constexpr float          FREQ_MIN            = SPEC_FREQ_MIN;
            static constexpr float          FREQ_MAX            = SPEC_FREQ_MAX;

            static constexpr size_t         FFT_RANK            = 13;
            static constexpr size_t         FFT_ITEMS           = 1 << FFT_RANK;
            static constexpr size_t         MESH_POINTS         = 640;
            static constexpr size_t         FILTER_MESH_POINTS  = MESH_POINTS + 2;
            static constexpr size_t         FFT_WINDOW          = dspu::windows::HANN;
            static constexpr size_t         FFT_ENVELOPE        = dspu::envelope::PINK_NOISE;

            static constexpr float          REACT_TIME_MIN      = 0.000;
            static constexpr float          REACT_TIME_MAX      = 1.000;
            static constexpr float          REACT_TIME_DFL      = 0.200;
            static constexpr float          REACT_TIME_STEP     = 0.001;

            static constexpr float          BAND_GAIN_MIN       = GAIN_AMP_M_36_DB;
            static constexpr float          BAND_GAIN_MAX       = GAIN_AMP_P_36_DB;
            static constexpr float          BAND_GAIN_DFL       = GAIN_AMP_0_DB;
            static constexpr float          BAND_GAIN_STEP      = 0.025f;

            static constexpr float          ZOOM_MIN            = GAIN_AMP_M_36_DB;
            static constexpr float          ZOOM_MAX            = GAIN_AMP_0_DB;
            static constexpr float          ZOOM_DFL            = GAIN_AMP_0_DB;
            static constexpr float          ZOOM_STEP           = 0.025f;

            static constexpr float          IN_GAIN_DFL         = 1.0f;
            static constexpr float          OUT_GAIN_DFL        = 1.0f;
            static constexpr size_t         MODE_DFL            = 0;

            static constexpr size_t         REFRESH_RATE        = 20;

            enum para_eq_mode_t
            {
                PEM_IIR,
                PEM_FIR,
                PEM_FFT,
                PEM_SPM
            };

            static const float band_frequencies[];
        };

        extern const meta::plugin_t graph_equalizer_x16_mono;
        extern const meta::plugin_t graph_equalizer_x16_stereo;
        extern const meta::plugin_t graph_equalizer_x16_lr;
        extern const meta::plugin_t graph_equalizer_x16_ms;
        extern const meta::plugin_t graph_equalizer_x32_mono;
        extern const meta::plugin_t graph_equalizer_x32_stereo;
        extern const meta::plugin_t graph_equalizer_x32_lr;
        extern const meta::plugin_t graph_equalizer_x32_ms;

    } // namespace meta
} // namespace lsp


#endif /* PRIVATE_META_GRAPH_EQUALIZER_H_ */
