add_executable(tapeshnik
        tapeshnik.cpp
        debounce.cpp
        tacho.cpp
        wheel.cpp
        )

# pull in common dependencies
target_link_libraries(tapeshnik pico_stdlib hardware_pwm correct_static)

pico_enable_stdio_usb(tapeshnik  1)
pico_enable_stdio_uart(tapeshnik 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(tapeshnik)

# add url via pico_set_program_url
example_auto_set_url(tapeshnik)
