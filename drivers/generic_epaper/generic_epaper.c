#define DT_DRV_COMPAT ar_generic_epaper

#include <zephyr/kernel.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/spi.h>

#include <drivers/generic_epaper.h>

LOG_MODULE_REGISTER(generic_epaper, LOG_LEVEL_DBG);

// "special" length values that will trigger particular behaviour.
// DO_RESET -> Triggers a hardware reset of the panel.
// WAIT_FOR_BUSY -> Wait until the BUSY line goes inactive.
#define DO_RESET 0xF0
#define WAIT_FOR_BUSY 0xF1
#define WAIT_100ms 0xF2
#define DONE 0x0


struct epd_metadata {
    uint16_t height;
    uint16_t width;

    size_t expected_data_size;

    uint8_t *init_command_list;
    uint8_t num_planes;
    uint8_t *powerdown_command_list;
    uint8_t *refresh_command_list;

    uint8_t data_transmission_command[];
};



// 0x21 to set size of display???
// 0x00, 0x40??
// 0x44, 0x40 to blank screen??

/*static const uint8_t GDEY029T71H_init_full[] = {
    DO_RESET,
    0x01, 0x12, // SWRESET
    WAIT_FOR_BUSY,
    0x02, 0x3C, 0x01, // Border waveform
    //0x03, 0x21, 0x00, 0x80,
    0x4, 0x01, (uint8_t)((GDEY029T71H_height-1) % 256), (uint8_t)((GDEY029T71H_height-1) / 256), 0x00,
    0x2, 0x11, 0x01,
    0x3, 0x44, 0x00, (uint8_t)(GDEY029T71H_width / 8 - 1),
    0x5, 0x45, (uint8_t)((GDEY029T71H_height-1) % 256), (uint8_t)((GDEY029T71H_height-1) / 256), 0x00, 0x00,
    0x2, 0x3C, 0x05,
    0x2, 0x18, 0x80,
    0x2, 0x4E, 0x00,
    0x3, 0x4F, (uint8_t)((GDEY029T71H_height-1) % 256), (uint8_t)((GDEY029T71H_height-1) / 256),
    WAIT_FOR_BUSY,
    DONE
};*/

#define GDEY029T71H_height 384
#define GDEY029T71H_width 168
static const uint8_t GDEY029T71H_init_full[] = {
    DO_RESET,
    0x01, 0x12, // SWRESET
    WAIT_FOR_BUSY,
    0x02, 0x3C, 0x01, // Border waveform
    //0x03, 0x21, 0x00, 0x80,
    0x4, 0x01, (uint8_t)((GDEY029T71H_height-1) % 256), (uint8_t)((GDEY029T71H_height-1) / 256), 0x00,
    0x2, 0x3C, 0x05,
    0x2, 0x18, 0x80,
    0x2, 0x21, 0x00,0x00,
    //0x2, 0x11, 0x03,
    //0x3, 0x44, 0x01, 21,
    //0x5, 0x45, 0, 0, 127, 1,
    //0x2, 0x4e, 1,
    //0x3, 0x4f, 0, 0,
    WAIT_FOR_BUSY,
    DONE
};
/**

x = 8
y = 0
w = 168
h = 384

#if 1 // normal, top opposite connection
  _writeCommand(0x11); // set ram entry mode
  _writeData(0x03);    // x increase, y increase : normal mode
  _writeCommand(0x44);
  _writeData(x / 8);
  _writeData((x + w - 1) / 8);
  _writeCommand(0x45);
  _writeData(y % 256);
  _writeData(y / 256);
  _writeData((y + h - 1) % 256);
  _writeData((y + h - 1) / 256);
  _writeCommand(0x4e);
  _writeData(x / 8);
  _writeCommand(0x4f);
  _writeData(y % 256);
  _writeData(y / 256);
*/

/*static const uint8_t GDEY029T71H_init_full[] = {
    DO_RESET,
    0x01, 0x12, // SWRESET
    WAIT_FOR_BUSY,
    0x2, 0x18, 0x80,
    0x2, 0x22, 0xB1,
    0x1, 0x20,
    WAIT_FOR_BUSY,
    0x3, 0x1A, 0x6E, 0x00,
    0x2, 0x22, 0x91,
    0x1, 0x20,
    WAIT_FOR_BUSY,
    DONE
};*/



static const uint8_t GDEY029T71H_refresh[] = {
    0x03, 0x21, 0x00, 0x00, // setting this to 0x44 instead of 0x40 gives black - overriding B/W ram reads with all zeros. Now it's just a puzzle of writing to RAM properly.
    0x02, 0x22, 0xF7,
    0x01, 0x20,
    WAIT_100ms,
    WAIT_FOR_BUSY,
    DONE
};


static const uint8_t GDEY029T71H_power_down[] = {
    0x02, 0x10, 0x01, // Deep sleep
    DONE
};

static const struct epd_metadata GDEY029T71H_meta = {
    .height = 168,
    .width = 384,
    .expected_data_size = 8064,

    .init_command_list = GDEY029T71H_init_full,
    .num_planes = 2,
    .powerdown_command_list = GDEY029T71H_power_down,
    .refresh_command_list = GDEY029T71H_refresh,
    .data_transmission_command = {0x24, 0x26}
};

#define GDEM035F51_Source_BITS 184
#define GDEM035F51_Gate_BITS 384

static const uint8_t GDEM035F51_init_full[] = {
    DO_RESET,
    WAIT_FOR_BUSY,

    0x7, 0x66, 0x49, 0x55, 0x13, 0x5D, 0x05, 0x10,
    0x2, 0x4D, 0x78,
    0x3, 0x00, 0x0F, 0x29,
    0x3, 0x01, 0x07, 0x00,
    0x4,  0x03, 0x10, 0x54, 0x44,
    0x8, 0x06, 0x0F, 0x0A, 0x2F, 0x25, 0x22, 0x2E, 0x21,
    0x2, 0x50, 0x37,
    0x3, 0x60, 0x02, 0x02,
    0x5, 0x61, GDEM035F51_Source_BITS/256, GDEM035F51_Source_BITS%256, GDEM035F51_Gate_BITS/256, GDEM035F51_Gate_BITS%256,
    0x2, 0xE7, 0x1C,
    0x2, 0xE3, 0x22,
    0x2, 0xB6, 0x6F,
    0x2, 0xB4, 0xD0,
    0x2, 0xE9, 0x01,
    0x2, 0x30, 0x08,
    0x1, 0x04,
    WAIT_FOR_BUSY,
    DONE
};
static const uint8_t GDEM035F51_refresh[] = {
    0x02, 0x12, 0x00,
    WAIT_FOR_BUSY,
    DONE
};
static const uint8_t GDEM035F51_power_down[] = {
    0x2, 0x02, 0x00,
    WAIT_FOR_BUSY,
    0x2, 0x07, 0xA5,
    DONE
};

static const struct epd_metadata GDEM035F51_meta = {
    .height = 184,
    .width = 384,
    .expected_data_size = 8064,

    .init_command_list = GDEM035F51_init_full,
    .num_planes = 1,
    .powerdown_command_list = GDEM035F51_power_down,
    .refresh_command_list = GDEM035F51_refresh,
    .data_transmission_command = {0x10}
};



#define GDEY029F51_Source_BITS 168
#define GDEY029F51_Gate_BITS 384

static const uint8_t GDEY029F51_init_full[] = {
    DO_RESET,
    WAIT_100ms,
    WAIT_FOR_BUSY,
    0x2, 0x4D, 0x78,
    0x3, 0x00, 0x0F, 0x29,
    0x3, 0x01, 0x07, 0x00,
    0x4, 0x03, 0x10, 0x54, 0x44,
    0x8, 0x06, 0x05, 0x00, 0x3F, 0x0A, 0x25, 0x12, 0x1A,

    0x2, 0x50, 0x37,
    0x3, 0x60, 0x02, 0x02,
    0x5, 0x61, GDEY029F51_Source_BITS/256, GDEY029F51_Source_BITS%256, GDEY029F51_Gate_BITS/256, GDEY029F51_Gate_BITS%256,
    0x2, 0xE7, 0x1C,
    0x2, 0xE3, 0x22,
    0x2, 0xB4, 0xD0,
    0x2, 0xB5, 0x03,
    0x2, 0xE9, 0x01,
    0x2, 0x30, 0x08,
    0x1, 0x04,
    WAIT_FOR_BUSY,
    DONE
};
static const uint8_t GDEY029F51_refresh[] = {
    0x2, 0x12, 0x00,
    WAIT_100ms,
    WAIT_FOR_BUSY,
    DONE
};
static const uint8_t GDEY029F51_power_down[] = {
    0x1, 0x02,
    WAIT_FOR_BUSY,
    WAIT_100ms,
    0x2, 0x07, 0xA5,
    DONE
};

static const struct epd_metadata GDEY029F51_meta = {
    .height = 168,
    .width = 384,
    .expected_data_size = 16128,

    .init_command_list = GDEY029F51_init_full,
    .num_planes = 1,
    .powerdown_command_list = GDEY029F51_power_down,
    .refresh_command_list = GDEY029F51_refresh,
    .data_transmission_command = {0x10}
};

#define GDEM075F52_Source_BITS 800
#define GDEM075F52_Gate_BITS 480

static const uint8_t GDEM075F52_init_full[] = {
    DO_RESET,
    WAIT_100ms,
    WAIT_FOR_BUSY,

    0x3, 0x00, 0x0F, 0x29,
    0x5, 0x06, 0x0F, 0x8B, 0x93, 0xA1,
    0x2, 0x41, 0x00,
    0x2, 0x50, 0x37,
    0x3, 0x60, 0x02, 0x02,
    0x5, 0x61, GDEM075F52_Source_BITS/256, GDEM075F52_Source_BITS%256, GDEM075F52_Gate_BITS/256, GDEM075F52_Gate_BITS%256,
    0x9, 0x62, 0x98, 0x98, 0x98, 0x75, 0xCA, 0xB2, 0x98, 0x7E,
    0x5, 0x65, 0x00, 0x00, 0x00, 0x00,
    0x2, 0xE7, 0x1C,
    0x2, 0xE3, 0x00,
    0x2, 0xE9, 0x01,
    0x2, 0x30, 0x08,
    0x1, 0x04,
    WAIT_FOR_BUSY,
    0x2, 0xE0, 0x02,
    0x2, 0xE6, 0x5A,
    0x2, 0xA5, 0x00,
    WAIT_FOR_BUSY,
    DONE
};
static const uint8_t GDEM075F52_refresh[] = {
    0x2, 0x12, 0x00,
    WAIT_100ms,
    WAIT_FOR_BUSY,
    DONE
};
static const uint8_t GDEM075F52_power_down[] = {
    0x2, 0x02, 0x00,
    WAIT_FOR_BUSY,
    WAIT_100ms,
    0x2, 0x07, 0xA5,
    DONE
};

static const struct epd_metadata GDEM075F52_meta = {
    .height = 480,
    .width = 800,
    .expected_data_size = 96000,

    .init_command_list = GDEM075F52_init_full,
    .num_planes = 1,
    .powerdown_command_list = GDEM075F52_power_down,
    .refresh_command_list = GDEM075F52_refresh,
    .data_transmission_command = {0x10}
};


static const uint8_t WS_75_V2B_init_full[] = {
    DO_RESET,
    WAIT_100ms,
    DO_RESET,
    WAIT_100ms,
    // from waveshare b/w?
    0x6, 0x01, 0x17,0x17, 0x3F, 0x3F, 0x11,
    0x2, 0x82, 0x24,
    0x5, 0x06, 0x27, 0x27, 0x2F, 0x17,
    0x2, 0x30, 0x06,
    0x1, 0x04,
    0x1, 0x71,
    WAIT_FOR_BUSY,
    0x2, 0x00, 0x1F,
    0x5, 0x61, 0x03, 0x20, 0x01, 0xE0,
    0x2, 0x15, 0x00,
    0x3, 0x50, 0x10, 0x00,
    0x2, 0x60, 0x22,
    0x5, 0x65, 0x00, 0x00, 0x00, 0x00,
    DONE
};
static const uint8_t WS_75_V2B_refresh[] = {
    0x1, 0x12,
    WAIT_100ms,
    WAIT_FOR_BUSY,
    DONE
};
static const uint8_t WS_75_V2B_power_down[] = {
    0x1, 0x02,
    WAIT_FOR_BUSY,
    WAIT_100ms,
    0x2, 0x07, 0xA5,
    DONE
};

static const struct epd_metadata WS_75_V2B_meta = {
    .height = 480,
    .width = 800,
    .expected_data_size = 48000,

    .init_command_list = WS_75_V2B_init_full,
    .num_planes = 1,
    .powerdown_command_list = WS_75_V2B_power_down,
    .refresh_command_list = WS_75_V2B_refresh,
    .data_transmission_command = {/*0x10,*/ 0x13}
};


struct epd_data {
	struct epd_metadata *meta;
};

struct epd_config {
    struct spi_dt_spec bus;

	struct gpio_dt_spec dc;
    struct gpio_dt_spec rst;
    struct gpio_dt_spec busy;
    struct gpio_dt_spec en;
};

static inline bool epd_has_pin(const struct gpio_dt_spec *spec)
{
	return spec->port != NULL;
}

static int epd_write_helper(const struct device *dev, bool cmd_present, uint8_t cmd, const uint8_t* data_buf, size_t data_len) {
    const struct epd_config *config = dev->config;

    struct spi_buf buffer;
	struct spi_buf_set buf_set = {
		.buffers = &buffer,
		.count = 1,
	};

    int ret = 0;

	buffer.buf = &cmd;
	buffer.len = sizeof(cmd);

	if (cmd_present) {
        LOG_ERR("Sending command: %02x", cmd);
		/* Set CD pin low for command */
		gpio_pin_set_dt(&config->dc, 0);
		ret = spi_write_dt(&config->bus, &buf_set);
		if (ret < 0) {
			goto out;
		}
	}

	if (data_len > 0) {
        //LOG_DBG("Sending data len %zu", data_len);
        for (size_t i = 0; i < data_len; i++) {
            buffer.buf = (void*)(data_buf + i);
            buffer.len = 1;

            //LOG_ERR("Sending data len %zu", buffer.len);

            // Set CD pin high for data 
            gpio_pin_set_dt(&config->dc, 1);
            ret = spi_write_dt(&config->bus, &buf_set);
            if (ret < 0) {
                goto out;
            }
        }
        /*
		buffer.buf = (void *)data_buf;
		buffer.len = data_len;

        LOG_ERR("Sending data len %zu", buffer.len);

		// Set CD pin high for data 
		gpio_pin_set_dt(&config->dc, 1);
		ret = spi_write_dt(&config->bus, &buf_set);
		if (ret < 0) {
			goto out;
		}*/
	}
out:
	return ret;
}

static int epd_do_command_list(const struct device *dev, const uint8_t* cmd_list) {
    const struct epd_config *config = dev->config;

    // Command list format:
    // 1 byte = length or special case
    // 2nd byte - command ID
    // 3rd+ byte (optional) data for the command
    int ret = 0;
    while(true) {
        LOG_INF("cmd is: %u", cmd_list[0]);
        if (cmd_list[0] == DONE) {
            LOG_INF("Command list completed");
            break;
        } else if (cmd_list[0] == WAIT_100ms) {
            LOG_INF("waiting 100ms");
            k_msleep(100);
            cmd_list++;
        }else if (cmd_list[0] == WAIT_FOR_BUSY) {
            uint32_t busy_count = 0;
            LOG_INF("Busy waiting.");
            while(true) {
                ret = gpio_pin_get_dt(&config->busy);
                if (ret < 0) {
                    LOG_ERR("failed to get busy pin");
                    return ret;
                }
                if (ret == 1) {
                    // busy is asserted, wait a little longer...
                    if (busy_count < 2000) { // TODO: make this configurable in config.
                        k_msleep(10);
                    } else {
                        LOG_ERR("BUSY never de-asserted.");
                        return -1;
                    }
                } else {
                    LOG_INF("display not busy");
                    break;
                }
            }
            cmd_list++;
        } else if (cmd_list[0] == DO_RESET) {
            LOG_INF("Resetting");
            if((ret = gpio_pin_set_dt(&config->rst, 1)) < 0) {
                LOG_ERR("failed to set rst pin");
                return ret;
            }
            k_msleep(10);
            if((ret = gpio_pin_set_dt(&config->rst, 0)) < 0) {
                LOG_ERR("failed to clear rst pin");
                return ret;
            }
            k_msleep(10);

            cmd_list++;
        } else {
            LOG_INF("Sending command string of total length %u, cmd %02x", cmd_list[0], cmd_list[1]);
            //LOG_HEXDUMP_INF(&cmd_list[1], cmd_list[0]);
            ret = epd_write_helper(dev, true, cmd_list[1], &cmd_list[2], cmd_list[0]-1);
            if (ret < 0) {
                LOG_ERR("failed to write command: %d", ret);
                return ret;
            }
            cmd_list+=cmd_list[0]+1;
        }
    }
    return 0;
}



int epd_set_type(const struct device *dev, epd_type_t typ) {
    struct epd_data *data = dev->data;
    switch (typ) {
        case EPD_TYPE_GDEY029T71H:
            data->meta = &GDEY029T71H_meta;
            return 0;
        case EPD_TYPE_GDEM035F51:
            data->meta = &GDEM035F51_meta;
            return 0;
        case EPD_TYPE_GDEY029F51:
            data->meta = &GDEY029F51_meta;
            return 0;
        case EPD_TYPE_GDEM075F52:
            data->meta = &GDEM075F52_meta;
            return 0;
        case EPD_TYPE_WS_75_V2B:
            data->meta = &WS_75_V2B_meta;
            return 0;
        default:
            LOG_ERR("Unknown type specified");
            return -1;
    }
}

int epd_get_dimensions(const struct device *dev, struct epd_dimensions *dims) {
    const struct epd_config *config = dev->config;
    struct epd_data *data = dev->data;
    if (data->meta == NULL) {
        LOG_ERR("Tried to power on with no type set");
        return -1;
    }

    dims->width = data->meta->width;
    dims->height = data->meta->height;
    dims->expected_data_size = data->meta->expected_data_size;
    dims->expected_data_size = data->meta->expected_data_size;
    dims->planes = data->meta->num_planes;

    return 0;
}

int epd_power_on(const struct device *dev) {
    struct epd_data *data = dev->data;
    const struct epd_config *config = dev->config;
    if (data->meta == NULL) {
        LOG_ERR("Tried to power on with no type set");
        return -1;
    }
    LOG_INF("powering on...");
    int ret = -1;
    if (epd_has_pin(&config->en)) {
        if((ret = gpio_pin_set_dt(&config->en, 1)) < 0) {
            LOG_ERR("failed to set en pin");
            return ret;
        }
        k_msleep(50); // give the epd a chance to power up
    }

    /*if((ret = gpio_pin_set_dt(&config->rst, 1)) < 0) {
        LOG_ERR("failed to set rst pin");
        return ret;
    }
    k_msleep(20);
    if((ret = gpio_pin_set_dt(&config->rst, 0)) < 0) {
        LOG_ERR("failed to clear rst pin");
        return ret;
    }
    k_msleep(20);*/

    return epd_do_command_list(dev, data->meta->init_command_list);
}
int epd_start_write_data(const struct device *dev, int plane) {
    struct epd_data *data = dev->data;
    const struct epd_config *config = dev->config;
    if (data->meta == NULL) {
        LOG_ERR("Tried to write data with no type set");
        return -1;
    }
    if (plane >= data->meta->num_planes) {
        LOG_ERR("Tried to write data beyond last plane");
        return -1;
    }

    return epd_write_helper(dev, true, data->meta->data_transmission_command[plane], NULL, 0);
}
int epd_continue_write_data(const struct device *dev, uint8_t *data, size_t data_len) {
    struct epd_data *ep_data = dev->data;
    const struct epd_config *config = dev->config;
    if (ep_data->meta == NULL) {
        LOG_ERR("Tried to write continued data with no type set");
        return -1;
    }

    return epd_write_helper(dev, false, 0x00, data, data_len);
}
int epd_do_refresh(const struct device *dev) {
    struct epd_data *data = dev->data;
    if (data->meta == NULL) {
        LOG_ERR("Tried to refresh with no type set");
        return -1;
    }
    return epd_do_command_list(dev, data->meta->refresh_command_list);
}
int epd_power_off(const struct device *dev) {
    const struct epd_config *config = dev->config;
    struct epd_data *data = dev->data;
    if (data->meta == NULL) {
        LOG_ERR("Tried to power off with no type set");
        return -1;
    }
    int ret = epd_do_command_list(dev, data->meta->powerdown_command_list);

    if (epd_has_pin(&config->en)) {
        gpio_pin_set_dt(&config->en, 0);
    }

    if((ret = gpio_pin_set_dt(&config->rst, 1)) < 0) {
        LOG_ERR("failed to set rst pin");
        return ret;
    }

    return ret;
}

static int epd_early_init(const struct device *dev)
{
	const struct epd_config *config = dev->config;
	struct epd_data *data = dev->data;
	int ret;

    data->meta = NULL;

	if (!device_is_ready(config->bus.bus)) {
		LOG_ERR("SPI device is not ready");
		return -ENODEV;
	}

    if (!epd_has_pin(&config->dc)) {
        LOG_ERR("No D/C pin specified");
        return -ENODEV;
    }
    if (!gpio_is_ready_dt(&config->dc)) {
        return -ENODEV;
    }
    ret = gpio_pin_configure_dt(&config->dc, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        LOG_ERR("Could not configure command/data GPIO (%d)", ret);
        return ret;
    }

    if (!epd_has_pin(&config->rst)) {
        LOG_ERR("No RST pin specified");
        return -ENODEV;
    }
    if (!gpio_is_ready_dt(&config->rst)) {
        return -ENODEV;
    }
    ret = gpio_pin_configure_dt(&config->rst, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Could not configure RST GPIO (%d)", ret);
        return ret;
    }

    if (!epd_has_pin(&config->busy)) {
        LOG_ERR("No BUSY pin specified");
        return -ENODEV;
    }
    if (!gpio_is_ready_dt(&config->busy)) {
        return -ENODEV;
    }
    ret = gpio_pin_configure_dt(&config->busy, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Could not configure busy GPIO (%d)", ret);
        return ret;
    }

    if (epd_has_pin(&config->en)) {
        if (!gpio_is_ready_dt(&config->en)) {
        return -ENODEV;
        }
        ret = gpio_pin_configure_dt(&config->en, GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            LOG_ERR("Could not configure enable GPIO (%d)", ret);
            return ret;
        }
    } else {
        LOG_INF("Configuring without enable signal");
    }

    return 0;
}

#define BLINK_GPIO_LED_DEFINE(inst)                                            \
	static struct epd_data data##inst;                          \
                                                                               \
	static const struct epd_config config##inst = {             \
	    .bus = SPI_DT_SPEC_INST_GET(inst, SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8), 0),    \
	    .dc = GPIO_DT_SPEC_INST_GET(inst, dc_gpios),           \
	    .rst = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),           \
	    .busy = GPIO_DT_SPEC_INST_GET(inst, busy_gpios),           \
	    .en = GPIO_DT_SPEC_INST_GET_OR(inst, en_gpios, {}),           \
	};                                                                     \
                                                                               \
	DEVICE_DT_INST_DEFINE(inst, epd_early_init, NULL, &data##inst,    \
			      &config##inst, POST_KERNEL,                      \
			      CONFIG_GENERIC_EPAPER_INIT_PRIORITY,                      \
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(BLINK_GPIO_LED_DEFINE)