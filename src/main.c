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
#include <zephyr/settings/settings.h>

#include <zephyr/net/openthread.h>
#include <openthread/thread.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* Define the size of the box */
#define BOX_WIDTH  40
#define BOX_HEIGHT 40


static void on_thread_state_changed(otChangedFlags flags, void *user_data)
{
	if (flags & OT_CHANGED_THREAD_ROLE) {
		switch (otThreadGetDeviceRole(openthread_get_default_instance())) {
		case OT_DEVICE_ROLE_CHILD:
		case OT_DEVICE_ROLE_ROUTER:
		case OT_DEVICE_ROLE_LEADER:
			LOG_INF("OpenThread connected");
			break;

		case OT_DEVICE_ROLE_DISABLED:
		case OT_DEVICE_ROLE_DETACHED:
		default:
			LOG_INF("OpenThread detached");
			break;
		}
	}
    else if (flags & OT_CHANGED_PARENT_LINK_QUALITY) {
        otRouterInfo parentInfo;
        otError err = otThreadGetParentInfo(openthread_get_default_instance(), &parentInfo);
        if (err != OT_ERROR_NONE) {
            LOG_INF("Error getting parent status: %d", err);
        }
        else {
            LOG_INF("Parent link quality changed: in: %u out: %u cost: %u", parentInfo.mLinkQualityIn, parentInfo.mLinkQualityOut, parentInfo.mPathCost);
        }
        
    }
}

static struct openthread_state_changed_callback ot_state_chaged_cb = {
	.otCallback = on_thread_state_changed};

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

    // OT does a setting write every startup to save new network and parent info.
    // I _really_ don't like this unneccessary flash wear.
    // 

    // otDatasetParseTlvs -> convert binary dataset into real dataset
    // otDatasetSetActiveTlvs -> Sets binary dataset as active dataset.
    //
    // Can we disable settings integration and just do an `otDatasetSetActiveTlvs` on every boot?
    // Probably will take a bit longer.
    // Alternatively, can we disable writing to `kKeyNetworkInfo` / `kKeyParentInfo`?

    openthread_state_changed_callback_register(&ot_state_chaged_cb);
    LOG_INF("Starting OpenThread!");
    openthread_run();

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
