cmake_minimum_required(VERSION 3.20.0)

set(BOARD nrf54l15dk/nrf54l15/cpuapp)

#find_package(Zephyr)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zapp)

target_sources(app PRIVATE src/main.c src/cbor.c src/heatshrink/heatshrink_decoder.c)



