add_executable(tapeshnik
        tapeshnik.cpp
        debounce.cpp
        tacho.cpp
        wheel.cpp
        bitstream.cpp
        readloop.cpp
        sectors.cpp
        plaintext.cpp
        crc.c
        mfm.cpp
        )

pico_generate_pio_header(tapeshnik ${CMAKE_CURRENT_LIST_DIR}/bitstream.pio)

# pull in common dependencies
target_link_libraries(tapeshnik 
    pico_stdlib
    pico_multicore
    hardware_pwm
    hardware_pio
    correct_static)

pico_enable_stdio_usb(tapeshnik  1)
pico_enable_stdio_uart(tapeshnik 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(tapeshnik)

# add url via pico_set_program_url
example_auto_set_url(tapeshnik)
