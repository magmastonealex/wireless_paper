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

#if DT_NODE_EXISTS(DT_NODELABEL(npm2100_vbat))
#include <zephyr/drivers/sensor/npm2100_vbat.h>
#include <zephyr/dt-bindings/regulator/npm2100.h>
#include <zephyr/drivers/mfd/npm2100.h>
#endif

#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/sensor.h>

#include <zcbor_common.h>
#include <zcbor_encode.h>

#include "coap_request.h"
#include "cbor.h"
#include "wrapped_settings.h"

#include <zephyr/drivers/sensor.h>
#include <zephyr/shell/shell.h>

#include <drivers/generic_epaper.h>

// We want to disable some functionality on devkits (particularly OTA upgrades.)
#ifdef CONFIG_BOARD_NRF54L15DK
    #define IS_DEVKIT 1
#else
    #define IS_DEVKIT 0
#endif

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

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

// technically you can just use settings_write for this, but something is broken when using rtt
// and you can't type more than a couple characters into the shell.
static int epd_cfg_callback(const struct shell* shell, size_t argc, char** argv) {
    if (argc < 2) {
        shell_print(shell, "must specify subcmd g or s");
        return 1;
    }
    int ret = 0;
    if (argv[1][0] == 'g') {
        uint8_t expected_type = 0;
        ret = wrapped_settings_get_raw("ep_type", &expected_type, 1, NULL);
        if (ret < 0) {
            shell_print(shell, "failed to read ep type: %d", ret);
        } else {
            shell_print(shell, "got ep type: %u", expected_type);
        }
    }
    else if (argv[1][0] == 's') {
        if (argc < 3) {
            shell_print(shell, "must specify epd type");
            return 1;
        }
        uint8_t argument = argv[2][0];
        if (argument > 57 || argument < 48) {
            shell_print(shell, "arg must be 0-9 %u", argument);
            return 1;
        }

        uint8_t expected_type = argument - 48;
        ret = wrapped_settings_set_raw("ep_type", &expected_type, 1);
        if (ret < 0) {
            shell_print(shell, "failed to write ep type: %d", ret);
        } else {
            shell_print(shell, "wrote ep type: %u", expected_type);
        }
    } else if (argv[1][0] == 'o') {
        set_ot_data();
    } else {
        shell_print(shell, "must specify subcmd g or s was something else %c %zu", argv[1][0], argc);
        return 1;
    }
    return 0;
}

SHELL_CMD_REGISTER(epc, NULL, "epaper config settings", epd_cfg_callback);


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



static struct coap_client client = {0};

static int fw_coap_response(const uint8_t *payload, size_t len, size_t offset, bool last_block, void *user_data) {
    struct flash_img_context *write_ctx = (struct flash_img_context *) user_data;

    int err = 0;
    if ((err = flash_img_buffered_write(write_ctx, payload, len, last_block)) < 0) {
        LOG_ERR("Failed writing to flash: %d", err);
        return -1;
    } else {
        LOG_INF("Write succeeded for this block (pos %zu), continuing", offset + len);
    }
    return 0;
}

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
    struct device *eink_dev;
    size_t max_data;
    size_t total_produced;
    
    heatshrink_decoder hsd;
};

static int img_coap_response(const uint8_t *payload, size_t len, size_t offset, bool last_block, void *user_data)
{
    struct image_write_context * ctx = (struct image_write_context *) user_data;
    size_t payload_pos = 0;
    HSD_sink_res sres;
    HSD_poll_res pres;
    HSD_finish_res fres;

    uint8_t temp_buffer[100];

    while (payload_pos < len) {
        size_t size_in_payload = len - payload_pos;
        size_t actually_read = 0;
        sres = heatshrink_decoder_sink(&ctx->hsd, payload + payload_pos, size_in_payload, &actually_read);
        payload_pos+= actually_read;
        do {
            size_t did_poll = 0;
            pres = heatshrink_decoder_poll(&ctx->hsd, temp_buffer, sizeof(temp_buffer), &did_poll);
            if (pres < 0) { 
                LOG_ERR("pres1 failed: %d", pres);
                return -1;
            }
            //LOG_INF("Polled for %zu bytes", did_poll);
            ctx->total_produced += did_poll;
            if (ctx->total_produced > ctx->max_data) {
                LOG_ERR("would overrun: %zu received", ctx->total_produced);
                return -1;
            }

            if (did_poll > 0) {
                int epd_res = epd_continue_write_data(ctx->eink_dev, temp_buffer, did_poll);
                if (epd_res < 0) {
                    LOG_ERR("Failed write to display: %d", epd_res);
                    return -1;
                }
            }
        } while (pres == HSDR_POLL_MORE);
    }

    LOG_INF("Total produced: %zu", ctx->total_produced);
	    
    if (last_block) {
        fres = heatshrink_decoder_finish(&ctx->hsd);
        if (fres == HSDR_FINISH_MORE) {
            LOG_INF("Got bytes after finish...");
            do {
                size_t did_poll = 0;
                pres = heatshrink_decoder_poll(&ctx->hsd, temp_buffer, sizeof(temp_buffer), &did_poll);
                if (pres < 0) { 
                    LOG_ERR("pres failed: %d", pres);
                    return -1;
                }
                LOG_INF("finish polled for %zu bytes", did_poll);
                ctx->total_produced += did_poll;
                if (ctx->total_produced > ctx->max_data) {
                    LOG_ERR("would overrun: %zu received", ctx->total_produced);
                    return -1;
                }
                if (did_poll > 0) {
                    int epd_res = epd_continue_write_data(ctx->eink_dev, temp_buffer, did_poll);
                    if (epd_res < 0) {
                        LOG_ERR("Failed write to display: %d", epd_res);
                        return -1;
                    }
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
#if DT_NODE_EXISTS(DT_NODELABEL(npm2100_vbat))
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
#else
    LOG_INF("Using fake vbat measurement.");
    return 1900;
#endif
}

static struct image_write_context img_write = {0};


#if DT_NODE_EXISTS(DT_NODELABEL(npm2100_pmic))
static const struct device *npm2100_pmic = DEVICE_DT_GET(DT_NODELABEL(npm2100_pmic));
#endif


// Devkit doesn't have separate EN pin - rst is multiplexed by the breakout board.
#if DT_HAS_ALIAS(heartbeat_led)
static const struct gpio_dt_spec green_led = GPIO_DT_SPEC_GET(DT_ALIAS(heartbeat_led), gpios);
#endif

int main(void)
{
    LOG_INF("Starting app version: %s", APP_VERSION_STRING);
    LOG_INF("Boot swap type: %d", mcuboot_swap_type());
    // Set 3v3 for regulator...
    //int vset_res = regulator_set_voltage(boost, 3300000, 3300000);
    //if (vset_res != 0) {
    //    LOG_ERR("Failed to vset: %d", vset_res);
    //}

#if DT_HAS_ALIAS(heartbeat_led)
    gpio_pin_configure_dt(&green_led, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set_dt(&green_led, 1);
#endif

    /* Get the display device */
    const struct device *eink_dev = DEVICE_DT_GET(DT_NODELABEL(eink));

    if (!device_is_ready(eink_dev)) {
        LOG_ERR("Display device not ready.");
        return 0;
    }

    int ret;

    ret = wrapped_settings_init();
    if (ret < 0) {
            LOG_ERR("failed to initialize settings...: %d", ret);
            return 0;
    }

    // e-paper will not be written to if the type is invalid or we can't get dimensions.
    // This prevents us from bricking a display by writing bad data to it.
    uint8_t ep_disabled = 0;

    uint8_t expected_type = 0;
    ret = wrapped_settings_get_raw("ep_type", &expected_type, 1, NULL);
    if (ret < 0) {
            LOG_ERR("failed to read ep type setting, disabling epd: %d", ret);
            ep_disabled = 1;
    }
    LOG_INF("Got epaper type: %u", expected_type);

    ret = epd_set_type(eink_dev, (epd_type_t) expected_type);
    if (ret < 0) {
            LOG_ERR("failed to set type of display: %d", ret);
            ep_disabled = 1;
    }

    struct epd_dimensions eink_dimensions;
    ret = epd_get_dimensions(eink_dev, &eink_dimensions);
    if (ret < 0) {
        LOG_ERR("failed to get dimensions of display: %d", ret);
        ep_disabled = 1;
    }

    openthread_state_changed_callback_register(&ot_state_chaged_cb);
    //set_ot_data();
    LOG_INF("Starting OpenThread!");
    openthread_run();


	ret = coap_client_init(&client, NULL);
	if (ret) {
		LOG_ERR("Failed to init coap client, err %d", ret);
	}

    uint64_t device_id_mac = get_deviceaddr_mac();

    #if DT_NODE_EXISTS(DT_NODELABEL(npm2100_vbat))
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
    #endif

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
        req_encoded_size = 0;
    }

    uint8_t res_encoded[100];
    struct buffer_write_context bufwrite = {
        .buf = res_encoded,
        .max_size = 100,
        .current_size = 0
    };

    // default to wake every 5 minutes if not otherwise commanded.
    uint32_t sleep_for_seconds = 600;

    int connection_waits = 0;

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
                        if (!boot_is_img_confirmed()) {
                            if (boot_write_img_confirmed() != 0) {
                                LOG_ERR("Failed to mark image as confirmed!");
                            } else {
                                LOG_INF("Marked image as OK.");
                            }
                        }

                        LOG_INF("Decoded heartbeat. Desired firmware version: %08x, sleep interval %u", hb_resp.desired_firmware, hb_resp.checkin_interval);
                        
                        if (hb_resp.desired_firmware != APPVERSION && (IS_DEVKIT == 0)) {
                            LOG_WRN("Starting firmware upgrade: %08x -> %08x", APPVERSION, hb_resp.desired_firmware);

                            struct flash_img_context write_ctx;
                            if ((ret = flash_img_init(&write_ctx)) < 0) {
                                LOG_ERR("Failed to init flash image write: %d", ret);
                            }

                            char firmware_path[30] = {0};

                            snprintf(firmware_path, 29, "fw/%08x.bin", hb_resp.desired_firmware);

                            res = do_coap_request(&client, &sa, firmware_path, COAP_METHOD_GET, req_encoded, req_encoded_size, fw_coap_response, (void*) &write_ctx, 120);

                            if (res == 0) {
                                LOG_INF("Firmware upgrade downloaded. Kicking off upgrade....");
                                boot_request_upgrade(0);
                                // by using the npm2100 reset here, we'll set a 10 second wdt
                                // for zephyr to start up again, which should be plenty of time if the image is correct.
                                #if DT_NODE_EXISTS(DT_NODELABEL(npm2100_pmic))
                                mfd_npm2100_reset(npm2100_pmic);
                                #else
                                LOG_INF("no PMIC - reset board manually");
                                #endif
                            }
                        } else {
                            #if IS_DEVKIT == 0
                            LOG_INF("Firmware up to date, no action.");
                            #else
                            LOG_INF("Is devkit, ignoring potential firmware upgrade.");
                            #endif
                        }

                        sleep_for_seconds = hb_resp.checkin_interval;
                    } else {
                        LOG_INF("Failed to decode heartbeat: %d", res);
                    }
                }

                if (ep_disabled == 0) {
                    // Then fetch an updated image
                    memset(&img_write, 0, sizeof(struct image_write_context));
                    heatshrink_decoder_reset(&img_write.hsd);
                    img_write.eink_dev = eink_dev;
                    img_write.max_data = eink_dimensions.expected_data_size;

                    struct image_request img_req = {
                        .device_id = device_id_mac,
                        .epd_type = (uint8_t) EPD_TYPE_WS_75_V2B,
                        .expected_data_size = eink_dimensions.expected_data_size
                    };

                    ret = encode_image_request(&img_req, req_encoded, sizeof(req_encoded), &req_encoded_size);
                    if (ret != 0) {
                        LOG_ERR("failed to encode heartbeat: %d", ret);
                    }

                    res = epd_power_on(eink_dev);
                    if (res < 0) {
                            LOG_ERR("failed to power on display: %d", res);
                            goto hibernate;        
                    }
                    res = epd_start_write_data(eink_dev, 0);
                    if (res < 0) {
                            LOG_ERR("failed to init write: %d", res);
                            goto hibernate;
                    }
                    
                    res = do_coap_request(&client, &sa, "img", COAP_METHOD_GET, req_encoded, req_encoded_size, img_coap_response, (void*) &img_write, 90);
                    LOG_INF("return code: %d", res);
                    res = epd_do_refresh(eink_dev);
                    if (res < 0) {
                            LOG_ERR("failed to finish writing display: %d", res);
                    }
                    k_msleep(1000);
                    LOG_ERR("Done refresh?");
                    res = epd_power_off(eink_dev);
                    if (res < 0) {
                            LOG_ERR("failed to power off display: %d", res);
                    }
                } else {
                    LOG_ERR("epd disabled (bad settings?), did not attempt a write.");
                }

                tried_coap = 1;

                hibernate:
                LOG_INF("About to hibernate for %d seconds", sleep_for_seconds);
                k_msleep(200);
                #if DT_NODE_EXISTS(DT_NODELABEL(npm2100_pmic))
                //int hibres = mfd_npm2100_hibernate(npm2100_pmic, sleep_for_seconds * 1000, false);
                k_sleep(K_SECONDS(sleep_for_seconds));
                #else
                LOG_INF("No PMIC - sleeping instead. You probably want to reset the board.");
                k_sleep(K_SECONDS(sleep_for_seconds));
                #endif
                //LOG_INF("hibres: %d", hibres);
            }
        } else {
            connection_waits++;
            if (connection_waits > 60) {
                LOG_INF("No connection after 1 minute. Sleeping for a while...");
                k_msleep(200);
                #if DT_NODE_EXISTS(DT_NODELABEL(npm2100_pmic))
                mfd_npm2100_hibernate(npm2100_pmic, sleep_for_seconds * 1000, false);
                #else
                LOG_INF("No PMIC - sleeping manually.");
                k_sleep(K_SECONDS(sleep_for_seconds));
                connection_waits = 0;
                #endif
            }
        }

    #if DT_HAS_ALIAS(heartbeat_led)
        gpio_pin_toggle_dt(&green_led);
    #endif
		
        k_msleep(1000);
    }
    return 0;
}
