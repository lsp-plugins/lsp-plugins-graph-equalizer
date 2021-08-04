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

namespace lsp
{
    namespace plugui
    {
        //---------------------------------------------------------------------
        // Plugin UI factory
        static const meta::plugin_t *uis[] =
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

        static ui::Factory factory(uis, 8);

    } // namespace plugui
} // namespace lsp


