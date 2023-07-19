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

#include <lsp-plug.in/plug-fw/meta/ports.h>
#include <lsp-plug.in/shared/meta/developers.h>
#include <private/meta/graph_equalizer.h>

#define LSP_PLUGINS_GRAPH_EQUALIZER_VERSION_MAJOR       1
#define LSP_PLUGINS_GRAPH_EQUALIZER_VERSION_MINOR       0
#define LSP_PLUGINS_GRAPH_EQUALIZER_VERSION_MICRO       15

#define LSP_PLUGINS_GRAPH_EQUALIZER_VERSION  \
    LSP_MODULE_VERSION( \
        LSP_PLUGINS_GRAPH_EQUALIZER_VERSION_MAJOR, \
        LSP_PLUGINS_GRAPH_EQUALIZER_VERSION_MINOR, \
        LSP_PLUGINS_GRAPH_EQUALIZER_VERSION_MICRO  \
    )

namespace lsp
{
    namespace meta
    {
        //-------------------------------------------------------------------------
        // Graphic Equalizer

        const float graph_equalizer_metadata::band_frequencies[] =
        {
            16.0f, 20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f, 125.0f,
            160.0f, 200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f, 800.0f, 1000.0f, 1250.0f,
            1600.0f, 2000.0f, 2500.0f, 3150.0f, 4000.0f, 5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f,
            16000.0f, 20000.0f
        };

        static const int plugin_classes[]           = { C_MULTI_EQ, -1 };
        static const int clap_features_mono[]       = { CF_AUDIO_EFFECT, CF_EQUALIZER, CF_MONO, -1 };
        static const int clap_features_stereo[]     = { CF_AUDIO_EFFECT, CF_EQUALIZER, CF_STEREO, -1 };

        static const port_item_t band_fft_mode[] =
        {
            { "Off",                    "metering.fft.off" },
            { "Post-eq",                "metering.fft.post_eq" },
            { "Pre-eq",                 "metering.fft.pre_eq" },
            { NULL, NULL }
        };

        static const port_item_t band_slopes[] =
        {
            { "BT48",                   "graph_eq.slope.bt48" },
            { "MT48",                   "graph_eq.slope.mt48" },
            { "BT72",                   "graph_eq.slope.bt72" },
            { "MT72",                   "graph_eq.slope.mt72" },
            { "BT96",                   "graph_eq.slope.bt96" },
            { "MT96",                   "graph_eq.slope.mt96" },
            { NULL, NULL }
        };

        static const port_item_t band_eq_modes[] =
        {
            { "IIR",                    "eq.type.iir" },
            { "FIR",                    "eq.type.fir" },
            { "FFT",                    "eq.type.fft" },
            { "SPM",                    "eq.type.spm" },
            { NULL, NULL }
        };

        static const port_item_t band_select_16lr[] =
        {
            { "Bands Left",             "graph_eq.bands_l" },
            { "Bands Right",            "graph_eq.bands_r" },
            { NULL, NULL }
        };

        static const port_item_t band_select_16ms[] =
        {
            { "Bands Middle",           "graph_eq.bands_m" },
            { "Bands Side",             "graph_eq.bands_s" },
            { NULL, NULL }
        };

        static const port_item_t band_select_32[] =
        {
            { "Bands 0-15",             "graph_eq.bands_0:15" },
            { "Bands 16-31",            "graph_eq.bands_16:31" },
            { NULL, NULL }
        };

        static const port_item_t band_select_32lr[] =
        {
            { "Bands Left 0-15",        "graph_eq.bands_l_0:15" },
            { "Bands Right 0-15",       "graph_eq.bands_r_0:15" },
            { "Bands Left 16-31",       "graph_eq.bands_l_16:31" },
            { "Bands Right 16-31",      "graph_eq.bands_r_16:31" },
            { NULL, NULL }
        };

        static const port_item_t band_select_32ms[] =
        {
            { "Bands Middle 0-15",      "graph_eq.bands_m_0:15" },
            { "Bands Side 0-15",        "graph_eq.bands_s_0:15" },
            { "Bands Middle 16-31",     "graph_eq.bands_m_16:31" },
            { "Bands Side 16-31",       "graph_eq.bands_s_16:31" },
            { NULL, NULL }
        };

        #define EQ_BAND(id, label, x, f) \
            SWITCH("xs" id "_" #x, "Band solo" label " " f, 0.0f), \
            SWITCH("xm" id "_" #x, "Band mute" label " " f, 0.0f), \
            SWITCH("xe" id "_" #x, "Band on" label " " f, 1.0f), \
            BLINK("fv" id "_" #x, "Filter visibility " label " " f), \
            LOG_CONTROL("g" id "_" #x, "Band gain" label " " f, U_GAIN_AMP, graph_equalizer_metadata::BAND_GAIN)

        #define EQ_BAND_MONO(x, f)      EQ_BAND("", "", x, f)
        #define EQ_BAND_STEREO(x, f)    EQ_BAND("", "", x, f)
        #define EQ_BAND_LR(x, f)        EQ_BAND("l", " Left", x, f), EQ_BAND("r", " Right", x, f)
        #define EQ_BAND_MS(x, f)        EQ_BAND("m", " Mid", x, f), EQ_BAND("s", " Side", x, f)

        #define EQ_MONO_PORTS \
                MESH("ag", "Amplitude graph", 2, graph_equalizer_metadata::FILTER_MESH_POINTS), \
                METER_GAIN("im", "Input signal meter", GAIN_AMP_P_12_DB), \
                METER_GAIN("sm", "Output signal meter", GAIN_AMP_P_12_DB), \
                MESH("fftg", "FFT graph", 2, graph_equalizer_metadata::MESH_POINTS)

        #define EQ_STEREO_PORTS \
                PAN_CTL("bal", "Output balance", 0.0f), \
                MESH("ag", "Amplitude graph", 2, graph_equalizer_metadata::FILTER_MESH_POINTS), \
                METER_GAIN("iml", "Input signal meter Left", GAIN_AMP_P_12_DB), \
                METER_GAIN("sml", "Output signal meter Left", GAIN_AMP_P_12_DB), \
                MESH("fftg_l", "FFT channel Left", 2, graph_equalizer_metadata::MESH_POINTS), \
                SWITCH("fftv_l", "FFT visibility Left", 1.0f), \
                METER_GAIN("imr", "Input signal meter Right", GAIN_AMP_P_12_DB), \
                METER_GAIN("smr", "Output signal meter Right", GAIN_AMP_P_12_DB), \
                MESH("fftg_r", "FFT channel Right", 2, graph_equalizer_metadata::MESH_POINTS), \
                SWITCH("fftv_r", "FFT visibility Right", 1.0f)

        #define EQ_LR_PORTS \
                PAN_CTL("bal", "Output balance", 0.0f), \
                MESH("ag_l", "Amplitude graph Left", 2, graph_equalizer_metadata::FILTER_MESH_POINTS), \
                METER_GAIN("iml", "Input signal meter Left", GAIN_AMP_P_12_DB), \
                METER_GAIN("sml", "Output signal meter Left", GAIN_AMP_P_12_DB), \
                MESH("fftg_l", "FFT channel Left", 2, graph_equalizer_metadata::MESH_POINTS), \
                SWITCH("fftv_l", "FFT visibility Left", 1.0f), \
                MESH("ag_r", "Amplitude graph Right", 2, graph_equalizer_metadata::FILTER_MESH_POINTS), \
                METER_GAIN("imr", "Input signal meter Right", GAIN_AMP_P_12_DB), \
                METER_GAIN("smr", "Output signal meter Right", GAIN_AMP_P_12_DB), \
                MESH("fftg_r", "FFT channel Right", 2, graph_equalizer_metadata::MESH_POINTS), \
                SWITCH("fftv_r", "FFT visibility Right", 1.0f)

        #define EQ_MS_PORTS \
                PAN_CTL("bal", "Output balance", 0.0f), \
                SWITCH("lstn", "Mid/Side listen", 0.0f), \
                AMP_GAIN100("gain_m", "Mid gain", GAIN_AMP_0_DB), \
                AMP_GAIN100("gain_s", "Side gain", GAIN_AMP_0_DB), \
                MESH("ag_m", "Amplitude graph Mid", 2, graph_equalizer_metadata::FILTER_MESH_POINTS), \
                METER_GAIN("iml", "Input signal meter Left", GAIN_AMP_P_12_DB), \
                METER_GAIN("sml", "Output signal meter Left", GAIN_AMP_P_12_DB), \
                MESH("fftg_m", "FFT channel Mid", 2, graph_equalizer_metadata::MESH_POINTS), \
                SWITCH("fftv_m", "FFT visibility Left", 1.0f), \
                MESH("ag_s", "Amplitude graph Side", 2, graph_equalizer_metadata::FILTER_MESH_POINTS), \
                METER_GAIN("imr", "Input signal meter Right", GAIN_AMP_P_12_DB), \
                METER_GAIN("smr", "Output signal meter Right", GAIN_AMP_P_12_DB), \
                MESH("fftg_s", "FFT channel Side", 2, graph_equalizer_metadata::MESH_POINTS), \
                SWITCH("fftv_s", "FFT visibility Right", 1.0f)

        #define EQ_COMMON \
            BYPASS, \
            AMP_GAIN("g_in", "Input gain", graph_equalizer_metadata::IN_GAIN_DFL, 10.0f), \
            AMP_GAIN("g_out", "Output gain", graph_equalizer_metadata::OUT_GAIN_DFL, 10.0f), \
            COMBO("mode", "Equalizer mode", 0, band_eq_modes), \
            COMBO("slope", "Filter slope", 0, band_slopes), \
            COMBO("fft", "FFT analysis", 0, band_fft_mode), \
            LOG_CONTROL("react", "FFT reactivity", U_MSEC, graph_equalizer_metadata::REACT_TIME), \
            AMP_GAIN("shift", "Shift gain", 1.0f, 100.0f), \
            LOG_CONTROL("zoom", "Graph zoom", U_GAIN_AMP, graph_equalizer_metadata::ZOOM)

        #define BAND_SELECT(fselect) \
            COMBO("fsel", "Band select", 0, fselect)

        #define EQ_BANDS_32X(band) \
            band(0, "16"), \
            band(1, "20"), \
            band(2, "25"), \
            band(3, "31.5"), \
            band(4, "40"), \
            band(5, "50"), \
            band(6, "63"), \
            band(7, "80"), \
            band(8, "100"), \
            band(9, "125"), \
            band(10, "160"), \
            band(11, "200"), \
            band(12, "250"), \
            band(13, "315"), \
            band(14, "400"), \
            band(15, "500"), \
            band(16, "630"), \
            band(17, "800"), \
            band(18, "1K"), \
            band(19, "1.25K"), \
            band(20, "1.6K"), \
            band(21, "2K"), \
            band(22, "2.5K"), \
            band(23, "3.15K"), \
            band(24, "4K"), \
            band(25, "5K"), \
            band(26, "6.3K"), \
            band(27, "8K"), \
            band(28, "10K"), \
            band(29, "12.5K"), \
            band(30, "16K"), \
            band(31, "20K")

        #define EQ_BANDS_16X(band) \
            band(0, "16"), \
            band(1, "25"), \
            band(2, "40"), \
            band(3, "63"), \
            band(4, "100"), \
            band(5, "160"), \
            band(6, "250"), \
            band(7, "400"), \
            band(8, "630"), \
            band(9, "1K"), \
            band(10, "1.6K"), \
            band(11, "2.5K"), \
            band(12, "4K"), \
            band(13, "6.3K"), \
            band(14, "10K"), \
            band(15, "16K")

        static const port_t graph_equalizer_x16_mono_ports[] =
        {
            PORTS_MONO_PLUGIN,
            EQ_COMMON,
            EQ_MONO_PORTS,
            EQ_BANDS_16X(EQ_BAND_MONO),

            PORTS_END
        };

        static const port_t graph_equalizer_x16_stereo_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            EQ_COMMON,
            EQ_STEREO_PORTS,
            EQ_BANDS_16X(EQ_BAND_STEREO),

            PORTS_END
        };

        static const port_t graph_equalizer_x16_lr_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            EQ_COMMON,
            BAND_SELECT(band_select_16lr),
            EQ_LR_PORTS,
            EQ_BANDS_16X(EQ_BAND_LR),

            PORTS_END
        };

        static const port_t graph_equalizer_x16_ms_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            EQ_COMMON,
            BAND_SELECT(band_select_16ms),
            EQ_MS_PORTS,
            EQ_BANDS_16X(EQ_BAND_MS),

            PORTS_END
        };

        static const port_t graph_equalizer_x32_mono_ports[] =
        {
            PORTS_MONO_PLUGIN,
            EQ_COMMON,
            BAND_SELECT(band_select_32),
            EQ_MONO_PORTS,
            EQ_BANDS_32X(EQ_BAND_MONO),

            PORTS_END
        };

        static const port_t graph_equalizer_x32_stereo_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            EQ_COMMON,
            BAND_SELECT(band_select_32),
            EQ_STEREO_PORTS,
            EQ_BANDS_32X(EQ_BAND_STEREO),

            PORTS_END
        };

        static const port_t graph_equalizer_x32_lr_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            EQ_COMMON,
            BAND_SELECT(band_select_32lr),
            EQ_LR_PORTS,
            EQ_BANDS_32X(EQ_BAND_LR),

            PORTS_END
        };

        static const port_t graph_equalizer_x32_ms_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            EQ_COMMON,
            BAND_SELECT(band_select_32ms),
            EQ_MS_PORTS,
            EQ_BANDS_32X(EQ_BAND_MS),

            PORTS_END
        };

        const meta::bundle_t graph_equalizer_bundle =
        {
            "graph_equalizer",
            "Graphic Equalizer",
            B_EQUALIZERS,
            "OQq5r1gr5tw",
            "This plugin performs graphic equalization of signal. Overall 16 or 32\nfrequency bands are available for correction in range of 72 dB (-36..+36 dB)."
        };

        const meta::plugin_t graph_equalizer_x16_mono =
        {
            "Grafischer Entzerrer x16 Mono",
            "Graphic Equalizer x16 Mono",
            "GE16M",
            &developers::v_sadovnikov,
            "graph_equalizer_x16_mono",
            LSP_LV2_URI("graph_equalizer_x16_mono"),
            LSP_LV2UI_URI("graph_equalizer_x16_mono"),
            "rvwk",
            LSP_LADSPA_GRAPH_EQUALIZER_BASE + 0,
            LSP_LADSPA_URI("graph_equalizer_x16_mono"),
            LSP_CLAP_URI("graph_equalizer_x16_mono"),
            LSP_PLUGINS_GRAPH_EQUALIZER_VERSION,
            plugin_classes,
            clap_features_mono,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            graph_equalizer_x16_mono_ports,
            "equalizer/graphic/mono.xml",
            NULL,
            mono_plugin_port_groups,
            &graph_equalizer_bundle
        };

        const meta::plugin_t graph_equalizer_x32_mono =
        {
            "Grafischer Entzerrer x32 Mono",
            "Graphic Equalizer x32 Mono",
            "GE32M",
            &developers::v_sadovnikov,
            "graph_equalizer_x32_mono",
            LSP_LV2_URI("graph_equalizer_x32_mono"),
            LSP_LV2UI_URI("graph_equalizer_x32_mono"),
            "vnca",
            LSP_LADSPA_GRAPH_EQUALIZER_BASE + 1,
            LSP_LADSPA_URI("graph_equalizer_x32_mono"),
            LSP_CLAP_URI("graph_equalizer_x32_mono"),
            LSP_PLUGINS_GRAPH_EQUALIZER_VERSION,
            plugin_classes,
            clap_features_mono,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            graph_equalizer_x32_mono_ports,
            "equalizer/graphic/mono.xml",
            NULL,
            mono_plugin_port_groups,
            &graph_equalizer_bundle
        };

        const meta::plugin_t graph_equalizer_x16_stereo =
        {
            "Grafischer Entzerrer x16 Stereo",
            "Graphic Equalizer x16 Stereo",
            "GE16S",
            &developers::v_sadovnikov,
            "graph_equalizer_x16_stereo",
            LSP_LV2_URI("graph_equalizer_x16_stereo"),
            LSP_LV2UI_URI("graph_equalizer_x16_stereo"),
            "argl",
            LSP_LADSPA_GRAPH_EQUALIZER_BASE + 2,
            LSP_LADSPA_URI("graph_equalizer_x16_stereo"),
            LSP_CLAP_URI("graph_equalizer_x16_stereo"),
            LSP_PLUGINS_GRAPH_EQUALIZER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            graph_equalizer_x16_stereo_ports,
            "equalizer/graphic/stereo.xml",
            NULL,
            stereo_plugin_port_groups,
            &graph_equalizer_bundle
        };

        const meta::plugin_t graph_equalizer_x32_stereo =
        {
            "Grafischer Entzerrer x32 Stereo",
            "Graphic Equalizer x32 Stereo",
            "GE32S",
            &developers::v_sadovnikov,
            "graph_equalizer_x32_stereo",
            LSP_LV2_URI("graph_equalizer_x32_stereo"),
            LSP_LV2UI_URI("graph_equalizer_x32_stereo"),
            "nvsd",
            LSP_LADSPA_GRAPH_EQUALIZER_BASE + 3,
            LSP_LADSPA_URI("graph_equalizer_x32_stereo"),
            LSP_CLAP_URI("graph_equalizer_x32_stereo"),
            LSP_PLUGINS_GRAPH_EQUALIZER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            graph_equalizer_x32_stereo_ports,
            "equalizer/graphic/stereo.xml",
            NULL,
            stereo_plugin_port_groups,
            &graph_equalizer_bundle
        };

        const meta::plugin_t graph_equalizer_x16_lr =
        {
            "Grafischer Entzerrer x16 LeftRight",
            "Graphic Equalizer x16 LeftRight",
            "GE16LR",
            &developers::v_sadovnikov,
            "graph_equalizer_x16_lr",
            LSP_LV2_URI("graph_equalizer_x16_lr"),
            LSP_LV2UI_URI("graph_equalizer_x16_lr"),
            "zefi",
            LSP_LADSPA_GRAPH_EQUALIZER_BASE + 4,
            LSP_LADSPA_URI("graph_equalizer_x16_lr"),
            LSP_CLAP_URI("graph_equalizer_x16_lr"),
            LSP_PLUGINS_GRAPH_EQUALIZER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            graph_equalizer_x16_lr_ports,
            "equalizer/graphic/lr.xml",
            NULL,
            stereo_plugin_port_groups,
            &graph_equalizer_bundle
        };

        const meta::plugin_t graph_equalizer_x32_lr =
        {
            "Grafischer Entzerrer x32 LeftRight",
            "Graphic Equalizer x32 LeftRight",
            "GE32LR",
            &developers::v_sadovnikov,
            "graph_equalizer_x32_lr",
            LSP_LV2_URI("graph_equalizer_x32_lr"),
            LSP_LV2UI_URI("graph_equalizer_x32_lr"),
            "0heu",
            LSP_LADSPA_GRAPH_EQUALIZER_BASE + 5,
            LSP_LADSPA_URI("graph_equalizer_x32_lr"),
            LSP_CLAP_URI("graph_equalizer_x32_lr"),
            LSP_PLUGINS_GRAPH_EQUALIZER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            graph_equalizer_x32_lr_ports,
            "equalizer/graphic/lr.xml",
            NULL,
            stereo_plugin_port_groups,
            &graph_equalizer_bundle
        };

        const meta::plugin_t graph_equalizer_x16_ms =
        {
            "Grafischer Entzerrer x16 MidSide",
            "Graphic Equalizer x16 MidSide",
            "GE16MS",
            &developers::v_sadovnikov,
            "graph_equalizer_x16_ms",
            LSP_LV2_URI("graph_equalizer_x16_ms"),
            LSP_LV2UI_URI("graph_equalizer_x16_ms"),
            "woys",
            LSP_LADSPA_GRAPH_EQUALIZER_BASE + 6,
            LSP_LADSPA_URI("graph_equalizer_x16_ms"),
            LSP_CLAP_URI("graph_equalizer_x16_ms"),
            LSP_PLUGINS_GRAPH_EQUALIZER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            graph_equalizer_x16_ms_ports,
            "equalizer/graphic/ms.xml",
            NULL,
            stereo_plugin_port_groups,
            &graph_equalizer_bundle
        };

        const meta::plugin_t graph_equalizer_x32_ms =
        {
            "Grafischer Entzerrer x32 MidSide",
            "Graphic Equalizer x32 MidSide",
            "GE32MS",
            &developers::v_sadovnikov,
            "graph_equalizer_x32_ms",
            LSP_LV2_URI("graph_equalizer_x32_ms"),
            LSP_LV2UI_URI("graph_equalizer_x32_ms"),
            "ku8j",
            LSP_LADSPA_GRAPH_EQUALIZER_BASE + 7,
            LSP_LADSPA_URI("graph_equalizer_x32_ms"),
            LSP_CLAP_URI("graph_equalizer_x32_ms"),
            LSP_PLUGINS_GRAPH_EQUALIZER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            graph_equalizer_x32_ms_ports,
            "equalizer/graphic/ms.xml",
            NULL,
            stereo_plugin_port_groups,
            &graph_equalizer_bundle
        };
    } // namespace meta
} // namespace lsp
