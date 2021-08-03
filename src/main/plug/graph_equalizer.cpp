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

#include <private/plugins/graph_equalizer.h>
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/stdlib/math.h>

#include <lsp-plug.in/shared/id_colors.h>

#define EQ_BUFFER_SIZE          0x1000
#define TRACE_PORT(p) lsp_trace("  port id=%s", (p)->metadata()->id);

namespace lsp
{
    namespace plugins
    {
        //-------------------------------------------------------------------------
        typedef struct graph_equalizer_settings_t
        {
            const meta::plugin_t   *metadata;
            uint8_t                 bands;
            uint8_t                 mode;
        } graph_equalizer_settings_t;

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

        static const graph_equalizer_settings_t trigger_settings[] =
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

        static plug::Module *graph_equalizer_factory(const meta::plugin_t *meta)
        {
            for (const graph_equalizer_settings_t *s = trigger_settings; s->metadata != NULL; ++s)
                if (s->metadata == meta)
                    return new graph_equalizer(s->metadata, s->bands, s->mode);
            return NULL;
        }

        static plug::Factory factory(graph_equalizer_factory, plugins, 8);

        //-------------------------------------------------------------------------
        const float graph_equalizer::band_frequencies[] =
        {
            16.0f, 20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f, 125.0f,
            160.0f, 200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f, 800.0f, 1000.0f, 1250.0f,
            1600.0f, 2000.0f, 2500.0f, 3150.0f, 4000.0f, 5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f,
            16000.0f, 20000.0f
        };

        graph_equalizer::graph_equalizer(const meta::plugin_t *metadata, size_t bands, size_t mode):
            plug::Module(metadata)
        {
            vChannels       = NULL;
            nBands          = bands;
            nMode           = mode;
            nFftPosition    = FFTP_NONE;
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
            pFftMode        = NULL;
            pReactivity     = NULL;
            pShiftGain      = NULL;
            pZoom           = NULL;
            pBalance        = NULL;
        }

        graph_equalizer::~graph_equalizer()
        {
            destroy();
        }

        void graph_equalizer::init(plug::IWrapper *wrapper)
        {
            // Pass wrapper
            plug::Module::init(wrapper);

            // Determine number of channels
            size_t channels     = (nMode == EQ_MONO) ? 1 : 2;
            size_t max_latency  = 0;

            // Initialize analyzer
            if (!sAnalyzer.init(channels, meta::graph_equalizer_metadata::FFT_RANK,
                                MAX_SAMPLE_RATE, meta::graph_equalizer_metadata::REFRESH_RATE))
                return;

            sAnalyzer.set_rank(meta::graph_equalizer_metadata::FFT_RANK);
            sAnalyzer.set_activity(false);
            sAnalyzer.set_envelope(meta::graph_equalizer_metadata::FFT_ENVELOPE);
            sAnalyzer.set_window(meta::graph_equalizer_metadata::FFT_WINDOW);
            sAnalyzer.set_rate(meta::graph_equalizer_metadata::REFRESH_RATE);

            // Allocate channels
            vChannels           = new eq_channel_t[channels];
            if (vChannels == NULL)
                return;

            // Initialize global parameters
            fInGain             = 1.0f;
            bListen             = false;
            nFftPosition        = FFTP_NONE;

            // Allocate indexes
            vIndexes            = new uint32_t[meta::graph_equalizer_metadata::MESH_POINTS];
            if (vIndexes == NULL)
                return;

            // Allocate buffer
            size_t allocate     = (EQ_BUFFER_SIZE*2 + (nBands + 1)*meta::graph_equalizer_metadata::MESH_POINTS*2) * channels + meta::graph_equalizer_metadata::MESH_POINTS;
            float *abuf         = new float[allocate];
            if (abuf == NULL)
                return;

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
                c->vDryBuf          = abuf;
                abuf               += EQ_BUFFER_SIZE;
                c->vBuffer          = abuf;
                abuf               += EQ_BUFFER_SIZE;
                c->vTrRe            = abuf;
                abuf               += meta::graph_equalizer_metadata::MESH_POINTS;
                c->vTrIm            = abuf;
                abuf               += meta::graph_equalizer_metadata::MESH_POINTS;

                c->pIn              = NULL;
                c->pOut             = NULL;
                c->pInGain          = NULL;
                c->pTrAmp           = NULL;
                c->pFft             = NULL;
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
                    b->vTrRe        = abuf;
                    abuf           += meta::graph_equalizer_metadata::MESH_POINTS;
                    b->vTrIm        = abuf;
                    abuf           += meta::graph_equalizer_metadata::MESH_POINTS;

                    b->pGain        = NULL;
                    b->pSolo        = NULL;
                    b->pMute        = NULL;
                    b->pEnable      = NULL;
                    b->pVisibility  = NULL;
                }
            }

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
            {
                TRACE_PORT(vPorts[port_id]);
                vChannels[i].pIn        =   vPorts[port_id++];
            }
            for (size_t i=0; i<channels; ++i)
            {
                TRACE_PORT(vPorts[port_id]);
                vChannels[i].pOut       =   vPorts[port_id++];
            }

            // Bind common ports
            lsp_trace("Binding common ports");
            TRACE_PORT(vPorts[port_id]);
            pBypass                 = vPorts[port_id++];
            TRACE_PORT(vPorts[port_id]);
            pInGain                 = vPorts[port_id++];
            TRACE_PORT(vPorts[port_id]);
            pOutGain                = vPorts[port_id++];
            TRACE_PORT(vPorts[port_id]);
            pEqMode                 = vPorts[port_id++];
            TRACE_PORT(vPorts[port_id]);
            pSlope                  = vPorts[port_id++];
            TRACE_PORT(vPorts[port_id]);
            pFftMode                = vPorts[port_id++];
            TRACE_PORT(vPorts[port_id]);
            pReactivity             = vPorts[port_id++];
            TRACE_PORT(vPorts[port_id]);
            pShiftGain              = vPorts[port_id++];
            TRACE_PORT(vPorts[port_id]);
            pZoom                   = vPorts[port_id++];
            // Skip band select port
            if (nBands > 16)
            {
                TRACE_PORT(vPorts[port_id]);
                port_id++;
            }
            else if ((nMode != EQ_MONO) && (nMode != EQ_STEREO))
            {
                TRACE_PORT(vPorts[port_id]);
                port_id++;
            }

            // Balance
            if (channels > 1)
            {
                TRACE_PORT(vPorts[port_id]);
                pBalance                = vPorts[port_id++];
            }

            // Listen port
            if (nMode == EQ_MID_SIDE)
            {
                TRACE_PORT(vPorts[port_id]);
                pListen                 = vPorts[port_id++];
                TRACE_PORT(vPorts[port_id]);
                vChannels[0].pInGain    = vPorts[port_id++];
                TRACE_PORT(vPorts[port_id]);
                vChannels[1].pInGain    = vPorts[port_id++];
            }

            for (size_t i=0; i<channels; ++i)
            {
                if ((nMode == EQ_STEREO) && (i > 0))
                {
                    vChannels[i].pTrAmp     =   NULL;
                }
                else
                {
                    TRACE_PORT(vPorts[port_id]);
                    vChannels[i].pTrAmp     =   vPorts[port_id++];
                }
                TRACE_PORT(vPorts[port_id]);
                vChannels[i].pInMeter   =   vPorts[port_id++];
                TRACE_PORT(vPorts[port_id]);
                vChannels[i].pOutMeter  =   vPorts[port_id++];
                TRACE_PORT(vPorts[port_id]);
                vChannels[i].pFft       =   vPorts[port_id++];
                if (channels > 1)
                {
                    TRACE_PORT(vPorts[port_id]);
                    vChannels[i].pVisible       =   vPorts[port_id++];
                    if ((nMode == EQ_MONO) || (nMode == EQ_STEREO))
                        vChannels[i].pVisible       = NULL;
                }
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
                        TRACE_PORT(vPorts[port_id]);
                        b->pSolo            = vPorts[port_id++];
                        TRACE_PORT(vPorts[port_id]);
                        b->pMute            = vPorts[port_id++];
                        TRACE_PORT(vPorts[port_id]);
                        b->pEnable          = vPorts[port_id++];
                        TRACE_PORT(vPorts[port_id]);
                        b->pVisibility      = vPorts[port_id++];
                        TRACE_PORT(vPorts[port_id]);
                        b->pGain            = vPorts[port_id++];
                    }
                }
            }
        }

        void graph_equalizer::destroy()
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

            if (pFftMode != NULL)
            {
                fft_position_t pos = fft_position_t(pFftMode->value());
                if (pos != nFftPosition)
                {
                    nFftPosition    = pos;
                    sAnalyzer.reset();
                }
                sAnalyzer.set_activity(nFftPosition != FFTP_NONE);
            }

            // Update reactivity
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
                            fp.fFreq        = sqrtf(band_frequencies[0] * band_frequencies[step]);
                            fp.fFreq2       = fp.fFreq;
                        }
                        else if (j == (nBands-1))
                        {
                            fp.nType        = (bMatched) ? dspu::FLT_MT_LRX_HISHELF : dspu::FLT_BT_LRX_HISHELF;
                            fp.fFreq        = sqrtf(band_frequencies[(j-1)*step] * band_frequencies[j*step]);
                            fp.fFreq2       = fp.fFreq;
                        }
                        else
                        {
                            fp.nType        = (bMatched) ? dspu::FLT_MT_LRX_LADDERPASS : dspu::FLT_BT_LRX_LADDERPASS;
                            fp.fFreq        = sqrtf(band_frequencies[(j-1)*step] * band_frequencies[j*step]);
                            fp.fFreq2       = sqrtf(band_frequencies[j*step] * band_frequencies[(j+1)*step]);
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
                vChannels[i].sDryDelay.set_delay(latency);
            set_latency(latency);
        }

        void graph_equalizer::update_sample_rate(long sr)
        {
            size_t channels     = (nMode == EQ_MONO) ? 1 : 2;

            sAnalyzer.set_sample_rate(sr);

            // Initialize channels
            for (size_t i=0; i<channels; ++i)
            {
                eq_channel_t *c     = &vChannels[i];
                c->sBypass.init(sr);
                c->sEqualizer.set_sample_rate(sr);
            }
        }

        void graph_equalizer::ui_activated()
        {
            size_t channels     = ((nMode == EQ_MONO) || (nMode == EQ_STEREO)) ? 1 : 2;
            for (size_t i=0; i<channels; ++i)
                vChannels[i].nSync     = CS_UPDATE;
        }

        void graph_equalizer::process(size_t samples)
        {
            size_t channels     = (nMode == EQ_MONO) ? 1 : 2;
            float *analyze[2];

            // Initialize buffer pointers
            for (size_t i=0; i<channels; ++i)
            {
                eq_channel_t *c     = &vChannels[i];
                c->vIn              = c->pIn->buffer<float>();
                c->vOut             = c->pOut->buffer<float>();
                analyze[i]          = c->vBuffer;
            }

            size_t fft_pos          = (ui_active()) ? nFftPosition : FFTP_NONE;

            // Process samples
            while (samples > 0)
            {
                // Determine buffer size for processing
                size_t to_process   = (samples > EQ_BUFFER_SIZE) ? EQ_BUFFER_SIZE : samples;

                // Store unprocessed data
                for (size_t i=0; i<channels; ++i)
                {
                    eq_channel_t *c     = &vChannels[i];
                    c->sDryDelay.process(c->vDryBuf, c->vIn, to_process);
                }

                // Pre-process data
                if (nMode == EQ_MID_SIDE)
                {
                    if (!bListen)
                    {
                        vChannels[0].pInMeter->set_value(dsp::abs_max(vChannels[0].vIn, to_process));
                        vChannels[1].pInMeter->set_value(dsp::abs_max(vChannels[1].vIn, to_process));
                    }
                    dsp::lr_to_ms(vChannels[0].vBuffer, vChannels[1].vBuffer, vChannels[0].vIn, vChannels[1].vIn, to_process);
                    if (bListen)
                    {
                        vChannels[0].pInMeter->set_value(dsp::abs_max(vChannels[0].vBuffer, to_process));
                        vChannels[1].pInMeter->set_value(dsp::abs_max(vChannels[1].vBuffer, to_process));
                    }
                    if (fInGain != 1.0f)
                    {
                        dsp::mul_k2(vChannels[0].vBuffer, fInGain, to_process);
                        dsp::mul_k2(vChannels[1].vBuffer, fInGain, to_process);
                    }
                }
                else if (nMode == EQ_MONO)
                {
                    vChannels[0].pInMeter->set_value(dsp::abs_max(vChannels[0].vIn, to_process));
                    if (fInGain != 1.0f)
                        dsp::mul_k3(vChannels[0].vBuffer, vChannels[0].vIn, fInGain, to_process);
                    else
                        dsp::copy(vChannels[0].vBuffer, vChannels[0].vIn, to_process);
                }
                else
                {
                    vChannels[0].pInMeter->set_value(dsp::abs_max(vChannels[0].vIn, to_process));
                    vChannels[1].pInMeter->set_value(dsp::abs_max(vChannels[1].vIn, to_process));
                    if (fInGain != 1.0f)
                    {
                        dsp::mul_k3(vChannels[0].vBuffer, vChannels[0].vIn, fInGain, to_process);
                        dsp::mul_k3(vChannels[1].vBuffer, vChannels[1].vIn, fInGain, to_process);
                    }
                    else
                    {
                        dsp::copy(vChannels[0].vBuffer, vChannels[0].vIn, to_process);
                        dsp::copy(vChannels[1].vBuffer, vChannels[1].vIn, to_process);
                    }
                }

                // Do FFT in 'PRE'-position
                if (fft_pos == FFTP_PRE)
                    sAnalyzer.process(analyze, to_process);

                // Process each channel individually
                for (size_t i=0; i<channels; ++i)
                {
                    eq_channel_t *c     = &vChannels[i];

                    // Process the signal by the equalizer
                    c->sEqualizer.process(c->vBuffer, c->vBuffer, to_process);
                    if (c->fInGain != 1.0f)
                        dsp::mul_k2(c->vBuffer, c->fInGain, to_process);
                }

                // Do FFT in 'POST'-position
                if (fft_pos == FFTP_POST)
                    sAnalyzer.process(analyze, to_process);

                // Post-process data (if needed)
                if ((nMode == EQ_MID_SIDE) && (!bListen))
                    dsp::ms_to_lr(vChannels[0].vBuffer, vChannels[1].vBuffer, vChannels[0].vBuffer, vChannels[1].vBuffer, to_process);

                // Process data via bypass
                for (size_t i=0; i<channels; ++i)
                {
                    eq_channel_t *c     = &vChannels[i];

                    // Apply output gain
                    if (c->fOutGain != 1.0f)
                        dsp::mul_k2(c->vBuffer, c->fOutGain, to_process);

                    // Do metering
                    if (c->pOutMeter != NULL)
                        c->pOutMeter->set_value(dsp::abs_max(c->vBuffer, to_process));

                    // Process via bypass
                    c->sBypass.process(c->vOut, c->vDryBuf, c->vBuffer, to_process);

                    c->vIn             += to_process;
                    c->vOut            += to_process;
                }

                // Update counter
                samples            -= to_process;
            }

            // Output FFT curves for each channel and report latency
            for (size_t i=0; i<channels; ++i)
            {
                eq_channel_t *c     = &vChannels[i];

                // Output FFT curve
                plug::mesh_t *mesh            = c->pFft->buffer<plug::mesh_t>();
                if ((mesh != NULL) && (mesh->isEmpty()))
                {
                    if (nFftPosition != FFTP_NONE)
                    {
                        // Copy frequency points
                        dsp::copy(mesh->pvData[0], vFreqs, meta::graph_equalizer_metadata::MESH_POINTS);
                        sAnalyzer.get_spectrum(i, mesh->pvData[1], vIndexes, meta::graph_equalizer_metadata::MESH_POINTS);

                        // Mark mesh containing data
                        mesh->data(2, meta::graph_equalizer_metadata::MESH_POINTS);
                    }
                    else
                        mesh->data(2, 0);
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
                v->write("vDryBuf", c->vDryBuf);
                v->write("vBuffer", c->vBuffer);
                v->write("vTrRe", c->vTrRe);
                v->write("vTrIm", c->vTrIm);

                v->write("pIn", c->pIn);
                v->write("pOut", c->pOut);
                v->write("pInGain", c->pInGain);
                v->write("pTrAmp", c->pTrAmp);
                v->write("pFft", c->pFft);
                v->write("pVisible", c->pVisible);
                v->write("pInMeter", c->pInMeter);
                v->write("pOutMeter", c->pOutMeter);
            }
            v->end_object();
        }

        void graph_equalizer::dump(dspu::IStateDumper *v) const
        {
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
            v->write("nFftPosition", nFftPosition);
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
            v->write("pFftMode", pFftMode);
            v->write("pReactivity", pReactivity);
            v->write("pShiftGain", pShiftGain);
            v->write("pZoom", pZoom);
            v->write("pBalance", pBalance);
        }

    } // namespace plugins
} // namespace lsp


