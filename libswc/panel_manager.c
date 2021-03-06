/* swc: libswc/panel_manager.c
 *
 * Copyright (c) 2013, 2014 Michael Forney
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "panel_manager.h"
#include "internal.h"
#include "panel.h"

#include <wayland-server.h>
#include "protocol/swc-server-protocol.h"

static struct
{
    struct wl_global * global;
    struct wl_resource * resource;
} panel_manager;

static void create_panel(struct wl_client * client,
                         struct wl_resource * resource, uint32_t id,
                         struct wl_resource * surface_resource)
{
    struct swc_surface * surface = wl_resource_get_user_data(surface_resource);
    struct swc_panel * panel;

    panel = swc_panel_new(client, id, surface);

    if (!panel)
        wl_client_post_no_memory(client);
}

static const struct swc_panel_manager_interface panel_manager_implementation = {
    .create_panel = &create_panel
};

static void bind_panel_manager(struct wl_client * client, void * data,
                               uint32_t version, uint32_t id)
{
    struct wl_resource * resource;

    resource = wl_resource_create(client, &swc_panel_manager_interface, 1, id);
    wl_resource_set_implementation(resource, &panel_manager_implementation,
                                   NULL, NULL);
}

bool swc_panel_manager_initialize()
{
    panel_manager.global = wl_global_create(swc.display,
                                            &swc_panel_manager_interface, 1,
                                            NULL, &bind_panel_manager);

    if (!panel_manager.global)
        return false;

    return true;
}

void swc_panel_manager_finalize()
{
    wl_global_destroy(panel_manager.global);
}

// vim: fdm=syntax fo=croql et sw=4 sts=4 ts=8

