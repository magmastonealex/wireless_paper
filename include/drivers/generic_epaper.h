#pragma once

#include <zephyr/device.h>
#include <zephyr/toolchain.h>


// This is a driver intended to support lots of common e-paper displays, mostly from Good-Display.
// These displays share a common connector (24 pin 0.5mm pitch FPC) and pinout. They all have a different controller,
// but for the most part can be treated ~identically.
// This is not integrated into the graphics subsystem (those drivers exist already for many displays).
// I want to support a very specific use case:
// - Support for the weird n-bit-per-pixel color spaces (including the really weird ones with 1bpp but across multiple color planes)
// - I will write all of the data for the display in chunks, but in one linear pass (this can be extended in the future for partial writes or offsets)
// - I want to dynamically (i.e. at _runtime_) select which display will be used. This allows me to use the same board and firmware image on lots of different displays.
// Chances are, you don't want to use this driver - use the perfectly good display drivers already in the zephyr tree.
// This is really only useful for a project where the whole point is just to "be" an epaper display, and you want to be able to swap them in and out with ease.

typedef int (*epd_cmd_t)(const struct device *dev);

// Some epds have multiple display planes (i.e. one for black/white and another for red/white)
#define EPD_DISPLAY_PLANE_MAIN 1

struct epd_dimensions {
    int width;
    int height;
    int planes;
    size_t expected_data_size;
};

typedef enum {
    EPD_TYPE_GDEY029T71H = 0, // 2.9 b/w (not working at the moment)
    EPD_TYPE_GDEM035F51 = 1, // 3.5 4-color
    EPD_TYPE_GDEY029F51 = 2, // 2.9 4-color
    EPD_TYPE_GDEM075F52 = 3, // 7.5 4-color
    EPD_TYPE_WS_75_V2B = 4, // 7.5 2-color + red?
    MAX_EPD
} epd_type_t;

int epd_get_dimensions(const struct device *dev, struct epd_dimensions *dims);
int epd_set_type(const struct device *dev, epd_type_t typ); // Set the type of the e-paper display. This needs to be called before any of the other methods.
int epd_power_on(const struct device *dev); // Power up the display and get it ready to draw.
int epd_start_write_data(const struct device *dev, int plane); // Write data to a specific plane.
int epd_continue_write_data(const struct device *dev, uint8_t *data, size_t data_len);
int epd_do_refresh(const struct device *dev); // Complete the write and refresh the display.
int epd_power_off(const struct device *dev); // Shut down the display, will disable power at the right moment as well.