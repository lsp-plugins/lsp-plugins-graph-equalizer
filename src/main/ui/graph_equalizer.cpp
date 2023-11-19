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
#include <lsp-plug.in/plug-fw/ui.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/stdlib/locale.h>
#include <lsp-plug.in/stdlib/stdio.h>
#include <lsp-plug.in/stdlib/string.h>

#include <private/meta/graph_equalizer.h>
#include <private/ui/graph_equalizer.h>

namespace lsp
{
    namespace plugui
    {
        //---------------------------------------------------------------------
        // Plugin UI factory
        static const meta::plugin_t *plugin_uis[] =
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

        static ui::Module *ui_factory(const meta::plugin_t *meta)
        {
            return new graph_equalizer_ui(meta);
        }

        static ui::Factory factory(ui_factory, plugin_uis, 8);

        //---------------------------------------------------------------------
        static const char *fmt_strings[] =
        {
            "%s_%d",
            NULL
        };

        static const char *fmt_strings_lr[] =
        {
            "%sl_%d",
            "%sr_%d",
            NULL
        };

        static const char *fmt_strings_ms[] =
        {
            "%sm_%d",
            "%ss_%d",
            NULL
        };

        //---------------------------------------------------------------------
        graph_equalizer_ui::graph_equalizer_ui(const meta::plugin_t *meta): ui::Module(meta)
        {
            fmtStrings      = fmt_strings;
            nFilters        = 16;
            pCurrFilter     = NULL;

            if ((!strcmp(meta->uid, meta::graph_equalizer_x16_lr.uid)) ||
                (!strcmp(meta->uid, meta::graph_equalizer_x32_lr.uid)))
            {
                fmtStrings      = fmt_strings_lr;
            }
            else if ((!strcmp(meta->uid, meta::graph_equalizer_x16_ms.uid)) ||
                 (!strcmp(meta->uid, meta::graph_equalizer_x32_ms.uid)))
            {
                fmtStrings      = fmt_strings_ms;
            }


            if ((!strcmp(meta->uid, meta::graph_equalizer_x32_lr.uid)) ||
                (!strcmp(meta->uid, meta::graph_equalizer_x32_mono.uid)) ||
                (!strcmp(meta->uid, meta::graph_equalizer_x32_ms.uid)) ||
                (!strcmp(meta->uid, meta::graph_equalizer_x32_stereo.uid)))
                nFilters       = 32;
        }

        graph_equalizer_ui::~graph_equalizer_ui()
        {

        }

        status_t graph_equalizer_ui::post_init()
        {
            status_t res = ui::Module::post_init();
            if (res != STATUS_OK)
                return res;

            // Find main filter grids
            pWrapper->controller()->widgets()->query_group("filters", &vFilterGrids);
            for (size_t i=0, n=vFilterGrids.size(); i<n; ++i)
            {
                tk::Widget *g = vFilterGrids.uget(i);
                g->slots()->bind(tk::SLOT_REALIZED, slot_main_grid_realized, this);
                g->slots()->bind(tk::SLOT_MOUSE_IN, slot_main_grid_mouse_in, this);
                g->slots()->bind(tk::SLOT_MOUSE_OUT, slot_main_grid_mouse_out, this);
                g->slots()->bind(tk::SLOT_MOUSE_MOVE, slot_main_grid_mouse_move, this);
            }

            add_filters();

            return STATUS_OK;
        }

        status_t graph_equalizer_ui::pre_destroy()
        {
            return ui::Module::pre_destroy();
        }

        template <class T>
        T *graph_equalizer_ui::find_filter_widget(const char *fmt, const char *base, size_t id)
        {
            char widget_id[64];
            ::snprintf(widget_id, sizeof(widget_id)/sizeof(char), fmt, base, int(id));
            return pWrapper->controller()->widgets()->get<T>(widget_id);
        }

        ui::IPort *graph_equalizer_ui::find_port(const char *fmt, const char *base, size_t id)
        {
            char port_id[32];
            ::snprintf(port_id, sizeof(port_id)/sizeof(char), fmt, base, int(id));
            return pWrapper->port(port_id);
        }

        void graph_equalizer_ui::add_filters()
        {
            size_t step = 32/nFilters;
            for (const char **fmt = fmtStrings; *fmt != NULL; ++fmt)
            {
                for (size_t port_id=0; port_id<nFilters; ++port_id)
                {
                    filter_t f;

                    f.pUI           = this;

                    f.sRect.nLeft   = 0;
                    f.sRect.nTop    = 0;
                    f.sRect.nWidth  = 0;
                    f.sRect.nHeight = 0;

                    f.bMouseIn      = false;

                    f.wMarker       = find_filter_widget<tk::GraphMarker>(*fmt, "filter_marker", port_id);
                    f.wDot          = find_filter_widget<tk::GraphDot>(*fmt, "filter_dot", port_id);
                    f.wInfo         = find_filter_widget<tk::GraphText>(*fmt, "filter_info", port_id);

                    f.wGain         = find_filter_widget<tk::Knob>(*fmt, "filter_gain", port_id);
                    f.wGrid         = find_filter_grid(&f);

                    f.fFreq         = meta::graph_equalizer_metadata::band_frequencies[port_id*step];
                    f.pGain         = find_port(*fmt, "g", port_id);
                    if (f.pGain != NULL)
                        f.pGain->bind(this);
                    f.pOn           = find_port(*fmt, "xe", port_id);
                    f.pMute         = find_port(*fmt, "xm", port_id);
                    f.pVisible      = find_port(*fmt, "fv", port_id);
                    if (f.pVisible != NULL)
                        f.pVisible->bind(this);

                    vFilters.add(&f);
                }
            }

            // Bind events
            size_t index = 0;
            for (const char **fmt = fmtStrings; *fmt != NULL; ++fmt)
            {
                for (size_t port_id=0; port_id<nFilters; ++port_id)
                {
                    filter_t *f = vFilters.uget(index++);
                    if (f == NULL)
                        return;

                    if (f->wDot != NULL)
                    {
                        f->wDot->slots()->bind(tk::SLOT_MOUSE_IN, slot_filter_mouse_in, f);
                        f->wDot->slots()->bind(tk::SLOT_MOUSE_OUT, slot_filter_mouse_out, f);
                    }

                    // Get all filter-related widgets
                    LSPString grp_name;
                    grp_name.fmt_utf8(*fmt, "grp_filter", int(port_id));
                    lltl::parray<tk::Widget> all_widgets;
                    pWrapper->controller()->widgets()->query_group(&grp_name, &all_widgets);
                    for (size_t i=0, n=all_widgets.size(); i<n; ++i)
                    {
                        tk::Widget *w = all_widgets.uget(i);
                        if (w != NULL)
                        {
                            w->slots()->bind(tk::SLOT_MOUSE_IN, slot_filter_mouse_in, f);
                            w->slots()->bind(tk::SLOT_MOUSE_OUT, slot_filter_mouse_out, f);
                        }
                    }
                }
            }
        }

        void graph_equalizer_ui::update_filter_info_text()
        {
            // Determine which frequency/note to show: of inspected filter or of selected filter
            filter_t *f = pCurrFilter;

            if (f != NULL && f->pVisible != NULL && f->pVisible->value() < 0.5f)
                f = NULL;

            // Commit current filter pointer and update note text
            for (size_t i=0, n=vFilters.size(); i<n; ++i)
            {
                filter_t *xf = vFilters.uget(i);
                if (xf != NULL)
                {
                    xf->wInfo->visibility()->set(xf == f);
                    xf->wMarker->visibility()->set(xf == f);
                }
            }

            // Check that we have the widget to display
            if ((f == NULL) || (f->wInfo == NULL))
                return;

            // Get the frequency
            float freq = f->fFreq;
            if (freq < 0.0f)
            {
                f->wInfo->visibility()->set(false);
                f->wMarker->visibility()->set(false);
                return;
            }

            // Get the gain
            float gain = (f->pGain != NULL) ? f->pGain->value() : -1.0f;
            if (gain < 0.0f)
            {
                f->wInfo->visibility()->set(false);
                f->wMarker->visibility()->set(false);
                return;
            }

            // Check that filter is enabled
            bool on = (f->pOn != NULL) ? (f->pOn->value() >= 0.5f) : false;
            if (!on)
            {
                f->wInfo->visibility()->set(false);
                f->wMarker->visibility()->set(false);
                return;
            }

            // Update the info displayed in the text
            {
                // Fill the parameters
                expr::Parameters params;
                tk::prop::String lc_string;
                LSPString text;
                lc_string.bind(f->wInfo->style(), pDisplay->dictionary());
                SET_LOCALE_SCOPED(LC_NUMERIC, "C");

                // Frequency
                text.fmt_ascii("%.2f", freq);
                params.set_string("frequency", &text);

                // Gain
                text.fmt_ascii("%.2f", dspu::gain_to_db(gain));
                params.set_string("gain", &text);

                // Filter number and audio channel
                text.set_ascii(f->pGain->id());
                if (text.starts_with_ascii("gm_"))
                    lc_string.set("labels.chan.mid");
                else if (text.starts_with_ascii("gs_"))
                    lc_string.set("labels.chan.side");
                else if (text.starts_with_ascii("gl_"))
                    lc_string.set("labels.chan.left");
                else if (text.starts_with_ascii("gr_"))
                    lc_string.set("labels.chan.right");
                else
                    lc_string.set("labels.filter");
                lc_string.format(&text);
                params.set_string("filter", &text);
                lc_string.params()->clear();

                f->wInfo->text()->set("lists.graph_eq.filter_info", &params);
            }
        }

        graph_equalizer_ui::filter_t *graph_equalizer_ui::find_filter_by_rect(tk::Widget *grid, ssize_t x, ssize_t y)
        {
            for (size_t i=0, n=vFilters.size(); i<n; ++i)
            {
                filter_t *d = vFilters.uget(i);
                if (d->wGrid != grid)
                    continue;
                if (tk::Position::inside(&d->sRect, x, y))
                    return d;
            }
            return NULL;
        }

        void graph_equalizer_ui::on_main_grid_mouse_in(tk::Widget *w, ssize_t x, ssize_t y)
        {
            filter_t *f = find_filter_by_rect(w, x, y);
            if (f != NULL)
                on_filter_mouse_in(f);
            else
                on_filter_mouse_out();
        }

        void graph_equalizer_ui::on_main_grid_mouse_out(tk::Widget *w, ssize_t x, ssize_t y)
        {
            on_filter_mouse_out();
        }

        void graph_equalizer_ui::on_main_grid_mouse_move(tk::Widget *w, ssize_t x, ssize_t y)
        {
            filter_t *f = find_filter_by_rect(w, x, y);
            if (f != NULL)
                on_filter_mouse_in(f);
            else
                on_filter_mouse_out();
        }

        void graph_equalizer_ui::on_main_grid_realized(tk::Widget *w)
        {
            // Bind events
            size_t index = 0;
            for (const char **fmt = fmtStrings; *fmt != NULL; ++fmt)
            {
                for (size_t port_id=0; port_id<nFilters; ++port_id)
                {
                    filter_t *f = vFilters.uget(index++);
                    if ((f == NULL) || (f->wGrid != w))
                        continue;

                    // Get all filter-related widgets
                    LSPString grp_name;
                    grp_name.fmt_utf8(*fmt, "grp_filter", int(port_id));
                    lltl::parray<tk::Widget> all_widgets;
                    pWrapper->controller()->widgets()->query_group(&grp_name, &all_widgets);

                    // Estimate the surrounding rectangle size
                    ws::rectangle_t r;
                    ssize_t min_x = 0, max_x = 0;
                    ssize_t min_y = 0, max_y = 0;
                    size_t processed = 0;
                    for (size_t i=0, n=all_widgets.size(); i<n; ++i)
                    {
                        tk::Widget *w = all_widgets.uget(i);
                        if (w != NULL)
                        {
                            w->get_padded_rectangle(&r);
                            if (processed++ > 0)
                            {
                                min_x = lsp_min(min_x, r.nLeft);
                                min_y = lsp_min(min_y, r.nTop);
                                max_x = lsp_max(max_x, r.nLeft + r.nWidth);
                                max_y = lsp_max(max_y, r.nTop + r.nHeight);
                            }
                            else
                            {
                                min_x = r.nLeft;
                                min_y = r.nTop;
                                max_x = r.nLeft + r.nWidth;
                                max_y = r.nTop + r.nHeight;
                            }
                        }
                    }

                    // Update allocation rectangle
                    f->sRect.nLeft      = min_x;
                    f->sRect.nTop       = min_y;
                    f->sRect.nWidth     = max_x - min_x;
                    f->sRect.nHeight    = max_y - min_y;
                }
            }
        }

        void graph_equalizer_ui::notify(ui::IPort *port, size_t flags)
        {
            if (pCurrFilter != NULL)
            {
                if (pCurrFilter->pVisible == port || pCurrFilter->pGain == port)
                    update_filter_info_text();
            }
        }

        status_t graph_equalizer_ui::slot_filter_mouse_in(tk::Widget *sender, void *ptr, void *data)
        {
            // Fetch parameters
            filter_t *f = static_cast<filter_t *>(ptr);
            if ((f == NULL) || (f->pUI == NULL))
                return STATUS_BAD_STATE;

            f->pUI->on_filter_mouse_in(f);

            return STATUS_OK;
        }

        status_t graph_equalizer_ui::slot_filter_mouse_out(tk::Widget *sender, void *ptr, void *data)
        {
            // Fetch parameters
            filter_t *f = static_cast<filter_t *>(ptr);
            if ((f == NULL) || (f->pUI == NULL))
                return STATUS_BAD_STATE;

            f->pUI->on_filter_mouse_out();

            return STATUS_OK;
        }

        status_t graph_equalizer_ui::slot_main_grid_realized(tk::Widget *sender, void *ptr, void *data)
        {
            // Fetch parameters
            graph_equalizer_ui *_this = static_cast<graph_equalizer_ui *>(ptr);
            if (_this == NULL)
                return STATUS_BAD_STATE;

            _this->on_main_grid_realized(sender);

            return STATUS_OK;
        }

        status_t graph_equalizer_ui::slot_main_grid_mouse_in(tk::Widget *sender, void *ptr, void *data)
        {
            // Fetch parameters
            graph_equalizer_ui *_this = static_cast<graph_equalizer_ui *>(ptr);
            if (_this == NULL)
                return STATUS_BAD_STATE;
            ws::event_t *ev = static_cast<ws::event_t *>(data);
            if (ev == NULL)
                return STATUS_BAD_STATE;

            _this->on_main_grid_mouse_in(sender, ev->nLeft, ev->nTop);

            return STATUS_OK;
        }

        status_t graph_equalizer_ui::slot_main_grid_mouse_out(tk::Widget *sender, void *ptr, void *data)
        {
            // Fetch parameters
            graph_equalizer_ui *_this = static_cast<graph_equalizer_ui *>(ptr);
            if (_this == NULL)
                return STATUS_BAD_STATE;
            ws::event_t *ev = static_cast<ws::event_t *>(data);
            if (ev == NULL)
                return STATUS_BAD_STATE;

            _this->on_main_grid_mouse_out(sender, ev->nLeft, ev->nTop);

            return STATUS_OK;
        }

        status_t graph_equalizer_ui::slot_main_grid_mouse_move(tk::Widget *sender, void *ptr, void *data)
        {
            // Fetch parameters
            graph_equalizer_ui *_this = static_cast<graph_equalizer_ui *>(ptr);
            if (_this == NULL)
                return STATUS_BAD_STATE;
            ws::event_t *ev = static_cast<ws::event_t *>(data);
            if (ev == NULL)
                return STATUS_BAD_STATE;

            _this->on_main_grid_mouse_move(sender, ev->nLeft, ev->nTop);

            return STATUS_OK;
        }

        void graph_equalizer_ui::on_filter_mouse_in(filter_t *f)
        {
            pCurrFilter   = (f->pMute->value() >= 0.5) ? NULL : f;
            f->bMouseIn = true;
            update_filter_info_text();
        }

        void graph_equalizer_ui::on_filter_mouse_out()
        {
            pCurrFilter = NULL;
            for (size_t i=0, n=vFilters.size(); i<n; ++i)
            {
                filter_t *f = vFilters.uget(i);
                if (f != NULL)
                    f->bMouseIn = false;
            }
            update_filter_info_text();
        }

        tk::Widget *graph_equalizer_ui::find_filter_grid(filter_t *f)
        {
            tk::Widget *list[] =
            {
                f->wGain
            };

            for (size_t i=0, n=vFilterGrids.size(); i<n; ++i)
            {
                tk::Widget *g = vFilterGrids.uget(i);

                for (size_t j=0, m=sizeof(list)/sizeof(list[0]); j<m; ++j)
                {
                    tk::Widget *w = list[j];
                    if ((w != NULL) && (w->has_parent(g)))
                        return g;
                }
            }

            return NULL;
        }

    } // namespace plugui
} // namespace lsp


