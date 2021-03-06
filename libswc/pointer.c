/* swc: libswc/pointer.c
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

#include "pointer.h"
#include "event.h"
#include "internal.h"
#include "screen.h"
#include "shm.h"
#include "util.h"
#include "cursor/cursor_data.h"

#include <stdio.h>
#include <assert.h>
#include <wld/wld.h>

static void enter(struct swc_input_focus_handler * handler,
                  struct wl_resource * resource, struct swc_surface * surface)
{
    struct swc_pointer * pointer;
    struct wl_client * client;
    struct wl_display * display;
    uint32_t serial;
    wl_fixed_t surface_x, surface_y;

    pointer = CONTAINER_OF(handler, typeof(*pointer), focus_handler);
    client = wl_resource_get_client(resource);
    display = wl_client_get_display(client);
    serial = wl_display_next_serial(display);

    surface_x = pointer->x - wl_fixed_from_int(surface->view->geometry.x);
    surface_y = pointer->y - wl_fixed_from_int(surface->view->geometry.y);

    wl_pointer_send_enter(resource, serial, surface->resource,
                          surface_x, surface_y);
}

static void leave(struct swc_input_focus_handler * handler,
                  struct wl_resource * resource, struct swc_surface * surface)
{
    struct wl_client * client;
    struct wl_display * display;
    uint32_t serial;

    client = wl_resource_get_client(resource);
    display = wl_client_get_display(client);
    serial = wl_display_next_serial(display);

    wl_pointer_send_leave(resource, serial, surface->resource);
}

static void handle_cursor_surface_destroy(struct wl_listener * listener,
                                          void * data)
{
    struct swc_pointer * pointer = CONTAINER_OF(listener, typeof(*pointer),
                                                cursor.destroy_listener);

    swc_view_attach(&pointer->cursor.view, NULL);
}

static bool update(struct swc_view * view)
{
    return true;
}

static bool attach(struct swc_view * view, struct wld_buffer * buffer)
{
    struct swc_pointer * pointer
        = CONTAINER_OF(view, typeof(*pointer), cursor.view);
    struct swc_surface * surface = pointer->cursor.surface;

    if (surface && !pixman_region32_not_empty(&surface->state.damage))
        return true;

    wld_set_target_buffer(swc.shm->renderer, pointer->cursor.buffer);
    wld_fill_rectangle(swc.shm->renderer, 0x00000000, 0, 0, 64, 64);
    wld_copy_rectangle(swc.shm->renderer, buffer, 0, 0, 0, 0,
                       buffer->width, buffer->height);
    wld_flush(swc.shm->renderer);

    if (surface)
        pixman_region32_clear(&surface->state.damage);

    /* TODO: Send an early release to the buffer */

    swc_view_set_size_from_buffer(view, buffer);

    return true;
}

static bool move(struct swc_view * view, int32_t x, int32_t y)
{
    swc_view_set_position(view, x, y);

    return true;
}

static const struct swc_view_impl view_impl = {
    .update = &update,
    .attach = &attach,
    .move = &move,
};

static void handle_view_event(struct wl_listener * listener, void * data)
{
    struct swc_pointer * pointer
        = CONTAINER_OF(listener, typeof(*pointer), cursor.view_listener);
    struct swc_event * event = data;
    struct swc_view_event_data * event_data = event->data;
    struct swc_view * view = event_data->view;
    struct screen * screen;

    switch (event->type)
    {
        case SWC_VIEW_EVENT_MOVED:
            wl_list_for_each(screen, &swc.screens, link)
            {
                swc_view_move(&screen->planes.cursor.view,
                              view->geometry.x, view->geometry.y);
            }

            swc_view_update_screens(view);
            break;
        case SWC_VIEW_EVENT_RESIZED:
            swc_view_update_screens(view);
            break;
        case SWC_VIEW_EVENT_SCREENS_CHANGED:
            wl_list_for_each(screen, &swc.screens, link)
            {
                if (event_data->screens_changed.entered & screen_mask(screen))
                {
                    swc_view_attach(&screen->planes.cursor.view,
                                    pointer->cursor.buffer);
                }
                else if (event_data->screens_changed.left & screen_mask(screen))
                    swc_view_attach(&screen->planes.cursor.view, NULL);
            }
            break;
    }
}

static inline void update_cursor(struct swc_pointer * pointer)
{
    swc_view_move(&pointer->cursor.view,
                  wl_fixed_to_int(pointer->x) - pointer->cursor.hotspot.x,
                  wl_fixed_to_int(pointer->y) - pointer->cursor.hotspot.y);
}

void swc_pointer_set_cursor(struct swc_pointer * pointer, uint32_t id)
{
    struct cursor * cursor = &cursor_metadata[id];
    union wld_object object = { .ptr = &cursor_data[cursor->offset] };

    if (pointer->cursor.internal_buffer)
        wld_buffer_unreference(pointer->cursor.internal_buffer);

    pointer->cursor.internal_buffer = wld_import_buffer
        (swc.shm->context, WLD_OBJECT_DATA, object,
         cursor->width, cursor->height, WLD_FORMAT_ARGB8888, cursor->width * 4);

    if (!pointer->cursor.internal_buffer)
    {
        ERROR("Failed to create cursor buffer\n");
        return;
    }

    pointer->cursor.hotspot.x = cursor->hotspot_x;
    pointer->cursor.hotspot.y = cursor->hotspot_y;
    update_cursor(pointer);
    swc_view_attach(&pointer->cursor.view, pointer->cursor.internal_buffer);
}

bool swc_pointer_initialize(struct swc_pointer * pointer)
{
    struct screen * screen;

    /* Center cursor in the geometry of the first screen. */
    screen = CONTAINER_OF(swc.screens.next, typeof(*screen), link);
    pointer->x = wl_fixed_from_int
        (screen->base.geometry.x + screen->base.geometry.width / 2);
    pointer->y = wl_fixed_from_int
        (screen->base.geometry.y + screen->base.geometry.height / 2);

    pointer->focus_handler.enter = &enter;
    pointer->focus_handler.leave = &leave;

    swc_view_initialize(&pointer->cursor.view, &view_impl);
    pointer->cursor.view_listener.notify = &handle_view_event;
    wl_signal_add(&pointer->cursor.view.event_signal,
                  &pointer->cursor.view_listener);
    pointer->cursor.surface = NULL;
    pointer->cursor.destroy_listener.notify = &handle_cursor_surface_destroy;
    pointer->cursor.buffer = wld_create_buffer
        (swc.drm->context, 64, 64, WLD_FORMAT_ARGB8888, WLD_FLAG_MAP);
    pointer->cursor.internal_buffer = NULL;

    if (!pointer->cursor.buffer)
        return false;

    swc_pointer_set_cursor(pointer, cursor_left_ptr);

    swc_input_focus_initialize(&pointer->focus, &pointer->focus_handler);
    pixman_region32_init(&pointer->region);

    return true;
}

void swc_pointer_finalize(struct swc_pointer * pointer)
{
    swc_input_focus_finalize(&pointer->focus);
    pixman_region32_fini(&pointer->region);
}

/**
 * Sets the focus of the pointer to the specified surface.
 */
void swc_pointer_set_focus(struct swc_pointer * pointer,
                           struct swc_surface * surface)
{
    swc_input_focus_set(&pointer->focus, surface);
}

static void clip_position(struct swc_pointer * pointer,
                          wl_fixed_t fx, wl_fixed_t fy)
{
    int32_t x, y, last_x, last_y;
    pixman_box32_t box;

    x = wl_fixed_to_int(fx);
    y = wl_fixed_to_int(fy);
    last_x = wl_fixed_to_int(pointer->x);
    last_y = wl_fixed_to_int(pointer->y);

    if (!pixman_region32_contains_point(&pointer->region, x, y, NULL))
    {
        assert(pixman_region32_contains_point(&pointer->region,
                                              last_x, last_y, &box));

        /* Do some clipping. */
        x = MAX(MIN(x, box.x2 - 1), box.x1);
        y = MAX(MIN(y, box.y2 - 1), box.y1);
    }

    pointer->x = wl_fixed_from_int(x);
    pointer->y = wl_fixed_from_int(y);
}

void swc_pointer_set_region(struct swc_pointer * pointer,
                            pixman_region32_t * region)
{
    pixman_region32_copy(&pointer->region, region);
    clip_position(pointer, pointer->x, pointer->y);
}

static void set_cursor(struct wl_client * client,
                       struct wl_resource * resource, uint32_t serial,
                       struct wl_resource * surface_resource,
                       int32_t hotspot_x, int32_t hotspot_y)
{
    struct swc_pointer * pointer = wl_resource_get_user_data(resource);
    struct swc_surface * surface;

    if (pointer->cursor.surface)
        wl_list_remove(&pointer->cursor.destroy_listener.link);

    surface = surface_resource ? wl_resource_get_user_data(surface_resource)
                               : NULL;
    pointer->cursor.surface = surface;
    pointer->cursor.hotspot.x = hotspot_x;
    pointer->cursor.hotspot.y = hotspot_y;

    if (surface)
    {
        swc_surface_set_view(surface, &pointer->cursor.view);
        wl_resource_add_destroy_listener(surface->resource,
                                         &pointer->cursor.destroy_listener);
        update_cursor(pointer);
    }
}

static struct wl_pointer_interface pointer_implementation = {
    .set_cursor = &set_cursor
};

static void unbind(struct wl_resource * resource)
{
    struct swc_pointer * pointer = wl_resource_get_user_data(resource);

    swc_input_focus_remove_resource(&pointer->focus, resource);
}

struct wl_resource * swc_pointer_bind(struct swc_pointer * pointer,
                                      struct wl_client * client, uint32_t id)
{
    struct wl_resource * client_resource;

    client_resource = wl_resource_create(client, &wl_pointer_interface, 1, id);
    wl_resource_set_implementation(client_resource, &pointer_implementation,
                                   pointer, &unbind);
    swc_input_focus_add_resource(&pointer->focus, client_resource);

    return client_resource;
}

void swc_pointer_handle_button(struct swc_pointer * pointer, uint32_t time,
                               uint32_t button, uint32_t state)
{
    if ((!pointer->handler || !pointer->handler->button
         || !pointer->handler->button(pointer, time, button, state))
        && pointer->focus.resource)
    {
        struct wl_client * client
            = wl_resource_get_client(pointer->focus.resource);
        struct wl_display * display = wl_client_get_display(client);
        uint32_t serial = wl_display_next_serial(display);

        wl_pointer_send_button(pointer->focus.resource, serial, time,
                               button, state);
    }
}

void swc_pointer_handle_axis(struct swc_pointer * pointer, uint32_t time,
                             uint32_t axis, wl_fixed_t amount)
{
    if ((!pointer->handler || !pointer->handler->axis
         || !pointer->handler->axis(pointer, time, axis, amount))
        && pointer->focus.resource)
    {
        wl_pointer_send_axis(pointer->focus.resource, time, axis, amount);
    }
}

void swc_pointer_handle_relative_motion
    (struct swc_pointer * pointer, uint32_t time, wl_fixed_t dx, wl_fixed_t dy)
{
    clip_position(pointer, pointer->x + dx, pointer->y + dy);

    if ((!pointer->handler || !pointer->handler->motion
         || !pointer->handler->motion(pointer, time))
        && pointer->focus.resource)
    {
        wl_fixed_t surface_x, surface_y;
        surface_x = pointer->x
            - wl_fixed_from_int(pointer->focus.surface->view->geometry.x);
        surface_y = pointer->y
            - wl_fixed_from_int(pointer->focus.surface->view->geometry.y);

        wl_pointer_send_motion(pointer->focus.resource, time,
                               surface_x, surface_y);
    }

    update_cursor(pointer);
}

