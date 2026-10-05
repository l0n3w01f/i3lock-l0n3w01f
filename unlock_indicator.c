/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2010 Michael Stapelberg
 *
 * See LICENSE for licensing information
 *
 */
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon.h>
#include <ev.h>
#include <cairo.h>
#include <cairo/cairo-xcb.h>
#include <time.h>

#include "i3lock.h"
#include "xcb.h"
#include "unlock_indicator.h"
#include "randr.h"
#include "dpi.h"

#define BUTTON_RADIUS 90
#define BUTTON_SPACE (BUTTON_RADIUS + 5)
#define BUTTON_CENTER (BUTTON_RADIUS + 5)
#define BUTTON_DIAMETER (2 * BUTTON_SPACE)
#define TIME_FORMAT_12 "%l:%M %p"
#define TIME_FORMAT_24 "%k:%M"

/*******************************************************************************
 * Variables defined in i3lock.c.
 ******************************************************************************/
static struct ev_periodic *time_redraw_tick;

extern bool debug_mode;

/* The current position in the input buffer. Useful to determine if any
 * characters of the password have already been entered or not. */
extern int input_position;

/* The lock window. */
extern xcb_window_t win;

/* The current resolution of the X11 root window. */
extern uint32_t last_resolution[2];

/* Whether the unlock indicator is enabled (defaults to true). */
extern bool unlock_indicator;

/* List of pressed modifiers, or NULL if none are pressed. */
extern char *modifier_string;
/* Name of the current keyboard layout or NULL if not initialized. */
char *layout_string = NULL;

/* A Cairo surface containing the specified image (-i), if any. */
extern cairo_surface_t *img;

/* Whether the image should be tiled. */
extern bool tile;

/* The background color to use (in hex). */
extern char color[7];

/* Verify color to use (in hex). */
extern char verifycolor[7];

/* Wrong/Error color to use (in hex). */
extern char wrongcolor[7];

/* Idle color to use (in hex). */
extern char idlecolor[7];

/* Use 24 hour time format */
extern bool use24hour;

/* Whether the failed attempts should be displayed. */
extern bool show_failed_attempts;
/* Whether keyboard layout should be displayed. */
extern bool show_keyboard_layout;
/* Number of failed unlock attempts. */
extern int failed_attempts;

extern struct xkb_keymap *xkb_keymap;
extern struct xkb_state *xkb_state;

/*******************************************************************************
 * Variables defined in xcb.c.
 ******************************************************************************/

/* The root screen, to determine the DPI. */
extern xcb_screen_t *screen;

/*******************************************************************************
 * Local variables.
 ******************************************************************************/

/* Cache the screen’s visual, necessary for creating a Cairo context. */
static xcb_visualtype_t *vistype;

/* Maintain the current unlock/PAM state to draw the appropriate unlock
 * indicator. 
 */
unlock_state_t unlock_state;
auth_state_t auth_state;

static void string_append(char **string_ptr, const char *appended) {
    char *tmp = NULL;
    if (*string_ptr == NULL) {
        if (asprintf(&tmp, "%s", appended) != -1) {
            *string_ptr = tmp;
        }
    } else if (asprintf(&tmp, "%s, %s", *string_ptr, appended) != -1) {
        free(*string_ptr);
        *string_ptr = tmp;
    }
}

/* Draws text centered in the unlock indicator using the current source color. */
static void display_button_text(cairo_t *ctx, const char *text, double y_offset) {
    cairo_text_extents_t extents;
    double x, y;

    cairo_text_extents(ctx, text, &extents);
    x = BUTTON_CENTER - ((extents.width / 2) + extents.x_bearing);
    y = BUTTON_CENTER - ((extents.height / 2) + extents.y_bearing) + y_offset;

    cairo_move_to(ctx, x, y);
    cairo_show_text(ctx, text);
    cairo_close_path(ctx);
}

/* Sets the color based on argument (color/background, verify, wrong, idle)
 * and type (line, background and fill). Type defines alpha value and tint.
 */
static void set_color(cairo_t *cr, const char *colorarg, char colortype) {
    char strgroups[3][3] = {{colorarg[0], colorarg[1], '\0'},
                            {colorarg[2], colorarg[3], '\0'},
                            {colorarg[4], colorarg[5], '\0'}};
    uint32_t rgb16[3] = {(strtol(strgroups[0], NULL, 16)),
                         (strtol(strgroups[1], NULL, 16)),
                         (strtol(strgroups[2], NULL, 16))};

    switch (colortype) {
        case 'b': /* Background */
            cairo_set_source_rgb(cr, rgb16[0] / 255.0, rgb16[1] / 255.0, rgb16[2] / 255.0);
            break;
        case 'l': /* Line and text */
            cairo_set_source_rgba(cr, rgb16[0] / 255.0, rgb16[1] / 255.0, rgb16[2] / 255.0, 0.8);
            break;
        case 'f': /* Fill */
            /* Use a lighter tint of the user defined color for circle fill */
            for (int i = 0; i < 3; i++) {
                rgb16[i] = ((255 - rgb16[i]) * .5) + rgb16[i];
            }
            cairo_set_source_rgba(cr, rgb16[0] / 255.0, rgb16[1] / 255.0, rgb16[2] / 255.0, 0.2);
            break;
    }
}

/* Use the appropriate color for the different PAM states
 * (currently verifying, wrong password, or idle)
 */
static void set_auth_color(cairo_t *cr, char colortype) {
    switch (auth_state) {
        case STATE_AUTH_VERIFY:
            set_color(cr, verifycolor, colortype);
            break;
        case STATE_AUTH_LOCK:
            set_color(cr, idlecolor, colortype);
            break;
        case STATE_AUTH_WRONG:
        case STATE_I3LOCK_LOCK_FAILED:
            set_color(cr, wrongcolor, colortype);
            break;
        case STATE_AUTH_IDLE:
            if (unlock_state == STATE_BACKSPACE_ACTIVE) {
                set_color(cr, wrongcolor, colortype);
            } else {
                set_color(cr, idlecolor, colortype);
            }
            break;
    }
}

static void update_layout_string() {
    if (layout_string) {
        free(layout_string);
        layout_string = NULL;
    }
    xkb_layout_index_t num_layouts = xkb_keymap_num_layouts(xkb_keymap);
    for (xkb_layout_index_t i = 0; i < num_layouts; ++i) {
        if (xkb_state_layout_index_is_active(xkb_state, i, XKB_STATE_LAYOUT_EFFECTIVE)) {
            const char *name = xkb_keymap_layout_get_name(xkb_keymap, i);
            if (name) {
                string_append(&layout_string, name);
            }
        }
    }
}

/* check_modifier_keys describes the currently active modifiers (Caps Lock, Alt,
   Num Lock or Super) in the modifier_string variable. */
static void check_modifier_keys(void) {
    xkb_mod_index_t idx, num_mods;
    const char *mod_name;

    num_mods = xkb_keymap_num_mods(xkb_keymap);

    for (idx = 0; idx < num_mods; idx++) {
        if (!xkb_state_mod_index_is_active(xkb_state, idx, XKB_STATE_MODS_EFFECTIVE)) {
            continue;
        }

        mod_name = xkb_keymap_mod_get_name(xkb_keymap, idx);
        if (mod_name == NULL) {
            continue;
        }

        /* Replace certain xkb names with nicer, human-readable ones. */
        if (strcmp(mod_name, XKB_MOD_NAME_CAPS) == 0) {
            mod_name = "Caps Lock";
        } else if (strcmp(mod_name, XKB_MOD_NAME_NUM) == 0) {
            mod_name = "Num Lock";
        } else {
            /* Show only Caps Lock and Num Lock, other modifiers (e.g. Shift)
             * leak state about the password. */
            continue;
        }
        string_append(&modifier_string, mod_name);
    }
}

/*
 * Draws global image with fill color onto a pixmap with the given
 * resolution and returns it.
 */
void draw_image(xcb_pixmap_t bg_pixmap, uint32_t *resolution) {
    const double scaling_factor = get_dpi_value() / 96.0;
    int button_diameter_physical = ceil(scaling_factor * BUTTON_DIAMETER);
    DEBUG("scaling_factor is %.f, physical diameter is %d px\n",
          scaling_factor, button_diameter_physical);

    if (!vistype) {
        vistype = get_root_visual_type(screen);
    }

    /* Initialize cairo: Create one in-memory surface to render the unlock
     * indicator on, create one XCB surface to actually draw (one or more,
     * depending on the amount of screens) unlock indicators on. 
     */
    cairo_surface_t *output = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, button_diameter_physical, button_diameter_physical);
    cairo_t *ctx = cairo_create(output);

    cairo_surface_t *xcb_output = cairo_xcb_surface_create(conn, bg_pixmap, vistype, resolution[0], resolution[1]);
    cairo_t *xcb_ctx = cairo_create(xcb_output);

    /* After the first iteration, the pixmap will still contain the previous
     * contents. Explicitly clear the entire pixmap with the background color
     * first to get back into a defined state: */
    set_color(xcb_ctx, color, 'b');
    cairo_rectangle(xcb_ctx, 0, 0, resolution[0], resolution[1]);
    cairo_fill(xcb_ctx);

    if (img) {
        if (!tile) {
            cairo_set_source_surface(xcb_ctx, img, 0, 0);
            cairo_paint(xcb_ctx);
        } else {
            /* create a pattern and fill a rectangle as big as the screen */
            cairo_pattern_t *pattern;
            pattern = cairo_pattern_create_for_surface(img);
            cairo_set_source(xcb_ctx, pattern);
            cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
            cairo_rectangle(xcb_ctx, 0, 0, resolution[0], resolution[1]);
            cairo_fill(xcb_ctx);
            cairo_pattern_destroy(pattern);
        }
    }

    if (unlock_indicator) {
        cairo_scale(ctx, scaling_factor, scaling_factor);
        /* Draw a (centered) circle with transparent background. */
        cairo_set_line_width(ctx, 3.0);
        cairo_arc(ctx,
                  BUTTON_CENTER /* x */,
                  BUTTON_CENTER /* y */,
                  BUTTON_RADIUS /* radius */,
                  0 /* start */,
                  2 * M_PI /* end */);

        set_auth_color(ctx, 'f');
        cairo_fill_preserve(ctx);

        /* Circle border */
        set_auth_color(ctx, 'l');
        cairo_stroke(ctx);

        /* Display (centered) Time */
        char timetext[32] = "";

        time_t curtime = time(NULL);
        struct tm *tm = localtime(&curtime);
        if (tm != NULL) {
            strftime(timetext, sizeof(timetext), use24hour ? TIME_FORMAT_24 : TIME_FORMAT_12, tm);
        }

        /* Text */
        set_auth_color(ctx, 'l');
        cairo_set_font_size(ctx, 32.0);

        display_button_text(ctx, timetext, 0.);

        if (modifier_string != NULL) {
            cairo_set_font_size(ctx, 14.0);
            display_button_text(ctx, modifier_string, 28.);
        }
        if (show_keyboard_layout && layout_string != NULL) {
            cairo_set_font_size(ctx, 14.0);
            display_button_text(ctx, layout_string, -28.);
        }

        /* After the user pressed any valid key or the backspace key, we
         * highlight a random part of the unlock indicator to confirm this
         * keypress. */
        if (unlock_state == STATE_KEY_ACTIVE ||
            unlock_state == STATE_BACKSPACE_ACTIVE) {
            cairo_set_line_width(ctx, 4);
            cairo_new_sub_path(ctx);
            double highlight_start = (rand() % (int)(2 * M_PI * 100)) / 100.0;
            cairo_arc(ctx,
                      BUTTON_CENTER /* x */,
                      BUTTON_CENTER /* y */,
                      BUTTON_RADIUS /* radius */,
                      highlight_start,
                      highlight_start + (M_PI / 2.5)); 

            /* Set newly drawn lines to erase what they're drawn over */
            cairo_set_operator(ctx,CAIRO_OPERATOR_CLEAR); 
            cairo_stroke(ctx);

            /* Back to normal operator */
            cairo_set_operator(ctx,CAIRO_OPERATOR_OVER); 
            cairo_set_line_width(ctx, 10);

            /* Change color of separators based on backspace/active keypress */
            set_auth_color(ctx, 'l');

            /* Separator 1 */
            cairo_arc(ctx,
                BUTTON_CENTER /* x */,
                BUTTON_CENTER /* y */,
                BUTTON_RADIUS /* radius */,
                highlight_start /* start */,
                highlight_start + (M_PI / 128.0) /* end */);
            cairo_stroke(ctx);

            /* Separator 2 */
            cairo_arc(ctx,
                BUTTON_CENTER /* x */,
                BUTTON_CENTER /* y */,
                BUTTON_RADIUS /* radius */,
                highlight_start + (M_PI / 2.5) /* start */,
                (highlight_start + (M_PI / 2.5)) + (M_PI / 128.0) /* end */);
            cairo_stroke(ctx);
        }
    }

    if (xr_screens > 0) {
        /* Composite the unlock indicator in the middle of each screen. */
        for (int screen = 0; screen < xr_screens; screen++) {
            int x = (xr_resolutions[screen].x + ((xr_resolutions[screen].width / 2) - (button_diameter_physical / 2)));
            int y = (xr_resolutions[screen].y + ((xr_resolutions[screen].height / 2) - (button_diameter_physical / 2)));
            cairo_set_source_surface(xcb_ctx, output, x, y);
            cairo_rectangle(xcb_ctx, x, y, button_diameter_physical, button_diameter_physical);
            cairo_fill(xcb_ctx);
        }
    } else {
        /* We have no information about the screen sizes/positions, so we just
         * place the unlock indicator in the middle of the X root window and
         * hope for the best. */
        int x = (last_resolution[0] / 2) - (button_diameter_physical / 2);
        int y = (last_resolution[1] / 2) - (button_diameter_physical / 2);
        cairo_set_source_surface(xcb_ctx, output, x, y);
        cairo_rectangle(xcb_ctx, x, y, button_diameter_physical, button_diameter_physical);
        cairo_fill(xcb_ctx);
    }

    cairo_surface_destroy(xcb_output);
    cairo_surface_destroy(output);
    cairo_destroy(ctx);
    cairo_destroy(xcb_ctx);
}

static xcb_pixmap_t bg_pixmap = XCB_NONE;

/*
 * Releases the current background pixmap so that the next redraw_screen() call
 * will allocate a new one with the updated resolution.
 *
 */
void free_bg_pixmap(void) {
    xcb_free_pixmap(conn, bg_pixmap);
    bg_pixmap = XCB_NONE;
}

/* Calls draw_image on a new pixmap and swaps that with the current pixmap */
void redraw_screen(void) {
    DEBUG("redraw_screen(unlock_state = %d, auth_state = %d)\n", unlock_state, auth_state);

    if (modifier_string) {
        free(modifier_string);
        modifier_string = NULL;
    }
    check_modifier_keys();
    update_layout_string();

    if (bg_pixmap == XCB_NONE) {
        DEBUG("allocating pixmap for %d x %d px\n", last_resolution[0], last_resolution[1]);
        bg_pixmap = create_bg_pixmap(conn, screen, last_resolution, color);
    }

    draw_image(bg_pixmap, last_resolution);
    xcb_change_window_attributes(conn, win, XCB_CW_BACK_PIXMAP, (uint32_t[1]){bg_pixmap});
    /* XXX: Possible optimization: Only update the area in the middle of the
     * screen instead of the whole screen. */
    xcb_clear_area(conn, 0, win, 0, 0, last_resolution[0], last_resolution[1]);
    xcb_flush(conn);
}

/* Always show unlock indicator. */

void clear_indicator(void) {
    if (input_position == 0) {
        unlock_state = STATE_STARTED;
    } else {
        unlock_state = STATE_KEY_PRESSED;
    }
    redraw_screen();
}

/* Periodic redraw for clock updates - taken from github.com/ravinrabbid/i3lock-clock */

static void time_redraw_cb(struct ev_loop *loop, ev_periodic *w, int revents) {
    redraw_screen();
}

void start_time_redraw_tick(struct ev_loop* main_loop) {
    if (time_redraw_tick) {
        ev_periodic_set(time_redraw_tick, 1.0, 60., 0);
        ev_periodic_again(main_loop, time_redraw_tick);
    } else {
        /* When there is no memory, we just don’t have a timeout. We cannot
        * exit() here, since that would effectively unlock the screen. */
        if (!(time_redraw_tick = calloc(1, sizeof(struct ev_periodic)))) {
            return;
        }
        ev_periodic_init(time_redraw_tick,time_redraw_cb, 1.0, 60., 0);
        ev_periodic_start(main_loop, time_redraw_tick);
    }
}
