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

            public:
                explicit graph_equalizer_ui(const meta::plugin_t *meta);
                virtual ~graph_equalizer_ui() override;

                virtual status_t    post_init() override;
                virtual status_t    pre_destroy() override;

                virtual void        notify(ui::IPort *port) override;
        };
    } // namespace plugui
} // namespace lsp


#endif /* PRIVATE_UI_GRAPH_EQUALIZER_H_ */
