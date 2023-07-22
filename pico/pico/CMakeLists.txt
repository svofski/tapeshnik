add_executable(tapeshnik
        tapeshnik.c
        debounce.c
        tacho.c
        wheel.c
        )

# pull in common dependencies
target_link_libraries(tapeshnik pico_stdlib correct_static)

pico_enable_stdio_usb(tapeshnik  1)
pico_enable_stdio_uart(tapeshnik 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(tapeshnik)

# add url via pico_set_program_url
example_auto_set_url(tapeshnik)
