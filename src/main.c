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
#include <openthread/dataset.h>

#include <app_version.h>

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

static uint8_t tlvData[111] = {
    0x0e, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x13, 0x4a, 0x03, 0x00, 0x00, 0x14, 0x35, 0x06, 0x00, 0x04, 0x00, 0x1f, 0xff, 0xe0, 0x02, 0x08, 0xf3, 0x8d, 0x7d, 0xff, 0xdb, 0x71, 0xbc, 0x4f, 0x07, 0x08, 0xfd, 0xed, 0x56, 0xea, 0xb7, 0xec, 0xb3, 0xae, 0x05, 0x10, 0xde, 0x02, 0xa0, 0xf2, 0x46, 0x27, 0xd7, 0x88, 0xdc, 0x91, 0xe0, 0x82, 0x02, 0xd9, 0x70, 0x67, 0x03, 0x0f, 0x4f, 0x70, 0x65, 0x6e, 0x54, 0x68, 0x72, 0x65, 0x61, 0x64, 0x2d, 0x39, 0x33, 0x63, 0x31, 0x01, 0x02, 0x93, 0xc1, 0x04, 0x10, 0x37, 0x89, 0xf9, 0x85, 0xb8, 0x57, 0x8a, 0x89, 0xbe, 0x72, 0xd7, 0x6d, 0x66, 0xbb, 0x3e, 0x82, 0x0c, 0x04, 0x02, 0xa0, 0xf7, 0xf8
};

void set_ot_data() {
    otOperationalDatasetTlvs tlvs;

    memcpy(tlvs.mTlvs, tlvData, 111);
    tlvs.mLength = 111;
    
    otError err = otDatasetSetActiveTlvs(openthread_get_default_instance(), &tlvs);
    if (err != OT_ERROR_NONE) {
        LOG_ERR("failed to set active TLVs: %d", err);
    }
    else {
        LOG_INF("Set active dataset.");
    }
}

int main(void)
{
    LOG_INF("Starting app version: %s", APP_VERSION_STRING);

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
    //set_ot_data();
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
