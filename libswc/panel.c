/* swc: libswc/panel.c
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

#include "panel.h"
#include "compositor.h"
#include "internal.h"
#include "keyboard.h"
#include "output.h"
#include "screen.h"
#include "seat.h"
#include "surface.h"
#include "util.h"
#include "view.h"
#include "protocol/swc-server-protocol.h"

#include <assert.h>
#include <stdlib.h>

static void update_position(struct swc_panel * panel)
{
    int32_t x, y;
    struct swc_rectangle * screen = &panel->screen->base.geometry,
                         * view = &panel->surface->view->geometry;

    switch (panel->edge)
    {
        case SWC_PANEL_EDGE_TOP:
            x = screen->x + panel->offset;
            y = screen->y;
            break;
        case SWC_PANEL_EDGE_BOTTOM:
            x = screen->x + panel->offset;
            y = screen->y + screen->height - view->height;
            break;
        case SWC_PANEL_EDGE_LEFT:
            x = screen->x;
            y = screen->y + screen->height - view->height - panel->offset;
            break;
        case SWC_PANEL_EDGE_RIGHT:
            x = screen->x + screen->width - view->width;
            y = screen->y + panel->offset;
            break;
        default: return;
    }

    swc_view_move(panel->surface->view, x, y);
}

static void dock(struct wl_client * client, struct wl_resource * resource,
                 uint32_t edge, struct wl_resource * output_resource,
                 uint32_t focus)
{
    struct swc_panel * panel = wl_resource_get_user_data(resource);
    struct swc_output * output = output_resource
        ? wl_resource_get_user_data(output_resource) : NULL;
    struct screen * screen = output
        ? output->screen : CONTAINER_OF(swc.screens.next, struct screen, link);
    bool screen_changed = screen != panel->screen;
    uint32_t length;

    switch (edge)
    {
        case SWC_PANEL_EDGE_TOP:
        case SWC_PANEL_EDGE_BOTTOM:
            length = screen->base.geometry.width;
            break;
        case SWC_PANEL_EDGE_LEFT:
        case SWC_PANEL_EDGE_RIGHT:
            length = screen->base.geometry.height;
            break;
        default: return;
    }

    if (panel->docked)
        wl_list_remove(&panel->view_listener.link);

    if (panel->screen && screen_changed)
    {
        wl_list_remove(&panel->modifier.link);
        screen_update_usable_geometry(panel->screen);
    }

    panel->screen = screen;
    panel->edge = edge;
    panel->docked = true;

    swc_compositor_add_surface(panel->surface);
    update_position(panel);
    swc_compositor_surface_show(panel->surface);
    wl_signal_add(&panel->surface->view->event_signal, &panel->view_listener);
    wl_list_insert(&screen->modifiers, &panel->modifier.link);

    if (focus)
        swc_keyboard_set_focus(swc.seat->keyboard, panel->surface);

    swc_panel_send_docked(resource, length);
}

static void set_offset(struct wl_client * client, struct wl_resource * resource,
                       uint32_t offset)
{
    struct swc_panel * panel = wl_resource_get_user_data(resource);

    panel->offset = offset;

    if (panel->docked)
        update_position(panel);
}

static void set_strut(struct wl_client * client, struct wl_resource * resource,
                      uint32_t size, uint32_t begin, uint32_t end)
{
    struct swc_panel * panel = wl_resource_get_user_data(resource);

    panel->strut_size = size;

    if (panel->docked)
        screen_update_usable_geometry(panel->screen);
}

static const struct swc_panel_interface panel_implementation = {
    .dock = &dock,
    .set_offset = &set_offset,
    .set_strut = &set_strut
};

static void modify(struct screen_modifier * modifier,
                   const struct swc_rectangle * geometry,
                   pixman_region32_t * usable)
{
    struct swc_panel * panel = CONTAINER_OF(modifier, typeof(*panel), modifier);
    pixman_box32_t box = {
        .x1 = geometry->x, .y1 = geometry->y,
        .x2 = geometry->x + geometry->width,
        .y2 = geometry->y + geometry->height
    };

    assert(panel->docked);

    DEBUG("Original geometry { x1: %d, y1: %d, x2: %d, y2: %d }\n",
          box.x1, box.y1, box.x2, box.y2);

    switch (panel->edge)
    {
        case SWC_PANEL_EDGE_TOP:
            box.y1 = MAX(box.y1, geometry->y + panel->strut_size);
            break;
        case SWC_PANEL_EDGE_BOTTOM:
            box.y2 = MIN(box.y2, geometry->y + geometry->height
                         - panel->strut_size);
            break;
        case SWC_PANEL_EDGE_LEFT:
            box.x1 = MAX(box.x1, geometry->x + panel->strut_size);
            break;
        case SWC_PANEL_EDGE_RIGHT:
            box.x2 = MIN(box.x2, geometry->x + geometry->width
                         - panel->strut_size);
            break;
    }

    DEBUG("Usable region { x1: %d, y1: %d, x2: %d, y2: %d }\n",
          box.x1, box.y1, box.x2, box.y2);

    pixman_region32_reset(usable, &box);
}

static void destroy_panel(struct wl_resource * resource)
{
    struct swc_panel * panel = wl_resource_get_user_data(resource);

    if (panel->docked)
    {
        wl_list_remove(&panel->view_listener.link);
        wl_list_remove(&panel->modifier.link);
        screen_update_usable_geometry(panel->screen);
        swc_compositor_remove_surface(panel->surface);
    }

    free(panel);
}

static void handle_view_event(struct wl_listener * listener, void * data)
{
    struct swc_panel * panel;
    struct swc_event * event = data;

    panel = CONTAINER_OF(listener, typeof(*panel), view_listener);

    switch (event->type)
    {
        case SWC_VIEW_EVENT_RESIZED:
            update_position(panel);
            break;
    }
}

static void handle_surface_destroy(struct wl_listener * listener, void * data)
{
    struct swc_panel * panel;

    panel = CONTAINER_OF(listener, typeof(*panel), surface_destroy_listener);
    wl_resource_destroy(panel->resource);
}

struct swc_panel * swc_panel_new(struct wl_client * client, uint32_t id,
                                 struct swc_surface * surface)
{
    struct swc_panel * panel;

    panel = malloc(sizeof *panel);

    if (!panel)
        goto error0;

    panel->resource = wl_resource_create(client, &swc_panel_interface, 1, id);

    if (!panel->resource)
        goto error1;

    wl_resource_set_implementation(panel->resource, &panel_implementation,
                                   panel, &destroy_panel);

    panel->surface = surface;
    panel->surface_destroy_listener.notify = &handle_surface_destroy;
    panel->view_listener.notify = &handle_view_event;
    panel->modifier.modify = &modify;
    panel->screen = NULL;
    panel->offset = 0;
    panel->strut_size = 0;
    panel->docked = false;

    wl_resource_add_destroy_listener(surface->resource,
                                     &panel->surface_destroy_listener);

    return panel;

  error1:
    free(panel);
  error0:
    return NULL;
}

