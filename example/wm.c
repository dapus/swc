/* swc: example/wm.c
 *
 * Copyright (c) 2014 Michael Forney
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

#include <stdlib.h>
#include <swc.h>
#include <unistd.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>

struct screen
{
    struct swc_screen * swc;
    struct wl_listener event_listener;
    struct wl_list windows;
    unsigned num_windows;
};

struct window
{
    struct swc_window * swc;
    struct wl_listener event_listener;
    struct screen * screen;
    struct wl_list link;
};

static const char * terminal_command[] = { "st-wl", NULL };
static const char * dmenu_command[] = { "dmenu_run-wl", NULL };
static const uint32_t border_width = 1;
static const uint32_t border_color_active = 0xff333388;
static const uint32_t border_color_normal = 0xff888888;

static struct screen * active_screen;
static struct window * focused_window;
static struct wl_event_loop * event_loop;
static struct wl_event_source * focus_idle_source;

/* This is a basic grid arrange function that tries to give each window an
 * equal space. */
static void arrange(struct screen * screen)
{
    struct window * window = NULL;
    unsigned num_columns, num_rows, column_index, row_index;
    struct swc_rectangle geometry;
    struct swc_rectangle * screen_geometry = &screen->swc->usable_geometry;

    if (screen->num_windows == 0) return;

    num_columns = ceil(sqrt(screen->num_windows));
    num_rows = screen->num_windows / num_columns + 1;
    window = wl_container_of(screen->windows.next, window, link);

    for (column_index = 0; &window->link != &screen->windows; ++column_index)
    {
        geometry.x = screen_geometry->x + border_width
            + screen_geometry->width * column_index / num_columns;
        geometry.width = screen_geometry->width / num_columns
            - 2 * border_width;

        if (column_index == screen->num_windows % num_columns)
            --num_rows;

        for (row_index = 0; row_index < num_rows; ++row_index)
        {
            geometry.y = screen_geometry->y + border_width
                + screen_geometry->height * row_index / num_rows;
            geometry.height = screen_geometry->height / num_rows
                - 2 * border_width;

            swc_window_set_geometry(window->swc, &geometry);
            window = wl_container_of(window->link.next, window, link);
        }
    }
}

static void screen_add_window(struct screen * screen, struct window * window)
{
    window->screen = screen;
    wl_list_insert(&screen->windows, &window->link);
    ++screen->num_windows;
    swc_window_show(window->swc);
    arrange(screen);
}

static void screen_remove_window(struct screen * screen, struct window * window)
{
    window->screen = NULL;
    wl_list_remove(&window->link);
    --screen->num_windows;
    swc_window_hide(window->swc);
    arrange(screen);
}

/*
 * We can't immediately change the focus when the currently focused window is
 * being destroyed. This is because the keyboard registers a destroy listener
 * for the current focus so it can correctly keep track of it's focus
 * internally and set it to NULL. However, now we have two separate destroy
 * listeners which want to change the focus to two different things, and it
 * isn't defined which order they happen (and if they are adjacent, we end up
 * breaking the linked list traversal).
 *
 * To prevent this, instead of changing the focus immediately, we register an
 * idle event so that the focus is changed in the next iteration of the event
 * loop.
 *
 * Another way to solve this issue is internally in swc by requiring that all
 * window managers maintain a valid focus at all times. This way, the keyboard
 * does not need to register a destroy listener for the focus because it knows
 * that the window manager will provide a new focus before the old one becomes
 * invalid.
 */
static void commit_focus(void * data)
{
    swc_window_focus(focused_window ? focused_window->swc : NULL);
    focus_idle_source = NULL;
}

static void focus(struct window * window)
{
    /* Add an idle source (if one doesn't already exist) to the event loop to
     * actually change the keyboard focus. */
    if (!focus_idle_source)
    {
        focus_idle_source = wl_event_loop_add_idle(event_loop,
                                                   &commit_focus, NULL);
    }

    if (focused_window)
    {
        swc_window_set_border(focused_window->swc,
                              border_color_normal, border_width);
    }

    if (window)
        swc_window_set_border(window->swc, border_color_active, border_width);

    focused_window = window;
}

static void window_event(struct wl_listener * listener, void * data)
{
    struct swc_event * event = data;
    struct window * window = NULL, * next_focus;


    window = wl_container_of(listener, window, event_listener);

    switch (event->type)
    {
        case SWC_WINDOW_DESTROYED:
            if (focused_window == window)
            {
                /* Try to find a new focus nearby the old one. */
                next_focus = wl_container_of(window->link.next, window, link);

                if (&next_focus->link == &window->screen->windows)
                {
                    next_focus = wl_container_of(window->link.prev,
                                                 window, link);

                    if (&next_focus->link == &window->screen->windows)
                        next_focus = NULL;
                }

                focus(next_focus);
            }

            screen_remove_window(window->screen, window);
            free(window);
            break;
        case SWC_WINDOW_STATE_CHANGED:
            /* When the window changes state to TOPLEVEL, we can add it to the
             * current screen and then rearrange the windows on that screen. */
            if (window->swc->state == SWC_WINDOW_STATE_TOPLEVEL)
            {
                screen_add_window(active_screen, window);
                focus(window);
            }
            break;
        case SWC_WINDOW_ENTERED:
            focus(window);
            break;
    }
}

static void new_window(struct swc_window * swc)
{
    struct window * window;

    window = malloc(sizeof *window);

    if (!window)
        return;

    window->swc = swc;
    window->event_listener.notify = &window_event;
    window->screen = NULL;

    /* Register a listener for the window's event signal. */
    wl_signal_add(&swc->event_signal, &window->event_listener);
}

static void screen_event(struct wl_listener * listener, void * data)
{
    struct swc_event * event = data;
    struct screen * screen = NULL;

    screen = wl_container_of(listener, screen, event_listener);

    switch (event->type)
    {
        case SWC_SCREEN_USABLE_GEOMETRY_CHANGED:
            /* If the usable geometry of the screen changes, for example when a
             * panel is docked to the edge of the screen, we need to rearrange
             * the windows to ensure they are all within the new usable
             * geometry. */
            arrange(screen);
            break;
    }
}

static void new_screen(struct swc_screen * swc)
{
    struct screen * screen;

    screen = malloc(sizeof *screen);

    if (!screen)
        return;

    screen->swc = swc;
    screen->event_listener.notify = &screen_event;
    screen->num_windows = 0;
    wl_list_init(&screen->windows);

    /* Register a listener for the screen's event signal. */
    wl_signal_add(&swc->event_signal, &screen->event_listener);
    active_screen = screen;
}

const struct swc_manager manager = { &new_window, &new_screen };

static void spawn(void * data, uint32_t time, uint32_t value, uint32_t state)
{
    char * const * command = data;

    if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return;

    if (fork() == 0)
    {
        execvp(command[0], command);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char * argv[])
{
    struct wl_display * display;

    display = wl_display_create();

    if (!display)
        return EXIT_FAILURE;

    if (wl_display_add_socket(display, NULL) != 0)
        return EXIT_FAILURE;

    if (!swc_initialize(display, NULL, &manager))
        return EXIT_FAILURE;

    swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, XKB_KEY_Return,
                    &spawn, terminal_command);
    swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, XKB_KEY_r,
                    &spawn, dmenu_command);

    event_loop = wl_display_get_event_loop(display);
    wl_display_run(display);

    return EXIT_SUCCESS;
}

