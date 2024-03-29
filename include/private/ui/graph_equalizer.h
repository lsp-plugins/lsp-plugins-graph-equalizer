/*
 * Copyright (C) 2021 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2021 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-graph-equalizer
 * Created on: 15 мая 2023 г.
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

#ifndef PRIVATE_UI_GRAPH_EQUALIZER_H_
#define PRIVATE_UI_GRAPH_EQUALIZER_H_

#include <lsp-plug.in/plug-fw/ui.h>
#include <lsp-plug.in/lltl/darray.h>


namespace lsp
{
    namespace plugui
    {
        /**
         * UI for Graphic Equalizer plugin series
         */
        class graph_equalizer_ui: public ui::Module, public ui::IPortListener
        {

            protected:
                typedef struct filter_t
                {
                    graph_equalizer_ui *pUI;
                    ws::rectangle_t sRect;      // The overall rectangle over the grid

                    bool bMouseIn;              // Mouse is over filter indicator

                    float fFreq;

                    ui::IPort *pGain;
                    ui::IPort *pOn;
                    ui::IPort *pMute;
                    ui::IPort *pVisible;

                    tk::Widget *wGrid;          // Grid associated with the filter
                    tk::GraphMarker *wMarker;   // Graph dot for editing
                    tk::GraphDot *wDot;         // Graph dot for editing
                    tk::GraphText *wInfo;       // Text with note and frequency

                    tk::Knob *wGain;            // Gain button
                } filter_t;

            protected:
                const char            **fmtStrings;
                size_t                  nFilters;
                lltl::darray<filter_t>  vFilters;
                lltl::parray<tk::Widget> vFilterGrids;   // List of filter grids
                filter_t               *pCurrFilter;

            protected:
                static status_t slot_filter_mouse_in(tk::Widget *sender, void *ptr, void *data);
                static status_t slot_filter_mouse_out(tk::Widget *sender, void *ptr, void *data);

                static status_t slot_main_grid_realized(tk::Widget *sender, void *ptr, void *data);
                static status_t slot_main_grid_mouse_in(tk::Widget *sender, void *ptr, void *data);
                static status_t slot_main_grid_mouse_out(tk::Widget *sender, void *ptr, void *data);
                static status_t slot_main_grid_mouse_move(tk::Widget *sender, void *ptr, void *data);

            protected:
                void add_filters();

                void on_filter_mouse_in(filter_t *f);
                void on_filter_mouse_out();

                filter_t *find_filter_by_rect(tk::Widget *grid, ssize_t x, ssize_t y);
                void on_main_grid_realized(tk::Widget *w);

                void on_main_grid_mouse_in(tk::Widget *w, ssize_t x, ssize_t y);
                void on_main_grid_mouse_out(tk::Widget *w, ssize_t x, ssize_t y);
                void on_main_grid_mouse_move(tk::Widget *w, ssize_t x, ssize_t y);

                tk::Widget *find_filter_grid(filter_t *f);

                void update_filter_info_text();

                template <class T>
                T *find_filter_widget(const char *fmt, const char *base, size_t id);

                ui::IPort *find_port(const char *fmt, const char *base, size_t id);

            public:
                explicit graph_equalizer_ui(const meta::plugin_t *meta);
                virtual ~graph_equalizer_ui() override;

                virtual status_t    post_init() override;
                virtual status_t    pre_destroy() override;

                virtual void        notify(ui::IPort *port, size_t flags) override;
        };
    } // namespace plugui
} // namespace lsp


#endif /* PRIVATE_UI_GRAPH_EQUALIZER_H_ */
