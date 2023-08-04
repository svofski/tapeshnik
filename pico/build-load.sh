make && picotool info -f || sleep 2 && picotool load pico/tapeshnik.elf && picotool reboot && \
    (while [ ! -e /dev/ttyACM0 ] ; do sleep 0.25 ; echo -n . ;  done) &&\
    picocom /dev/ttyACM0
