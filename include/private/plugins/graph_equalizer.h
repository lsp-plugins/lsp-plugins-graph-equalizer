/*
 * Copyright (C) 2023 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2023 Vladimir Sadovnikov <sadko4u@gmail.com>
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

#ifndef PRIVATE_PLUGINS_GRAPH_EQUALIZER_H_
#define PRIVATE_PLUGINS_GRAPH_EQUALIZER_H_

#include <lsp-plug.in/plug-fw/plug.h>
#include <lsp-plug.in/plug-fw/core/IDBuffer.h>
#include <lsp-plug.in/dsp-units/ctl/Bypass.h>
#include <lsp-plug.in/dsp-units/filters/Equalizer.h>
#include <lsp-plug.in/dsp-units/util/Analyzer.h>
#include <lsp-plug.in/dsp-units/util/Delay.h>

#include <private/meta/graph_equalizer.h>

namespace lsp
{
    namespace plugins
    {
        /**
         * Graphic Equalizer Plugin Series
         */
        class graph_equalizer: public plug::Module
        {
            public:
                enum eq_mode_t
                {
                    EQ_MONO,
                    EQ_STEREO,
                    EQ_LEFT_RIGHT,
                    EQ_MID_SIDE
                };

            protected:
                enum chart_state_t
                {
                    CS_UPDATE       = 1 << 0,
                    CS_SYNC_AMP     = 1 << 1
                };

                enum fft_position_t
                {
                    FFTP_NONE,
                    FFTP_POST,
                    FFTP_PRE
                };

                typedef struct eq_band_t
                {
                    bool                bSolo;          // Solo
                    size_t              nSync;          // Chart state
                    float              *vTrRe;          // Transfer function (real part)
                    float              *vTrIm;          // Transfer function (imaginary part)

                    plug::IPort        *pGain;          // Gain port
                    plug::IPort        *pSolo;          // Solo port
                    plug::IPort        *pMute;          // Mute port
                    plug::IPort        *pEnable;        // Enable port
                    plug::IPort        *pVisibility;    // Filter visibility
                } eq_band_t;

                typedef struct eq_channel_t
                {
                    dspu::Equalizer     sEqualizer;     // Equalizer
                    dspu::Bypass        sBypass;        // Bypass
                    dspu::Delay         sDryDelay;      // Dry delay

                    size_t              nSync;          // Chart state
                    float               fInGain;        // Input gain
                    float               fOutGain;       // Output gain
                    eq_band_t          *vBands;         // Bands
                    float              *vIn;            // Input buffer
                    float              *vOut;           // Output buffer
                    float              *vDryBuf;        // Dry buffer
                    float              *vBuffer;        // Temporary buffer

                    float              *vTrRe;          // Transfer function (real part)
                    float              *vTrIm;          // Transfer function (imaginary part)

                    plug::IPort        *pIn;            // Input port
                    plug::IPort        *pOut;           // Output port
                    plug::IPort        *pInGain;        // Input gain
                    plug::IPort        *pTrAmp;         // Amplitude chart
                    plug::IPort        *pFft;           // FFT chart
                    plug::IPort        *pVisible;       // Visibility flag
                    plug::IPort        *pInMeter;       // Output level meter
                    plug::IPort        *pOutMeter;      // Output level meter
                } eq_channel_t;

            protected:
                inline dspu::equalizer_mode_t   get_eq_mode();
                void                            dump_channel(dspu::IStateDumper *v, const eq_channel_t *c) const;
                static void                     dump_band(dspu::IStateDumper *v, const eq_band_t *b);

            protected:
                dspu::Analyzer      sAnalyzer;      // Analyzer
                eq_channel_t       *vChannels;      // Equalizer channels
                size_t              nBands;         // Number of bands
                size_t              nMode;          // Equalize mode
                size_t              nFftPosition;   // FFT analysis position
                size_t              nSlope;         // Slope
                bool                bListen;        // Listen
                bool                bMatched;       // Matched transorm/Bilinear transform flag
                float               fInGain;        // Input gain
                float               fZoom;          // Zoom gain
                float              *vFreqs;         // Frequency list
                uint32_t           *vIndexes;       // FFT indexes
                core::IDBuffer     *pIDisplay;      // Inline display buffer

                plug::IPort        *pEqMode;        // Equalizer mode
                plug::IPort        *pSlope;         // Filter slope
                plug::IPort        *pListen;        // Mid-Side listen
                plug::IPort        *pInGain;        // Input gain
                plug::IPort        *pOutGain;       // Output gain
                plug::IPort        *pBypass;        // Bypass
                plug::IPort        *pFftMode;       // FFT mode
                plug::IPort        *pReactivity;    // FFT reactivity
                plug::IPort        *pShiftGain;     // Shift gain
                plug::IPort        *pZoom;          // Graph zoom
                plug::IPort        *pBalance;       // Output balance

            protected:
                void                do_destroy();

            public:
                explicit graph_equalizer(const meta::plugin_t *metadata, size_t bands, size_t mode);
                virtual ~graph_equalizer() override;

            public:
                virtual void        init(plug::IWrapper *wrapper, plug::IPort **ports) override;
                virtual void        destroy() override;

                virtual void        update_settings() override;
                virtual void        update_sample_rate(long sr) override;
                virtual void        ui_activated() override;

                virtual void        process(size_t samples) override;
                virtual bool        inline_display(plug::ICanvas *cv, size_t width, size_t height) override;

                virtual void        dump(dspu::IStateDumper *v) const override;
        };
    } /* namespace plugins */
} /* namespace lsp */

#endif /* PRIVATE_PLUGINS_GRAPH_EQUALIZER_H_ */
