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

#include <zephyr/drivers/hwinfo.h>

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
#include <zephyr/drivers/gpio.h>

#include "heatshrink/heatshrink_decoder.h"

#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/dt-bindings/regulator/npm2100.h>
#include <zephyr/drivers/mfd/npm2100.h>

#include <zcbor_common.h>
#include <zcbor_encode.h>

#include "coap_request.h"
#include "cbor.h"

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm2100_vbat.h>


LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* Define the size of the box */
#define BOX_WIDTH  40
#define BOX_HEIGHT 40

static const struct gpio_dt_spec epd_en = GPIO_DT_SPEC_GET(DT_ALIAS(epd_en), gpios);

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

#define DISPLAY_WIDTH 800
#define DISPLAY_HEIGHT 480

struct buffer_write_context {
    uint8_t *buf;
    size_t max_size;
    size_t current_size;
};

static int buffer_coap_response(const uint8_t *payload, size_t len, size_t offset, bool last_block, void *user_data) {
    struct buffer_write_context * ctx = (struct buffer_write_context *) user_data;

    size_t free_space = ctx->max_size - ctx->current_size;
    size_t to_copy = MIN(len, free_space);

    LOG_INF("Inserting %zu bytes to buffer", to_copy);

    memcpy(ctx->buf + ctx->current_size, payload, to_copy);
    ctx->current_size += to_copy;

    return 0;
}

struct image_write_context {
    struct device *display_dev;
    size_t total_produced;
    
    uint16_t y_index;

    uint8_t num_bytes_in_buf;
    uint8_t buf[100];
    heatshrink_decoder hsd;
};

static void write_one_line(struct device *display_dev, uint16_t y, uint8_t *buf, uint16_t num_pix) {
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

static int img_coap_response(const uint8_t *payload, size_t len, size_t offset, bool last_block, void *user_data)
{
    struct image_write_context * ctx = (struct image_write_context *) user_data;
    size_t payload_pos = 0;
    HSD_sink_res sres;
    HSD_poll_res pres;
    HSD_finish_res fres;

    while (payload_pos < len) {
        size_t size_in_payload = len - payload_pos;
        size_t actually_read = 0;
        sres = heatshrink_decoder_sink(&ctx->hsd, payload + payload_pos, size_in_payload, &actually_read);
        payload_pos+= actually_read;


        do {
            size_t buffer_size_left = 100 - ctx->num_bytes_in_buf;
            size_t did_poll = 0;
            pres = heatshrink_decoder_poll(&ctx->hsd, ctx->buf + ctx->num_bytes_in_buf, buffer_size_left, &did_poll);
            if (pres < 0) { 
                LOG_ERR("pres1 failed: %d", pres);
                return -1;
            }
            //LOG_INF("Polled for %zu bytes", did_poll);
            ctx->num_bytes_in_buf += did_poll;
            ctx->total_produced += did_poll;

            if (ctx->num_bytes_in_buf == 100) {
                //LOG_INF("Writing line to display...");
                //memset(ctx->buf, 0xAA, 100);
                write_one_line(ctx->display_dev, ctx->y_index, ctx->buf, 100);
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
                    return -1;
                }
                //LOG_INF("Polled for %zu bytes", did_poll);
                ctx->num_bytes_in_buf += did_poll;
                ctx->total_produced += did_poll;

                if (ctx->num_bytes_in_buf == 100) {
                    //LOG_INF("Writing line to display...");
                    //memset(ctx->buf, 0xAA, 100);
                    write_one_line(ctx->display_dev, ctx->y_index, ctx->buf, 100);
                    ctx->y_index++;
                    ctx->num_bytes_in_buf = 0;
                }
            } while (pres == HSDR_POLL_MORE);
        } else {
            LOG_INF("Finish result: %d", fres);
        }
    }

    return 0;
}

uint64_t get_deviceaddr_mac() {

    uint64_t device_id_mac = 0;

#ifndef USE_DEVADDR_AS_DEVICE_ID
    int ret = hwinfo_get_device_id((uint8_t*)&device_id_mac, 8);
    if (ret < 0) {
        LOG_ERR("failed to get device ID: %d", ret);
    }
    // only use lower 32 bits for now. web UI and db don't like full 64 bit ints.
    device_id_mac = device_id_mac & 0x00000000FFFFFFFF;
#else
    // believe it or not, this is nordic's suggested approach to read this without the BT stack.
    // https://devzone.nordicsemi.com/f/nordic-q-a/102285/read-nrf_ficr--deviceaddr-in-zephyr
    
    uint32_t * addr_upper = (uint32_t*) 0x00FFC3A4;
    uint16_t * addr_lower = (uint16_t*) 0x00FFC3A8;

    device_id_mac |= ((uint64_t)(*addr_upper)) << 48 | ((uint64_t)addr_lower);

#endif

    return device_id_mac;
}

int32_t get_vbat_mV() {
    const struct device *npm2100_vbat = DEVICE_DT_GET(DT_NODELABEL(npm2100_vbat));
    if (!device_is_ready(npm2100_vbat)) {
        LOG_ERR("vbat device not ready.");
        return -1;
    }

    struct sensor_value adc_delay = {0};
    sensor_attr_set(npm2100_vbat, SENSOR_CHAN_GAUGE_VOLTAGE, SENSOR_ATTR_NPM2100_ADC_DELAY, &adc_delay);

    int ret = sensor_sample_fetch_chan(npm2100_vbat, SENSOR_CHAN_GAUGE_VOLTAGE);
    if (ret < 0) {
        LOG_ERR("failed to sample vbat: %d", ret);
        return -1;
    }

    struct sensor_value vbat_res;

    ret = sensor_channel_get(npm2100_vbat, SENSOR_CHAN_GAUGE_VOLTAGE, &vbat_res);
    if (ret < 0) {
        LOG_ERR("failed to get vbat: %d", ret);
        return -1;
    }

    int32_t vbat_mv = (int32_t) sensor_value_to_milli(&vbat_res);

    return vbat_mv;
}

static struct image_write_context img_write = {0};


static const struct gpio_dt_spec green_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static const struct device *boost = DEVICE_DT_GET(DT_NODELABEL(npm2100_boost));
static const struct device *npm2100_pmic = DEVICE_DT_GET(DT_NODELABEL(npm2100_pmic));

int main(void)
{
    LOG_INF("Starting app version: %s", APP_VERSION_STRING);
    LOG_INF("Boot swap type: %d", mcuboot_swap_type());

    // Set 3v3 for regulator...
    //int vset_res = regulator_set_voltage(boost, 3300000, 3300000);
    //if (vset_res != 0) {
    //    LOG_ERR("Failed to vset: %d", vset_res);
    //}


    gpio_pin_configure_dt(&epd_en, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set_dt(&epd_en, 1);

    gpio_pin_configure_dt(&green_led, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set_dt(&green_led, 1);

    // in the future, only do this when we've verified server connectivity or something similar.
    if (!boot_is_img_confirmed()) {
        if (boot_write_img_confirmed() != 0) {
            LOG_ERR("Failed to mark image as confirmed!");
        } else {
            LOG_INF("Marked image as OK.");
        }
    }

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

    openthread_state_changed_callback_register(&ot_state_chaged_cb);
    //set_ot_data();
    LOG_INF("Starting OpenThread!");
    openthread_run();

    

	LOG_INF("Black box drawn successfully in the center of the display.");
    
  	int ret;

	ret = coap_client_init(&client, NULL);
	if (ret) {
		LOG_ERR("Failed to init coap client, err %d", ret);
	}

    uint64_t device_id_mac = get_deviceaddr_mac();
    
    const struct device *npm2100_vbat = DEVICE_DT_GET(DT_NODELABEL(npm2100_vbat));
    if (!device_is_ready(npm2100_vbat)) {
        LOG_ERR("vbat device not ready.");
        return -1;
    }

    struct sensor_value adc_delay = {0};
    ret = sensor_attr_set(npm2100_vbat, SENSOR_CHAN_GAUGE_VOLTAGE, SENSOR_ATTR_NPM2100_ADC_DELAY, &adc_delay);
    if (ret != 0) {
        LOG_ERR("failed to set ADC delay for vbat: %d", ret);
    }

    int32_t vbat_mv = get_vbat_mV();

    struct device_heartbeat_request req = {
        .device_id = device_id_mac, // Note: device_id realistically should be u32. 
        .current_firmware = APPVERSION,
        .protocol_version = 1,
        .vbat_mv = vbat_mv
    };

    uint8_t req_encoded[100];
    size_t req_encoded_size = 0;
    ret = encode_heartbeat_request(&req, req_encoded, sizeof(req_encoded), &req_encoded_size);
    if (ret != 0) {
        LOG_ERR("failed to encode heartbeat: %d", ret);
    }

    uint8_t res_encoded[100];
    struct buffer_write_context bufwrite = {
        .buf = res_encoded,
        .max_size = 100,
        .current_size = 0
    };


    int tried_coap = 0;
    /* The application can now enter a low-power state or do other work */
    while (1) {
        // Are we connected?
        otDeviceRole role = otThreadGetDeviceRole(openthread_get_default_instance());
        if (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER || role == OT_DEVICE_ROLE_LEADER) {

            if (tried_coap == 0) {
                LOG_INF("Trying CoAP. vbat: %d! %llu", vbat_mv, device_id_mac);
                struct sockaddr sa;
                // calculated manually based on "br nat64prefix" from border router, so we don't need to enable ipv4 stack.
                // fd7d:56af:ad45:2:0:0::/96 -> 10.102.40.113 -> fd7d:56af:ad45:2::a66:2871

                struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&sa;

                addr6->sin6_family = AF_INET6;
                addr6->sin6_port = htons(5683);
                zsock_inet_pton(AF_INET6, "fd7d:56af:ad45:1:9fc6:1d58:d2db:2517", &addr6->sin6_addr);

                
                // Do our heartbeat first.

                coap_request_result_t  res = do_coap_request(&client, &sa, "hb", COAP_METHOD_PUT, req_encoded, req_encoded_size, buffer_coap_response, (void*) &bufwrite, 10);
                LOG_INF("HB return code: %d", res);
                if (res == 0) {
                    LOG_INF("Got %zu bytes from HB", bufwrite.current_size);
                    struct device_heartbeat_response hb_resp;
                    res = decode_heartbeat_response(res_encoded, bufwrite.current_size, &hb_resp);
                    if (res == 0) {
                        LOG_INF("Decoded heartbeat. Desired firmware version: %08x, sleep interval %u", hb_resp.desired_firmware, hb_resp.checkin_interval);
                        // We could upgrade firmware here if we wanted to.
                    } else {
                        LOG_INF("Failed to decode heartbeat: %d", res);
                    }
                }

                // Then fetch an updated image
                memset(&img_write, 0, sizeof(struct image_write_context));
                heatshrink_decoder_reset(&img_write.hsd);
                img_write.display_dev = display_dev;
                display_blanking_on(display_dev);
                res = do_coap_request(&client, &sa, "img", COAP_METHOD_GET, NULL, 0, img_coap_response, (void*) &img_write, 90);
                display_blanking_off(display_dev);
                gpio_pin_set_dt(&epd_en, 0); // avoid a voltage spike when boost turns off by using pmos as a diode

                LOG_INF("return code: %d", res);

                tried_coap = 1;

                LOG_INF("About to hibernate...");
                k_msleep(5000);
                //int hibres = mfd_npm2100_hibernate(npm2100_pmic, 60000, false);
                //LOG_INF("hibres: %d", hibres);
            }
        } else {
            LOG_INF("Not connected - not attempting CoAP request.");
        }

        gpio_pin_toggle_dt(&green_led);
		
        k_msleep(1000);
    }
    return 0;
}
