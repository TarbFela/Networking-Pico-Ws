# cmake version
cmake_minimum_required(VERSION 3.13)

# include the sdk.cmake file
include(pico_sdk_import.cmake)

# give the project a name (anything you want)
project(Computer_receiver C CXX ASM)

set(PICO_BOARD pico_w)

# initialize the sdk
pico_sdk_init()

add_executable(Computer_receiver
        dhcpserver/dhcpserver.c
	capstone_adc.c
)

target_sources(Computer_receiver PRIVATE Computer_receiver.c)

target_include_directories(Computer_receiver PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(Computer_receiver
        pico_cyw43_arch_lwip_threadsafe_background
        pico_stdlib pico_bootsel_via_double_reset hardware_sync pico_sync
	hardware_dma hardware_adc

)

pico_enable_stdio_usb(Computer_receiver 1)
pico_enable_stdio_uart(Computer_receiver 0)

pico_add_extra_outputs(Computer_receiver)

