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
        graph_equalizer_ui::graph_equalizer_ui(const meta::plugin_t *meta): ui::Module(meta)
        {

        }

        graph_equalizer_ui::~graph_equalizer_ui()
        {

        }

        status_t graph_equalizer_ui::post_init()
        {
            status_t res = ui::Module::post_init();
            if (res != STATUS_OK)
                return res;

            return STATUS_OK;
        }

        status_t graph_equalizer_ui::pre_destroy()
        {
            return ui::Module::pre_destroy();
        }

        void graph_equalizer_ui::notify(ui::IPort *port)
        {

        }

    } // namespace plugui
} // namespace lsp


