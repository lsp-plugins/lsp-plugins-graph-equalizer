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

                    f.wDot          = find_filter_widget<tk::GraphDot>(*fmt, "filter_dot", port_id);
                    f.wInfo         = find_filter_widget<tk::GraphText>(*fmt, "filter_info", port_id);

                    f.wGain         = find_filter_widget<tk::Knob>(*fmt, "filter_gain", port_id);
                    f.wGrid         = NULL; //find_filter_grid(&f);

                    f.fFreq         = 0; // TODO
                    f.pGain         = find_port(*fmt, "g", port_id);

//                    if (f.wDot != NULL)
//                        f.wDot->slots()->bind(tk::SLOT_MOUSE_CLICK, slot_filter_dot_click, this);

//                    bind_filter_edit(f.wDot);
//                    bind_filter_edit(f.wGain);

                    vFilters.add(&f);
                }
            }

//            // Bind events
//            size_t index = 0;
//            for (const char **fmt = fmtStrings; *fmt != NULL; ++fmt)
//            {
//                for (size_t port_id=0; port_id<nFilters; ++port_id)
//                {
//                    filter_t *f = vFilters.uget(index++);
//                    if (f == NULL)
//                        return;
//
//                    if (f->wDot != NULL)
//                    {
//                        f->wDot->slots()->bind(tk::SLOT_MOUSE_IN, slot_filter_mouse_in, f);
//                        f->wDot->slots()->bind(tk::SLOT_MOUSE_OUT, slot_filter_mouse_out, f);
//                    }
//
//                    // Get all filter-related widgets
//                    LSPString grp_name;
//                    grp_name.fmt_utf8(*fmt, "grp_filter", int(port_id));
//                    lltl::parray<tk::Widget> all_widgets;
//                    pWrapper->controller()->widgets()->query_group(&grp_name, &all_widgets);
//                    for (size_t i=0, n=all_widgets.size(); i<n; ++i)
//                    {
//                        tk::Widget *w = all_widgets.uget(i);
//                        if (w != NULL)
//                        {
//                            w->slots()->bind(tk::SLOT_MOUSE_IN, slot_filter_mouse_in, f);
//                            w->slots()->bind(tk::SLOT_MOUSE_OUT, slot_filter_mouse_out, f);
//                        }
//                    }
//                }
//            }
        }

        void graph_equalizer_ui::notify(ui::IPort *port)
        {

        }

    } // namespace plugui
} // namespace lsp


