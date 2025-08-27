# SPDX-License-Identifier: Apache-2.0


board_runner_args(openocd --cmd-pre-init "source [find interface/cmsis-dap.cfg]")
board_runner_args(openocd --cmd-pre-init "transport select swd")
board_runner_args(openocd --cmd-pre-init "source [find target/rp2040.cfg]")

# The adapter speed is expected to be set by interface configuration.
# But if not so, set 2000 to adapter speed.
board_runner_args(openocd --cmd-pre-init "set_adapter_speed_if_not_set 2000")

board_runner_args(uf2 "--board-id=RPI-RP2")

# keep first
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/uf2.board.cmake)