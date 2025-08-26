/*
 * Copyright (C) 2025 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2025 Vladimir Sadovnikov <sadko4u@gmail.com>
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

#include <lsp-plug.in/common/alloc.h>
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/plug-fw/core/AudioBuffer.h>
#include <lsp-plug.in/stdlib/math.h>

#include <lsp-plug.in/shared/debug.h>
#include <lsp-plug.in/shared/id_colors.h>

#include <private/plugins/graph_equalizer.h>

#define EQ_BUFFER_SIZE          0x400U

namespace lsp
{
    namespace plugins
    {
        //-------------------------------------------------------------------------
        inline namespace
        {
            typedef struct plugin_settings_t
            {
                const meta::plugin_t   *metadata;
                uint8_t                 bands;
                uint8_t                 mode;
            } plugin_settings_t;

            static const meta::plugin_t *plugins[] =
            {
                &meta::graph_equalizer_x16_mono,
                &meta::graph_equalizer_x16_stereo,
                &meta::graph_equalizer_x16_lr,
                &meta::graph_equalizer_x16_ms,
                &meta::graph_equalizer_x32_mono,
                &meta::graph_equalizer_x32_stereo,
                &meta::graph_equalizer_x32_lr,
                &meta::graph_equalizer_x32_ms
            };

            static const plugin_settings_t plugin_settings[] =
            {
                { &meta::graph_equalizer_x16_mono,   16, graph_equalizer::EQ_MONO         },
                { &meta::graph_equalizer_x16_stereo, 16, graph_equalizer::EQ_STEREO       },
                { &meta::graph_equalizer_x16_lr,     16, graph_equalizer::EQ_LEFT_RIGHT   },
                { &meta::graph_equalizer_x16_ms,     16, graph_equalizer::EQ_MID_SIDE     },
                { &meta::graph_equalizer_x32_mono,   32, graph_equalizer::EQ_MONO         },
                { &meta::graph_equalizer_x32_stereo, 32, graph_equalizer::EQ_STEREO       },
                { &meta::graph_equalizer_x32_lr,     32, graph_equalizer::EQ_LEFT_RIGHT   },
                { &meta::graph_equalizer_x32_ms,     32, graph_equalizer::EQ_MID_SIDE     },

                { NULL, 0, false }
            };

            static plug::Module *plugin_factory(const meta::plugin_t *meta)
            {
                for (const plugin_settings_t *s = plugin_settings; s->metadata != NULL; ++s)
                    if (s->metadata == meta)
                        return new graph_equalizer(s->metadata, s->bands, s->mode);
                return NULL;
            }

            static plug::Factory factory(plugin_factory, plugins, 8);
        } /* inline namespace */

        //-------------------------------------------------------------------------
        graph_equalizer::graph_equalizer(const meta::plugin_t *metadata, size_t bands, size_t mode):
            plug::Module(metadata)
        {
            vChannels       = NULL;
            nBands          = bands;
            nMode           = mode;
            nSlope          = -1;
            bListen         = false;
            bMatched        = false;
            fInGain         = 1.0f;
            fZoom           = 1.0f;
            vFreqs          = NULL;
            vIndexes        = NULL;
            pIDisplay       = NULL;

            pEqMode         = NULL;
            pSlope          = NULL;
            pListen         = NULL;
            pInGain         = NULL;
            pOutGain        = NULL;
            pBypass         = NULL;
            pReactivity     = NULL;
            pShiftGain      = NULL;
            pZoom           = NULL;
            pBalance        = NULL;
        }

        graph_equalizer::~graph_equalizer()
        {
            do_destroy();
        }

        void graph_equalizer::init(plug::IWrapper *wrapper, plug::IPort **ports)
        {
            // Pass wrapper
            plug::Module::init(wrapper, ports);

            // Determine number of channels
            size_t channels     = (nMode == EQ_MONO) ? 1 : 2;
            size_t max_latency  = 0;

            // Allocate channels
            vChannels           = new eq_channel_t[channels];
            if (vChannels == NULL)
                return;

            // Initialize global parameters
            fInGain             = 1.0f;
            bListen             = false;

            // Allocate indexes
            vIndexes            = new uint32_t[meta::graph_equalizer_metadata::MESH_POINTS];
            if (vIndexes == NULL)
                return;

            // Allocate buffer
            size_t allocate     = (EQ_BUFFER_SIZE*4 + (nBands + 1)*meta::graph_equalizer_metadata::MESH_POINTS*2) * channels + meta::graph_equalizer_metadata::MESH_POINTS;
            float *abuf         = new float[allocate];
            if (abuf == NULL)
                return;
            lsp_guard_assert(float *save = &abuf[allocate]);

            // Clear all floating-point buffers
            dsp::fill_zero(abuf, allocate);

            vFreqs              = abuf;
            abuf               += meta::graph_equalizer_metadata::MESH_POINTS;

            // Allocate channel data
            for (size_t i=0; i<channels; ++i)
            {
                // Allocate data
                eq_channel_t *c     = &vChannels[i];
                c->nSync            = CS_UPDATE;
                c->fInGain          = 1.0f;
                c->fOutGain         = 1.0f;
                c->vBands           = new eq_band_t[nBands];
                if (c->vBands == NULL)
                    return;

                c->vIn              = NULL;
                c->vOut             = NULL;
                c->vSend            = NULL;
                c->vReturn          = NULL;
                c->vInPtr           = NULL;
                c->vExtPtr          = NULL;
                c->vDryBuf          = advance_ptr<float>(abuf, EQ_BUFFER_SIZE);
                c->vInBuffer        = advance_ptr<float>(abuf, EQ_BUFFER_SIZE);
                c->vOutBuffer       = advance_ptr<float>(abuf, EQ_BUFFER_SIZE);
                c->vExtBuffer       = advance_ptr<float>(abuf, EQ_BUFFER_SIZE);
                c->vTrRe            = advance_ptr<float>(abuf, meta::graph_equalizer_metadata::MESH_POINTS);
                c->vTrIm            = advance_ptr<float>(abuf, meta::graph_equalizer_metadata::MESH_POINTS);

                c->pIn              = NULL;
                c->pOut             = NULL;
                c->pSend            = NULL;
                c->pReturn          = NULL;
                c->pInGain          = NULL;
                c->pTrAmp           = NULL;
                c->pFftInSwitch     = NULL;
                c->pFftOutSwitch    = NULL;
                c->pFftExtSwitch    = NULL;
                c->pFftInMesh       = NULL;
                c->pFftOutMesh      = NULL;
                c->pFftExtMesh      = NULL;
                c->pVisible         = NULL;
                c->pInMeter         = NULL;
                c->pOutMeter        = NULL;

                // Initialize equalizer
                c->sEqualizer.init(nBands, meta::graph_equalizer_metadata::FFT_RANK);
                max_latency         = lsp_max(max_latency, c->sEqualizer.max_latency());

                for (size_t j=0; j<nBands; ++j)
                {
                    eq_band_t *b    = &c->vBands[j];

                    b->bSolo        = false;
                    b->nSync        = CS_UPDATE;
                    b->vTrRe        = advance_ptr<float>(abuf, meta::graph_equalizer_metadata::MESH_POINTS);
                    b->vTrIm        = advance_ptr<float>(abuf, meta::graph_equalizer_metadata::MESH_POINTS);

                    b->pGain        = NULL;
                    b->pSolo        = NULL;
                    b->pMute        = NULL;
                    b->pEnable      = NULL;
                    b->pVisibility  = NULL;
                }
            }
            lsp_assert(abuf <= save);

            // Initialize latency compensation delay
            for (size_t i=0; i<channels; ++i)
            {
                eq_channel_t *c     = &vChannels[i];
                if (!c->sDryDelay.init(max_latency))
                    return;
            }

            // Bind ports
            size_t port_id          = 0;

            // Bind audio ports
            lsp_trace("Binding audio ports");
            for (size_t i=0; i<channels; ++i)
                BIND_PORT(vChannels[i].pIn);

            for (size_t i=0; i<channels; ++i)
                BIND_PORT(vChannels[i].pOut);

            // Bind common ports
            lsp_trace("Binding common ports");
            BIND_PORT(pBypass);
            BIND_PORT(pInGain);
            BIND_PORT(pOutGain);
            BIND_PORT(pEqMode);
            BIND_PORT(pSlope);
            BIND_PORT(pReactivity);
            BIND_PORT(pShiftGain);
            BIND_PORT(pZoom);
            if ((nMode == EQ_LEFT_RIGHT) || (nMode == EQ_MID_SIDE))
                SKIP_PORT("Separate channels link");

            // Communication
            SKIP_PORT("Send name");
            for (size_t i=0; i<channels; ++i)
                BIND_PORT(vChannels[i].pSend);
            SKIP_PORT("Return name");
            for (size_t i=0; i<channels; ++i)
                BIND_PORT(vChannels[i].pReturn);

            // Meters
            for (size_t i=0; i<channels; ++i)
            {
                eq_channel_t *c     = &vChannels[i];

                BIND_PORT(c->pFftInSwitch);
                BIND_PORT(c->pFftOutSwitch);
                BIND_PORT(c->pFftExtSwitch);
                BIND_PORT(c->pFftInMesh);
                BIND_PORT(c->pFftOutMesh);
                BIND_PORT(c->pFftExtMesh);
            }

            // Skip band select port
            if (nBands > 16)
                SKIP_PORT("Band selector");
            else if ((nMode != EQ_MONO) && (nMode != EQ_STEREO))
                SKIP_PORT("Band selector");

            // Balance
            if (channels > 1)
                BIND_PORT(pBalance);

            // Listen port
            if (nMode == EQ_MID_SIDE)
            {
                BIND_PORT(pListen);
                BIND_PORT(vChannels[0].pInGain);
                BIND_PORT(vChannels[1].pInGain);
            }

            for (size_t i=0; i<channels; ++i)
            {
                if ((nMode == EQ_STEREO) && (i > 0))
                    vChannels[i].pTrAmp     =   NULL;
                else
                    BIND_PORT(vChannels[i].pTrAmp);

                BIND_PORT(vChannels[i].pInMeter);
                BIND_PORT(vChannels[i].pOutMeter);
                if ((nMode == EQ_LEFT_RIGHT) || (nMode == EQ_MID_SIDE))
                    BIND_PORT(vChannels[i].pVisible); // Skip eq curve visibility
                else
                    vChannels[i].pVisible   = NULL;
            }

            // Bind filters
            lsp_trace("Binding filter ports");

            for (size_t i=0; i<nBands; ++i)
            {
                for (size_t j=0; j<channels; ++j)
                {
                    eq_band_t *b        = &vChannels[j].vBands[i];

                    if ((nMode == EQ_STEREO) && (j > 0))
                    {
                        // 1 port controls 2 filters
                        eq_band_t *sb       = &vChannels[0].vBands[i];

                        b->pGain            = sb->pGain;
                        b->pSolo            = sb->pSolo;
                        b->pMute            = sb->pMute;
                        b->pEnable          = sb->pEnable;
                        b->pVisibility      = sb->pVisibility;
                    }
                    else
                    {
                        // 1 port controls 1 band
                        BIND_PORT(b->pSolo);
                        BIND_PORT(b->pMute);
                        BIND_PORT(b->pEnable);
                        BIND_PORT(b->pVisibility);
                        BIND_PORT(b->pGain);
                    }
                }
            }
        }

        void graph_equalizer::destroy()
        {
            Module::destroy();
            do_destroy();
        }

        void graph_equalizer::do_destroy()
        {
            size_t channels     = (nMode == EQ_MONO) ? 1 : 2;

            if (vChannels != NULL)
            {
                // Destroy channels
                for (size_t i=0; i<channels; ++i)
                {
                    eq_channel_t *c = &vChannels[i];
                    c->sEqualizer.destroy();

                    if (c->vBands != NULL)
                    {
                        delete [] c->vBands;
                        c->vBands   = NULL;
                    }
                }

                delete[] vChannels;
                vChannels       = NULL;
            }

            if (vIndexes != NULL)
            {
                delete [] vIndexes;
                vIndexes    = NULL;
            }

            if (vFreqs != NULL)
            {
                delete [] vFreqs;
                vFreqs      = NULL;
            }

            if (pIDisplay != NULL)
            {
                pIDisplay->destroy();
                pIDisplay   = NULL;
            }

            // Destroy analyzer
            sAnalyzer.destroy();
        }

        inline dspu::equalizer_mode_t graph_equalizer::get_eq_mode()
        {
            switch (size_t(pEqMode->value()))
            {
                case meta::graph_equalizer_metadata::PEM_IIR: return dspu::EQM_IIR;
                case meta::graph_equalizer_metadata::PEM_FIR: return dspu::EQM_FIR;
                case meta::graph_equalizer_metadata::PEM_FFT: return dspu::EQM_FFT;
                case meta::graph_equalizer_metadata::PEM_SPM: return dspu::EQM_SPM;
                default:
                    break;
            }
            return dspu::EQM_BYPASS;
        }

        void graph_equalizer::update_settings()
        {
            // Check sample rate
            if (fSampleRate <= 0)
                return;

            // Update common settings
            if (pInGain != NULL)
                fInGain     = pInGain->value();
            if (pZoom != NULL)
            {
                float zoom  = pZoom->value();
                if (zoom != fZoom)
                {
                    fZoom       = zoom;
                    pWrapper->query_display_draw();
                }
            }

            // Calculate balance
            float bal[2]    = { 1.0f, 1.0f };
            if (pBalance != NULL)
            {
                float xbal      = pBalance->value();
                bal[0]          = (100.0f - xbal) * 0.01f;
                bal[1]          = (xbal + 100.0f) * 0.01f;
            }
            if (pOutGain != NULL)
            {
                float out_gain  = pOutGain->value();
                bal[0]         *= out_gain;
                bal[1]         *= out_gain;
            }

            // Listen
            if (pListen != NULL)
                bListen     = pListen->value() >= 0.5f;

            size_t channels     = (nMode == EQ_MONO) ? 1 : 2;

            // Configure analyzer
            size_t n_an_channels = 0;
            for (size_t i=0; i<channels; ++i)
            {
                eq_channel_t *c     = &vChannels[i];
                const bool in_fft   = c->pFftInSwitch->value() >= 0.5f;
                const bool out_fft  = c->pFftOutSwitch->value() >= 0.5f;
                const bool ext_fft  = c->pFftExtSwitch->value() >= 0.5f;

                // channel:        0     1     2      3      4     5
                // designation: in_l out_l ext_l   in_r  out_r ext_r
                sAnalyzer.enable_channel(i*3, in_fft);
                sAnalyzer.enable_channel(i*3 + 1, out_fft);
                sAnalyzer.enable_channel(i*3 + 2, ext_fft);
                if ((in_fft) || (out_fft) || (ext_fft))
                    ++n_an_channels;
            }
            sAnalyzer.set_activity(n_an_channels > 0);
            sAnalyzer.set_reactivity(pReactivity->value());

            // Update shift gain
            if (pShiftGain != NULL)
                sAnalyzer.set_shift(pShiftGain->value() * 100.0f);

            // Listen flag
            if (pListen != NULL)
                bListen         = pListen->value() >= 0.5f;

            size_t slope                = pSlope->value();
            bool bypass                 = pBypass->value() >= 0.5f;
            bool solo                   = false;
            bool matched_tr             = bMatched;
            size_t step                 = (nBands > 16) ? 1 : 2;

            bMatched                    = (slope & 1) != 0;
            fInGain                     = pInGain->value();
            dspu::equalizer_mode_t eq_mode  = get_eq_mode();
            slope                       = meta::graph_equalizer_metadata::SLOPE_MIN + (slope >> 1);

            // Update channels
            for (size_t i=0; i<channels; ++i)
            {
                dspu::filter_params_t fp;
                eq_channel_t *c     = &vChannels[i];
                bool visible        = (c->pVisible == NULL) ? true : (c->pVisible->value() >= 0.5f);

                // Update settings
                c->sEqualizer.set_mode(eq_mode);
                if (c->sBypass.set_bypass(bypass))
                    pWrapper->query_display_draw();
                c->fOutGain         = bal[i];
                if (c->pInGain != NULL)
                    c->fInGain          = c->pInGain->value();

                // Update each band solo
                for (size_t j=0; j<nBands; ++j)
                {
                    eq_band_t *b        = &c->vBands[j];
                    b->bSolo            = b->pSolo->value() >= 0.5f;
                    if (b->bSolo)
                        solo                = true;
                }

                // Update each band
                for (size_t j=0; j<nBands; ++j)
                {
                    eq_band_t *b        = &c->vBands[j];
                    bool enable         = b->pEnable->value() >= 0.5f;
                    bool mute           = b->pMute->value() >= 0.5f;
                    float gain          = meta::graph_equalizer_metadata::BAND_GAIN_DFL;
                    bool b_vis          = visible;

                    // Calculate band gain
                    if (enable)
                    {
                        if (mute)
                        {
                            gain            = meta::graph_equalizer_metadata::BAND_GAIN_MIN;
                            b_vis           = false;
                        }
                        else if (solo)
                        {
                            if (b->bSolo)
                                gain            = b->pGain->value();
                            else
                            {
                                gain            = meta::graph_equalizer_metadata::BAND_GAIN_MIN;
                                b_vis           = false;
                            }
                        }
                        else
                            gain            = b->pGain->value();
                    }
                    else
                    {
                        gain            = (solo) ? meta::graph_equalizer_metadata::BAND_GAIN_MIN : meta::graph_equalizer_metadata::BAND_GAIN_DFL;
                        b_vis           = false;
                    }

                    // Update visibility
                    b->pVisibility->set_value((b_vis) ? 1.0f : 0.0f);

                    // Fetch filter params
                    c->sEqualizer.get_params(j, &fp);

                    bool update         =
                        (fp.fGain != gain) ||
                        (fp.nSlope != slope) ||
                        (bMatched != matched_tr);

                    if (update)
                    {
                        if (j == 0)
                        {
                            fp.nType        = (bMatched) ? dspu::FLT_MT_LRX_LOSHELF : dspu::FLT_BT_LRX_LOSHELF;
                            fp.fFreq        = sqrtf(meta::graph_equalizer_metadata::band_frequencies[0] * meta::graph_equalizer_metadata::band_frequencies[step]);
                            fp.fFreq2       = fp.fFreq;
                        }
                        else if (j == (nBands-1))
                        {
                            fp.nType        = (bMatched) ? dspu::FLT_MT_LRX_HISHELF : dspu::FLT_BT_LRX_HISHELF;
                            fp.fFreq        = sqrtf(meta::graph_equalizer_metadata::band_frequencies[(j-1)*step] * meta::graph_equalizer_metadata::band_frequencies[j*step]);
                            fp.fFreq2       = fp.fFreq;
                        }
                        else
                        {
                            fp.nType        = (bMatched) ? dspu::FLT_MT_LRX_LADDERPASS : dspu::FLT_BT_LRX_LADDERPASS;
                            fp.fFreq        = sqrtf(meta::graph_equalizer_metadata::band_frequencies[(j-1)*step] * meta::graph_equalizer_metadata::band_frequencies[j*step]);
                            fp.fFreq2       = sqrtf(meta::graph_equalizer_metadata::band_frequencies[j*step] * meta::graph_equalizer_metadata::band_frequencies[(j+1)*step]);
                        }

                        fp.fGain            = gain;
                        fp.nSlope           = slope;
                        fp.fQuality         = 0.0f;

                        c->sEqualizer.set_params(j, &fp);
                        b->nSync           |= CS_UPDATE;
                    }
                }
            }

            // Update analyzer
            if (sAnalyzer.needs_reconfiguration())
            {
                sAnalyzer.reconfigure();
                sAnalyzer.get_frequencies(vFreqs, vIndexes, SPEC_FREQ_MIN, SPEC_FREQ_MAX, meta::graph_equalizer_metadata::MESH_POINTS);
            }

            // Update latency
            size_t latency          = 0;
            for (size_t i=0; i<channels; ++i)
                latency                 = lsp_max(latency, vChannels[i].sEqualizer.get_latency());

            for (size_t i=0; i<channels; ++i)
            {
                vChannels[i].sDryDelay.set_delay(latency);
                sAnalyzer.set_channel_delay(i*3, latency);  // delay left and right inputs
            }
            set_latency(latency);
        }

        void graph_equalizer::update_sample_rate(long sr)
        {
            size_t channels     = (nMode == EQ_MONO) ? 1 : 2;
            size_t max_latency  = 1 << (meta::graph_equalizer_metadata::FFT_RANK + 1);

            // Initialize channels
            for (size_t i=0; i<channels; ++i)
            {
                eq_channel_t *c     = &vChannels[i];
                c->sBypass.init(sr);
                c->sEqualizer.set_sample_rate(sr);
            }

            // Initialize analyzer
            if (!sAnalyzer.init(
                channels*3, meta::graph_equalizer_metadata::FFT_RANK,
                sr, meta::graph_equalizer_metadata::REFRESH_RATE,
                max_latency))
                return;

            sAnalyzer.set_sample_rate(sr);
            sAnalyzer.set_rank(meta::graph_equalizer_metadata::FFT_RANK);
            sAnalyzer.set_activity(false);
            sAnalyzer.set_envelope(meta::graph_equalizer_metadata::FFT_ENVELOPE);
            sAnalyzer.set_window(meta::graph_equalizer_metadata::FFT_WINDOW);
            sAnalyzer.set_rate(meta::graph_equalizer_metadata::REFRESH_RATE);
        }

        void graph_equalizer::ui_activated()
        {
            size_t channels     = ((nMode == EQ_MONO) || (nMode == EQ_STEREO)) ? 1 : 2;
            for (size_t i=0; i<channels; ++i)
                vChannels[i].nSync     = CS_UPDATE;
        }

        void graph_equalizer::perform_analysis(size_t samples)
        {
            // Do not do anything if analyzer is inactive
            if (!sAnalyzer.activity())
                return;

            // Prepare processing
            size_t channels     = (nMode == EQ_MONO) ? 1 : 2;

            const float *bufs[6] = { NULL, NULL, NULL, NULL, NULL, NULL };
            for (size_t i=0; i<channels; ++i)
            {
                eq_channel_t *c         = &vChannels[i];
                bufs[i*3]               = c->vInPtr;
                bufs[i*3 + 1]           = c->vOutBuffer;
                bufs[i*3 + 2]           = c->vExtPtr;
            }

            // Perform FFT analysis
            sAnalyzer.process(bufs, samples);
        }

        void graph_equalizer::process(size_t samples)
        {
            size_t channels     = (nMode == EQ_MONO) ? 1 : 2;

            // Initialize buffer pointers
            for (size_t i=0; i<channels; ++i)
            {
                eq_channel_t *c     = &vChannels[i];
                c->vIn              = c->pIn->buffer<float>();
                c->vOut             = c->pOut->buffer<float>();

                c->vSend            = NULL;
                c->vReturn          = NULL;
                c->vInPtr           = NULL;
                c->vExtPtr          = NULL;

                if (c->pSend != NULL)
                {
                    core::AudioBuffer *buf = c->pSend->buffer<core::AudioBuffer>();
                    if ((buf != NULL) && (buf->active()))
                        c->vSend        = buf->buffer();
                }
                if (c->pReturn != NULL)
                {
                    core::AudioBuffer *buf = c->pReturn->buffer<core::AudioBuffer>();
                    if ((buf != NULL) && (buf->active()))
                        c->vReturn      = buf->buffer();
                }
            }

            // Process samples
            while (samples > 0)
            {
                // Determine buffer size for processing
                size_t to_process   = lsp_min(EQ_BUFFER_SIZE, samples);

                // Store unprocessed data
                for (size_t i=0; i<channels; ++i)
                {
                    eq_channel_t *c     = &vChannels[i];
                    c->sDryDelay.process(c->vDryBuf, c->vIn, to_process);
                }

                // Pre-process data
                if (nMode == EQ_MID_SIDE)
                {
                    eq_channel_t *l = &vChannels[0], *r = &vChannels[1];
                    if (!bListen)
                    {
                        l->pInMeter->set_value(dsp::abs_max(l->vIn, to_process) * fInGain);
                        r->pInMeter->set_value(dsp::abs_max(l->vIn, to_process) * fInGain);
                    }
                    dsp::lr_to_ms(l->vInBuffer, r->vInBuffer, l->vIn, r->vIn, to_process);
                    if (fInGain != 1.0f)
                    {
                        dsp::mul_k2(l->vInBuffer, fInGain, to_process);
                        dsp::mul_k2(r->vInBuffer, fInGain, to_process);
                    }
                    l->vInPtr = l->vInBuffer;
                    r->vInPtr = r->vInBuffer;

                    if ((l->vReturn != NULL) && (r->vReturn != NULL))
                    {
                        dsp::lr_to_ms(l->vExtBuffer, r->vExtBuffer, l->vReturn, r->vReturn, to_process);
                        l->vExtPtr  = l->vExtBuffer;
                        r->vExtPtr  = r->vExtBuffer;
                    }

                    if (bListen)
                    {
                        l->pInMeter->set_value(dsp::abs_max(l->vInBuffer, to_process));
                        r->pInMeter->set_value(dsp::abs_max(r->vInBuffer, to_process));
                    }
                }
                else if (nMode == EQ_MONO)
                {
                    eq_channel_t *c = &vChannels[0];
                    if (fInGain != 1.0f)
                    {
                        dsp::mul_k3(c->vInBuffer, c->vIn, fInGain, to_process);
                        c->vInPtr   = c->vInBuffer;
                    }
                    else
                        c->vInPtr   = c->vIn;

                    if (c->vReturn != NULL)
                        c->vExtPtr  = c->vReturn;

                    c->pInMeter->set_value(dsp::abs_max(c->vInPtr, to_process));
                }
                else
                {
                    eq_channel_t *l = &vChannels[0], *r = &vChannels[1];
                    if (fInGain != 1.0f)
                    {
                        dsp::mul_k3(l->vInBuffer, l->vIn, fInGain, to_process);
                        dsp::mul_k3(r->vInBuffer, r->vIn, fInGain, to_process);
                        l->vInPtr   = l->vInBuffer;
                        r->vInPtr   = r->vInBuffer;
                    }
                    else
                    {
                        l->vInPtr   = l->vIn;
                        r->vInPtr   = r->vIn;
                    }

                    if ((l->vReturn != NULL) && (r->vReturn != NULL))
                    {
                        l->vExtPtr  = l->vReturn;
                        r->vExtPtr  = r->vReturn;
                    }

                    l->pInMeter->set_value(dsp::abs_max(l->vInPtr, to_process));
                    r->pInMeter->set_value(dsp::abs_max(r->vInPtr, to_process));
                }

                // Process each channel individually
                for (size_t i=0; i<channels; ++i)
                {
                    eq_channel_t *c     = &vChannels[i];

                    // Process the signal by the equalizer
                    c->sEqualizer.process(c->vOutBuffer, c->vInPtr, to_process);
                    if (c->fInGain != 1.0f)
                        dsp::mul_k2(c->vOutBuffer, c->fInGain, to_process);
                }

                // Call analyzer
                perform_analysis(to_process);

                // Post-process data (if needed)
                if ((nMode == EQ_MID_SIDE) && (!bListen))
                    dsp::ms_to_lr(vChannels[0].vOutBuffer, vChannels[1].vOutBuffer, vChannels[0].vOutBuffer, vChannels[1].vOutBuffer, to_process);

                // Process data via bypass
                for (size_t i=0; i<channels; ++i)
                {
                    eq_channel_t *c     = &vChannels[i];

                    // Do metering
                    if (c->pOutMeter != NULL)
                        c->pOutMeter->set_value(dsp::abs_max(c->vOutBuffer, to_process) * c->fOutGain);

                    // Process via bypass
                    if (c->fOutGain != 1.0f)
                    {
                        if (c->vSend != NULL)
                            dsp::mul_k3(c->vSend, c->vOutBuffer, c->fOutGain, to_process);
                        c->sBypass.process_wet(c->vOut, c->vDryBuf, c->vOutBuffer, c->fOutGain, to_process);
                    }
                    else
                    {
                        if (c->vSend != NULL)
                            dsp::copy(c->vSend, c->vOutBuffer, to_process);
                        c->sBypass.process(c->vOut, c->vDryBuf, c->vOutBuffer, to_process);
                    }

                    c->vIn             += to_process;
                    c->vOut            += to_process;
                    if (c->vSend != NULL)
                        c->vSend           += to_process;
                    if (c->vReturn != NULL)
                        c->vReturn         += to_process;
                }

                // Update counter
                samples            -= to_process;
            }

            // Output FFT curves for each channel and report latency
            for (size_t i=0; i<channels; ++i)
            {
                eq_channel_t *c     = &vChannels[i];

                // Input FFT mesh
                plug::mesh_t *mesh          = c->pFftInMesh->buffer<plug::mesh_t>();
                if ((mesh != NULL) && (mesh->isEmpty()))
                {
                    // Add extra points
                    mesh->pvData[0][0] = SPEC_FREQ_MIN * 0.5f;
                    mesh->pvData[0][meta::graph_equalizer_metadata::MESH_POINTS+1] = SPEC_FREQ_MAX * 2.0f;
                    mesh->pvData[1][0] = 0.0f;
                    mesh->pvData[1][meta::graph_equalizer_metadata::MESH_POINTS+1] = 0.0f;

                    // Copy frequency points
                    dsp::copy(&mesh->pvData[0][1], vFreqs, meta::graph_equalizer_metadata::MESH_POINTS);
                    sAnalyzer.get_spectrum(i*3, &mesh->pvData[1][1], vIndexes, meta::graph_equalizer_metadata::MESH_POINTS);

                    // Mark mesh containing data
                    mesh->data(2, meta::graph_equalizer_metadata::MESH_POINTS+2);
                }

                // Output FFT mesh
                mesh                        = c->pFftOutMesh->buffer<plug::mesh_t>();
                if ((mesh != NULL) && (mesh->isEmpty()))
                {
                    // Copy frequency points
                    dsp::copy(mesh->pvData[0], vFreqs, meta::graph_equalizer_metadata::MESH_POINTS);
                    sAnalyzer.get_spectrum(i*3 + 1, mesh->pvData[1], vIndexes, meta::graph_equalizer_metadata::MESH_POINTS);

                    // Mark mesh containing data
                    mesh->data(2, meta::graph_equalizer_metadata::MESH_POINTS);
                }

                // External (return) FFT mesh
                mesh                        = c->pFftExtMesh->buffer<plug::mesh_t>();
                if ((mesh != NULL) && (mesh->isEmpty()))
                {
                    // Copy frequency points
                    dsp::copy(mesh->pvData[0], vFreqs, meta::graph_equalizer_metadata::MESH_POINTS);
                    sAnalyzer.get_spectrum(i*3 + 2, mesh->pvData[1], vIndexes, meta::graph_equalizer_metadata::MESH_POINTS);

                    // Mark mesh containing data
                    mesh->data(2, meta::graph_equalizer_metadata::MESH_POINTS);
                }
            }

            // For Mono and Stereo channels only the first channel should be processed
            if (nMode == EQ_STEREO)
                channels        = 1;

            // Sync meshes
            for (size_t i=0; i<channels; ++i)
            {
                eq_channel_t *c     = &vChannels[i];

                // Synchronize bands
                for (size_t j=0; j<nBands; ++j)
                {
                    // Update transfer chart of the filter
                    eq_band_t *b  = &c->vBands[j];
                    if (b->nSync & CS_UPDATE)
                    {
                        c->sEqualizer.freq_chart(j, b->vTrRe, b->vTrIm, vFreqs, meta::graph_equalizer_metadata::MESH_POINTS);
                        b->nSync    = 0;
                        c->nSync    = CS_UPDATE;
                    }
                }

                // Synchronize main transfer function of the channel
                if (c->nSync & CS_UPDATE)
                {
                    // Initialize complex numbers for transfer function
                    dsp::fill_one(c->vTrRe, meta::graph_equalizer_metadata::MESH_POINTS);
                    dsp::fill_zero(c->vTrIm, meta::graph_equalizer_metadata::MESH_POINTS);

                    for (size_t j=0; j<nBands; ++j)
                    {
                        eq_band_t *b  = &c->vBands[j];
                        dsp::complex_mul2(c->vTrRe, c->vTrIm, b->vTrRe, b->vTrIm, meta::graph_equalizer_metadata::MESH_POINTS);
                    }
                    c->nSync    = CS_SYNC_AMP;
                }

                // Output amplification curve
                if ((c->pTrAmp != NULL) && (c->nSync & CS_SYNC_AMP))
                {
                    // Sync mesh
                    plug::mesh_t *mesh        = c->pTrAmp->buffer<plug::mesh_t>();
                    if ((mesh != NULL) && (mesh->isEmpty()))
                    {
                        // Add extra points
                        mesh->pvData[0][0] = SPEC_FREQ_MIN*0.5f;
                        mesh->pvData[0][meta::graph_equalizer_metadata::MESH_POINTS+1] = SPEC_FREQ_MAX*2.0;
                        mesh->pvData[1][0] = 1.0f;
                        mesh->pvData[1][meta::graph_equalizer_metadata::MESH_POINTS+1] = 1.0f;

                        // Copy data
                        dsp::copy(&mesh->pvData[0][1], vFreqs, meta::graph_equalizer_metadata::MESH_POINTS);
                        dsp::complex_mod(&mesh->pvData[1][1], c->vTrRe, c->vTrIm, meta::graph_equalizer_metadata::MESH_POINTS);
                        mesh->data(2, meta::graph_equalizer_metadata::FILTER_MESH_POINTS);

                        c->nSync           &= ~CS_SYNC_AMP;
                    }

                    // Request for redraw
                    if (pWrapper != NULL)
                        pWrapper->query_display_draw();
                }
            }
        }

        bool graph_equalizer::inline_display(plug::ICanvas *cv, size_t width, size_t height)
        {
            // Check proportions
            if (height > (M_RGOLD_RATIO * width))
                height  = M_RGOLD_RATIO * width;

            // Init canvas
            if (!cv->init(width, height))
                return false;
            width   = cv->width();
            height  = cv->height();

            // Clear background
            bool bypassing = vChannels[0].sBypass.bypassing();
            cv->set_color_rgb((bypassing) ? CV_DISABLED : CV_BACKGROUND);
            cv->paint();

            // Draw axis
            cv->set_line_width(1.0);

            float zx    = 1.0f/SPEC_FREQ_MIN;
            float zy    = fZoom/GAIN_AMP_M_48_DB;
            float dx    = width/(logf(SPEC_FREQ_MAX)-logf(SPEC_FREQ_MIN));
            float dy    = height/(logf(GAIN_AMP_M_48_DB/fZoom)-logf(GAIN_AMP_P_48_DB*fZoom));

            // Draw vertical lines
            cv->set_color_rgb(CV_YELLOW, 0.5f);
            for (float i=100.0f; i<SPEC_FREQ_MAX; i *= 10.0f)
            {
                float ax = dx*(logf(i*zx));
                cv->line(ax, 0, ax, height);
            }

            // Draw horizontal lines
            cv->set_color_rgb(CV_WHITE, 0.5f);
            for (float i=GAIN_AMP_M_48_DB; i<GAIN_AMP_P_48_DB; i *= GAIN_AMP_P_12_DB)
            {
                float ay = height + dy*(logf(i*zy));
                cv->line(0, ay, width, ay);
            }

            // Allocate buffer: f, x, y, re, im
            pIDisplay           = core::IDBuffer::reuse(pIDisplay, 5, width+2);
            core::IDBuffer *b   = pIDisplay;
            if (b == NULL)
                return false;

            // Initialize mesh
            b->v[0][0]          = SPEC_FREQ_MIN*0.5f;
            b->v[0][width+1]    = SPEC_FREQ_MAX*2.0f;
            b->v[3][0]          = 1.0f;
            b->v[3][width+1]    = 1.0f;
            b->v[4][0]          = 0.0f;
            b->v[4][width+1]    = 0.0f;

            size_t channels = ((nMode == EQ_MONO) || (nMode == EQ_STEREO)) ? 1 : 2;
            static uint32_t c_colors[] = {
                    CV_MIDDLE_CHANNEL, CV_MIDDLE_CHANNEL,
                    CV_MIDDLE_CHANNEL, CV_MIDDLE_CHANNEL,
                    CV_LEFT_CHANNEL, CV_RIGHT_CHANNEL,
                    CV_MIDDLE_CHANNEL, CV_SIDE_CHANNEL
                   };

            bool aa = cv->set_anti_aliasing(true);
            cv->set_line_width(2);

            for (size_t i=0; i<channels; ++i)
            {
                eq_channel_t *c = &vChannels[i];

                for (size_t j=0; j<width; ++j)
                {
                    size_t k        = (j*meta::graph_equalizer_metadata::MESH_POINTS)/width;
                    b->v[0][j+1]    = vFreqs[k];
                    b->v[3][j+1]    = c->vTrRe[k];
                    b->v[4][j+1]    = c->vTrIm[k];
                }

                dsp::complex_mod(b->v[3], b->v[3], b->v[4], width+2);
                dsp::fill(b->v[1], 0.0f, width+2);
                dsp::fill(b->v[2], height, width+2);
                dsp::axis_apply_log1(b->v[1], b->v[0], zx, dx, width+2);
                dsp::axis_apply_log1(b->v[2], b->v[3], zy, dy, width+2);

                // Draw mesh
                uint32_t color = (bypassing || !(active())) ? CV_SILVER : c_colors[nMode*2 + i];
                Color stroke(color), fill(color, 0.5f);
                cv->draw_poly(b->v[1], b->v[2], width+2, stroke, fill);
            }
            cv->set_anti_aliasing(aa);

            return true;
        }

        void graph_equalizer::dump_band(dspu::IStateDumper *v, const eq_band_t *b)
        {
            v->begin_object(b, sizeof(eq_band_t));
            {
                v->write("bSolo", b->bSolo);
                v->write("nSync", b->nSync);
                v->write("vTrRe", b->vTrRe);
                v->write("vTrIm", b->vTrIm);

                v->write("pGain", b->pGain);
                v->write("pSolo", b->pSolo);
                v->write("pMute", b->pMute);
                v->write("pEnable", b->pEnable);
                v->write("pVisibility", b->pVisibility);
            }
            v->end_object();
        }

        void graph_equalizer::dump_channel(dspu::IStateDumper *v, const eq_channel_t *c) const
        {
            v->begin_object(c, sizeof(eq_channel_t));
            {
                v->write_object("sEqualizer", &c->sEqualizer);
                v->write_object("sBypass", &c->sBypass);
                v->write_object("sDryDelay", &c->sDryDelay);

                v->write("nSync", c->nSync);
                v->write("fInGain", c->fInGain);
                v->write("fOutGain", c->fOutGain);
                v->begin_array("vBands", c->vBands, nBands);
                {
                    for (size_t i=0; i<nBands; ++i)
                        dump_band(v, &c->vBands[i]);
                }
                v->end_array();

                v->write("vIn", c->vIn);
                v->write("vOut", c->vOut);
                v->write("vSend", c->vSend);
                v->write("vReturn", c->vReturn);
                v->write("vInPtr", c->vInPtr);
                v->write("vExtPtr", c->vExtPtr);
                v->write("vDryBuf", c->vDryBuf);
                v->write("vInBuffer", c->vInBuffer);
                v->write("vOutBuffer", c->vOutBuffer);
                v->write("vExtBuffer", c->vExtBuffer);
                v->write("vTrRe", c->vTrRe);
                v->write("vTrIm", c->vTrIm);

                v->write("pIn", c->pIn);
                v->write("pOut", c->pOut);
                v->write("pSend", c->pSend);
                v->write("pReturn", c->pReturn);
                v->write("pInGain", c->pInGain);
                v->write("pTrAmp", c->pTrAmp);
                v->write("pFftInSwitch", c->pFftInSwitch);
                v->write("pFftOutSwitch", c->pFftOutSwitch);
                v->write("pFftExtSwitch", c->pFftExtSwitch);
                v->write("pFftInMesh", c->pFftInMesh);
                v->write("pFftOutMesh", c->pFftOutMesh);
                v->write("pFftExtMesh", c->pFftExtMesh);
                v->write("pVisible", c->pVisible);
                v->write("pInMeter", c->pInMeter);
                v->write("pOutMeter", c->pOutMeter);
            }
            v->end_object();
        }

        void graph_equalizer::dump(dspu::IStateDumper *v) const
        {
            plug::Module::dump(v);

            size_t channels     = (nMode == EQ_MONO) ? 1 : 2;

            v->write_object("sAnalyzer", &sAnalyzer);
            v->begin_array("vChannels", vChannels, channels);
            {
                for (size_t i=0; i<channels; ++i)
                    dump_channel(v, &vChannels[i]);
            }
            v->end_array();

            v->write("nBands", nBands);
            v->write("nMode", nMode);
            v->write("nSlope", nSlope);
            v->write("bListen", bListen);
            v->write("bMatched", bMatched);
            v->write("fInGain", fInGain);
            v->write("fZoom", fZoom);
            v->write("vFreqs", vFreqs);
            v->write("vIndexes", vIndexes);
            v->write_object("pIDisplay", pIDisplay);

            v->write("pEqMode", pEqMode);
            v->write("pSlope", pSlope);
            v->write("pListen", pListen);
            v->write("pInGain", pInGain);
            v->write("pOutGain", pOutGain);
            v->write("pBypass", pBypass);
            v->write("pReactivity", pReactivity);
            v->write("pShiftGain", pShiftGain);
            v->write("pZoom", pZoom);
            v->write("pBalance", pBalance);
        }

    } /* namespace plugins */
} /* namespace lsp */


