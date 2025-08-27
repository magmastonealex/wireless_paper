# SPDX-License-Identifier: Apache-2.0



board_runner_args(openocd "--config=board/st_nucleo_f103rb.cfg")

# keep first
include(${ZEPHYR_BASE}/boards/common/openocd-stm32.board.cmake)
