# cmake version
cmake_minimum_required(VERSION 3.13)

# include the sdk.cmake file
include(pico_sdk_import.cmake)

# give the project a name (anything you want)
project(ADPC_transmitter C CXX ASM)

set(PICO_BOARD pico2_w)

# initialize the sdk
pico_sdk_init()

add_executable(ADPC_transmitter
        dhcpserver/dhcpserver.c
	capstone_adc.c
)

target_sources(ADPC_transmitter PRIVATE ADPC_transmitter.c)

target_include_directories(ADPC_transmitter PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(ADPC_transmitter
        pico_cyw43_arch_lwip_threadsafe_background
        pico_stdlib pico_bootsel_via_double_reset hardware_sync pico_sync
	hardware_dma hardware_adc

)

pico_enable_stdio_usb(ADPC_transmitter 1)
pico_enable_stdio_uart(ADPC_transmitter 0)

pico_add_extra_outputs(ADPC_transmitter)

