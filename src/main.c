/*
 * Copyright (c) 2024 Your Name
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* Define the size of the box */
#define BOX_WIDTH  40
#define BOX_HEIGHT 40

int main(void)
{
    /* Get the display device */
    const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    struct display_capabilities caps;
    uint32_t display_width, display_height;

    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display device not ready.");
        return 0;
    }

    /* Get display capabilities */
    display_get_capabilities(display_dev, &caps);
    display_width = caps.x_resolution;
    display_height = caps.y_resolution;

    LOG_INF("Display resolution: %d x %d", display_width, display_height);
    LOG_INF("Supported pixel formats: %x", caps.supported_pixel_formats);
    LOG_INF("Current pixel format: %x", caps.current_pixel_format);
    LOG_INF("Current orientation: %d", caps.current_orientation);


    /* Define a buffer for our 10x10 pixel box */
    /* NOTE: The buffer format must match the display's native format.
     * This example assumes a 24-bit RGB888 format, which is common.
     * If your display uses a different format (e.g., MONO01, RGB565),
     * you will need to adjust the buffer size and how you set the pixel color.
     * For MONO01, a 1-bit-per-pixel format, you would need a buffer of
     * (BOX_WIDTH * BOX_HEIGHT) / 8 bytes.
     */
    uint8_t buf[(BOX_WIDTH * BOX_HEIGHT)/8]; // 1 byte per pixel for MONO01

    /* Fill the buffer with black color (R=0, G=0, B=0) */
    for (int i = 0; i < sizeof(buf); i++) {
        buf[i] = 0x00;
    }

    /* Define the buffer descriptor */
    struct display_buffer_descriptor buf_desc = {
        .buf_size = sizeof(buf),
        .width = BOX_WIDTH,
        .height = BOX_HEIGHT,
        .pitch = BOX_WIDTH, /* Pitch is the width of the buffer */
    };


	/* Clear the entire screen to a default color (optional, but good practice) */
    /* For this, we can just write a black buffer to the whole screen */
    display_blanking_on(display_dev);

    /* Calculate the center of the screen */
	uint16_t x_center = 0;
    uint16_t y_center = 0;
	for (uint8_t i = 0; i < 10; i++) {
		x_center = BOX_WIDTH * i;
		y_center = x_center;

		LOG_INF("Drawing box at x:%d, y:%d", x_center, y_center);
		/* Write the buffer to the display */
		int ret = display_write(display_dev, x_center, y_center, &buf_desc, buf);

		if (ret != 0) {
			LOG_ERR("Failed to write to display: %d", ret);
			return 0;
		}

		k_msleep(250);
	}
	display_blanking_off(display_dev);

	LOG_INF("Black box drawn successfully in the center of the display.");
    

    /* The application can now enter a low-power state or do other work */
    while (1) {
        k_sleep(K_FOREVER);
    }
    return 0;
}
