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

#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/coap_client.h>

#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>

#include <app_version.h>
#include <zephyr/sys/util.h>

#include <stdio.h>

#include "heatshrink/heatshrink_decoder.h"

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



static struct coap_client client = {0};

/*
static K_SEM_DEFINE(coap_done_sem, 0, 1);
struct firmware_write_context {
    uint32_t version;
    uint32_t high_water_mark;
    uint32_t has_failed;
    uint32_t is_done;
    struct flash_img_context write_ctx;
};

static void on_coap_response(int16_t result_code, size_t offset, const uint8_t *payload, size_t len,
			     bool last_block, void *user_data)
{
    struct firmware_write_context * ctx = (struct firmware_write_context *) user_data;

    LOG_INF("CoAP response, result_code=%d, offset=%u, len=%u is-last=%s", result_code, offset, len, last_block ? "yes" : "no");
    if (result_code != COAP_RESPONSE_CODE_CONTENT) {
		LOG_ERR("Error during CoAP download, result_code=%d", result_code);
        ctx->has_failed = 1;
        k_sem_give(&coap_done_sem);
		return;
    }
    
    if (ctx->has_failed == 1) {
        LOG_ERR("Request has failed, but we have no way to signal this right now. Just early return.");
        return;
    }

    // We need to receive the bytes in order - offset should always be == the high-water mark.
    if (offset != ctx->high_water_mark) {
        LOG_ERR("Invalid offset received (out of order or duplicate?)");
        ctx->has_failed = 1;
        k_sem_give(&coap_done_sem);
        return;
    }

    ctx->high_water_mark = offset + len;

    int err = 0;
    if ((err = flash_img_buffered_write(&ctx->write_ctx, payload, len, last_block)) < 0) {
        LOG_ERR("Failed writing to flash: %d", err);
        ctx->has_failed = 1;
        k_sem_give(&coap_done_sem);
        return;
    } else {
        LOG_INF("Write succeeded for this block, continuing");
    }
	
    if (last_block) {
        LOG_INF("CoAP download done, got %zu bytes ", flash_img_bytes_written(&ctx->write_ctx));
        ctx->is_done = 1;
        k_sem_give(&coap_done_sem);
    }
}

static struct firmware_write_context fw_write = {0};

// Download firmware from a given server using a CoAP request to /fw/hex_version
static void do_firmware_download(struct sockaddr *sa, uint32_t version)
{
	int ret;
	int sockfd;
    char firmware_path[20] = {0};

    snprintf(firmware_path, 19, "/fw/%08x", version);

    int err = 0;
    if ((err = flash_img_init(&fw_write.write_ctx)) < 0) {
        LOG_ERR("Failed to init flash image write: %d", err);
        return;
    }

    fw_write.version = version;

	struct coap_client_request request = {.method = COAP_METHOD_GET,
					      .confirmable = true,
					      .path = firmware_path,
					      .payload = NULL,
					      .len = 0,
					      .cb = on_coap_response,
					      .options = NULL,
					      .num_options = 0,
					      .user_data = (void*) &fw_write};

	LOG_INF("Starting CoAP download using %s", (AF_INET == sa->sa_family) ? "IPv4" : "IPv6");

	sockfd = zsock_socket(sa->sa_family, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		LOG_ERR("Failed to create socket, err %d", errno);
		return;
	}

	ret = coap_client_req(&client, sockfd, sa, &request, NULL);
	if (ret) {
		LOG_ERR("Failed to send CoAP request, err %d", ret);
		return;
	}

	// Wait for CoAP request to complete - we should probably put an upper bound on this and cancel requests afterwards? Does that work the way we think it should? 
	k_sem_take(&coap_done_sem, K_FOREVER);

    if (fw_write.has_failed != 0) {
        LOG_ERR("Firmware write has failed.");
    } else if (fw_write.is_done != 1) {
        LOG_ERR("Semaphore returned but write is not complete?");
    } else {
        LOG_INF("Firmware download is complete, marking partitiion ready...");
        boot_request_upgrade(0); // pending upgrade.
    }

	coap_client_cancel_requests(&client);

	zsock_close(sockfd);
}

*/

static K_SEM_DEFINE(image_done_sem, 0, 1);

const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

#define DISPLAY_WIDTH 800
#define DISPLAY_HEIGHT 480

struct image_write_context {
    uint32_t high_water_mark;
    uint32_t has_failed;
    uint32_t is_done;

    size_t total_produced;
    
    uint16_t y_index;

    uint8_t num_bytes_in_buf;
    uint8_t buf[100];
    heatshrink_decoder hsd;
};

static void write_one_line(uint16_t y, uint8_t *buf, uint16_t num_pix) {
    struct display_buffer_descriptor buf_desc = {
        .buf_size = num_pix,
        .width = num_pix*8,
        .height = 1,
        .pitch = num_pix*8, /* Pitch is the width of the buffer */
    };
    int ret = display_write(display_dev, 0, y, &buf_desc, buf);

    if (ret != 0) {
        LOG_ERR("Failed to write to display: %d", ret);
    }
}

static void img_coap_response(int16_t result_code, size_t offset, const uint8_t *payload, size_t len,
			     bool last_block, void *user_data)
{
    struct image_write_context * ctx = (struct image_write_context *) user_data;

    //LOG_INF("CoAP response, result_code=%d, offset=%u, len=%u is-last=%s", result_code, offset, len, last_block ? "yes" : "no");
    if (result_code != COAP_RESPONSE_CODE_CONTENT) {
		LOG_ERR("Error during CoAP download, result_code=%d", result_code);
        ctx->has_failed = 1;
        k_sem_give(&image_done_sem);
		return;
    }
    
    if (ctx->has_failed == 1) {
        LOG_ERR("Request has failed, but we have no way to signal this right now. Just early return.");
        k_sem_give(&image_done_sem);
        return;
    }

    // We need to receive the bytes in order - offset should always be == the high-water mark.
    if (offset != ctx->high_water_mark) {
        LOG_ERR("Invalid offset received (out of order or duplicate?)");
        ctx->has_failed = 1;
        k_sem_give(&image_done_sem);
        return;
    }

    ctx->high_water_mark = offset + len;

    
    // this needs to be totally re-written at some point... lots of memory waste and grossness here. For now, I want something I can flash
    // to test the hardware :)

    // this feels inefficient - copy into the buffer 100 bytes at a time until we have a full buffer.
    size_t payload_pos = 0;
    HSD_sink_res sres;
    HSD_poll_res pres;
    HSD_finish_res fres;

    while (payload_pos < len) {

        size_t size_in_payload = len - payload_pos;
        size_t actually_read = 0;
        sres = heatshrink_decoder_sink(&ctx->hsd, payload + payload_pos, size_in_payload, &actually_read);
        //LOG_INF("Sunk %zu bytes (sres %d)", actually_read, sres);
        payload_pos+= actually_read;


        do {
            size_t buffer_size_left = 100 - ctx->num_bytes_in_buf;
            size_t did_poll = 0;
            pres = heatshrink_decoder_poll(&ctx->hsd, ctx->buf + ctx->num_bytes_in_buf, buffer_size_left, &did_poll);
            if (pres < 0) { 
                LOG_ERR("pres failed: %d", pres);
                ctx->has_failed = 1;
                k_sem_give(&image_done_sem);
                return;
            }
            //LOG_INF("Polled for %zu bytes", did_poll);
            ctx->num_bytes_in_buf += did_poll;
            ctx->total_produced += did_poll;

            if (ctx->num_bytes_in_buf == 100) {
                //LOG_INF("Writing line to display...");
                //memset(ctx->buf, 0xAA, 100);
                write_one_line(ctx->y_index, ctx->buf, 100);
                ctx->y_index++;
                ctx->num_bytes_in_buf = 0;
            }

        } while (pres == HSDR_POLL_MORE);
    }

    LOG_INF("Total produced: %zu", ctx->total_produced);

    
	    
    if (last_block) {
        fres = heatshrink_decoder_finish(&ctx->hsd);
        if (fres == HSDR_FINISH_MORE) {
            LOG_INF("Got bytes after finish...");
            do {
                size_t buffer_size_left = 100 - ctx->num_bytes_in_buf;
                size_t did_poll = 0;
                pres = heatshrink_decoder_poll(&ctx->hsd, ctx->buf + ctx->num_bytes_in_buf, buffer_size_left, &did_poll);
                if (pres < 0) { 
                    LOG_ERR("pres failed: %d", pres);
                    ctx->has_failed = 1;
                    k_sem_give(&image_done_sem);
                    return;
                }
                //LOG_INF("Polled for %zu bytes", did_poll);
                ctx->num_bytes_in_buf += did_poll;
                ctx->total_produced += did_poll;

                if (ctx->num_bytes_in_buf == 100) {
                    //LOG_INF("Writing line to display...");
                    //memset(ctx->buf, 0xAA, 100);
                    write_one_line(ctx->y_index, ctx->buf, 100);
                    ctx->y_index++;
                    ctx->num_bytes_in_buf = 0;
                }
            } while (pres == HSDR_POLL_MORE);
        } else {
            LOG_INF("Finish result: %d", fres);
        }
        LOG_INF("CoAP img download done, got %u bytes, %u leftover", ctx->total_produced, ctx->num_bytes_in_buf);
        ctx->is_done = 1;
        k_sem_give(&image_done_sem);
    }
}

static struct image_write_context img_write = {0};

static void do_image_download(struct sockaddr *sa)
{
	int ret;
	int sockfd;

    int err = 0;
    
    display_blanking_on(display_dev);
    memset(&img_write, 0, sizeof(struct image_write_context));
    //img_write.version = version;
    heatshrink_decoder_reset(&img_write.hsd);
    

	struct coap_client_request request = {.method = COAP_METHOD_GET,
					      .confirmable = true,
					      .path = "/img",
					      .payload = NULL,
					      .len = 0,
					      .cb = img_coap_response,
					      .options = NULL,
					      .num_options = 0,
					      .user_data = (void*) &img_write};

	LOG_INF("Starting CoAP image download using %s", (AF_INET == sa->sa_family) ? "IPv4" : "IPv6");

	sockfd = zsock_socket(sa->sa_family, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		LOG_ERR("Failed to create socket, err %d", errno);
		return;
	}

	ret = coap_client_req(&client, sockfd, sa, &request, NULL);
	if (ret) {
		LOG_ERR("Failed to send CoAP request, err %d", ret);
		return;
	}

	/* Wait for CoAP request to complete - we should probably put an upper bound on this and cancel requests afterwards? Does that work the way we think it should?  */
	k_sem_take(&image_done_sem, K_FOREVER);

    if (img_write.has_failed != 0) {
        LOG_ERR("Image write has failed.");
    } else if (img_write.is_done != 1) {
        LOG_ERR("Semaphore returned but write is not complete?");
    } else {
        LOG_INF("Image download completed. Un-blanking display...");
        display_blanking_off(display_dev);
    }

	coap_client_cancel_requests(&client);

	zsock_close(sockfd);
}



int main(void)
{
    LOG_INF("Starting app version: %s", APP_VERSION_STRING);

    LOG_INF("Boot swap type: %d", mcuboot_swap_type());

    // in the future, only do this when we've verified server connectivity or something similar.
    if (!boot_is_img_confirmed()) {
        if (boot_write_img_confirmed() != 0) {
            LOG_ERR("Failed to mark image as confirmed!");
        } else {
            LOG_INF("Marked image as OK.");
        }
    }

    /* Get the display device */
    //const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
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

    uint8_t buf[200]; // 1 byte per pixel for MONO01
    memset(buf, 0xAA, 200);
    /*struct display_buffer_descriptor buf_desc = {
        .buf_size = 5,
        .width = BOX_WIDTH,
        .height = 1,
        .pitch = BOX_WIDTH, // Pitch is the width of the buffer 
    };*/

    /*display_blanking_on(display_dev);
    for (uint16_t i = 0; i < 20; i++) {
        memset(buf, i % 2 == 0 ? 0xAA : 0x44, 100);

        write_one_line(i, buf, 100);
    }
    display_blanking_off(display_dev);*/
    

	/* Clear the entire screen to a default color (optional, but good practice) */
    /* For this, we can just write a black buffer to the whole screen */
    /*
    display_blanking_on(display_dev);
    // Calculate the center of the screen
	uint16_t x_center = 0;
    uint16_t y_center = 0;
	for (uint8_t i = 0; i < 10; i++) {
		x_center = (BOX_WIDTH * i);
		y_center = i*5; //x_center;

		LOG_INF("Drawing box at x:%d, y:%d", x_center, y_center);
		// Write the buffer to the display
		int ret = display_write(display_dev, x_center, y_center, &buf_desc, buf);

		if (ret != 0) {
			LOG_ERR("Failed to write to display: %d", ret);
			return 0;
		}

		//k_msleep(250);
	}
	display_blanking_off(display_dev);*/
    

	LOG_INF("Black box drawn successfully in the center of the display.");
    
    
    
  	int ret;

	ret = coap_client_init(&client, NULL);
	if (ret) {
		LOG_ERR("Failed to init coap client, err %d", ret);
	}

    int tried_coap = 0;
    /* The application can now enter a low-power state or do other work */
    while (1) {
        // Are we connected?
        otDeviceRole role = otThreadGetDeviceRole(openthread_get_default_instance());
        if (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER || role == OT_DEVICE_ROLE_LEADER) {

            if (tried_coap == 0) {
                LOG_INF("Trying CoAP!");
                struct sockaddr sa;
                // calculated manually based on "br nat64prefix" from border router, so we don't need to enable ipv4 stack.
                // fd7d:56af:ad45:2:0:0::/96 -> 10.102.40.113 -> fd7d:56af:ad45:2::a66:2871

                struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&sa;

                addr6->sin6_family = AF_INET6;
                addr6->sin6_port = htons(5683);
                zsock_inet_pton(AF_INET6, "fd7d:56af:ad45:1:9fc6:1d58:d2db:2517", &addr6->sin6_addr);

                do_image_download(&sa/*, 0x00000001*/);
                tried_coap = 1;
            }
        } else {
            LOG_INF("Not connected - not attempting CoAP request.");
        }
		
        k_msleep(1000);
    }
    return 0;
}
