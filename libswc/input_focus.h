/* swc: input_focus.h
 *
 * Copyright (c) 2013 Michael Forney
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

#ifndef SWC_INPUT_FOCUS_H
#define SWC_INPUT_FOCUS_H

#include <stdbool.h>
#include <wayland-server.h>

enum
{
    SWC_INPUT_FOCUS_EVENT_CHANGED
};

struct swc_input_focus_event_data
{
    struct swc_surface * old, * new;
};

struct swc_input_focus_handler
{
    void (* enter)(struct swc_input_focus_handler * handler,
                   struct wl_resource * resource,
                   struct swc_surface * surface);
    void (* leave)(struct swc_input_focus_handler * handler,
                   struct wl_resource * resource,
                   struct swc_surface * surface);
};

struct swc_input_focus
{
    struct wl_resource * resource;
    struct swc_surface * surface;
    struct wl_listener surface_destroy_listener;

    struct swc_input_focus_handler * handler;
    struct wl_list resources;

    struct wl_signal event_signal;
};

bool swc_input_focus_initialize(struct swc_input_focus * input_focus,
                                struct swc_input_focus_handler * input_handler);

void swc_input_focus_finalize(struct swc_input_focus * input_focus);

void swc_input_focus_add_resource(struct swc_input_focus * input_focus,
                                  struct wl_resource * resource);

void swc_input_focus_remove_resource(struct swc_input_focus * input_focus,
                                     struct wl_resource * resource);

void swc_input_focus_set(struct swc_input_focus * input_focus,
                         struct swc_surface * surface);

#endif

