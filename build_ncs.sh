export ZEPHYR_BASE=/home/alex/Downloads/nrf/sdk/v3.1.0/zephyr/
export PATH=~/Downloads/nrf/:$PATH
export PATH=/opt/SEGGER/JLink/:$PATH

#west build --pristine -b epaper_driver/nrf54l15/cpuapp -- -DBOARD_ROOT=.
#west flash --hex-file build/nrfapp/zephyr/zephyr.signed.hex#
#west build --pristine  -b nrf54l15dk/nrf54l15/cpuapp
