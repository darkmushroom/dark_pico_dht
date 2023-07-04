@ECHO OFF
ECHO:
ECHO ===Let's flash and reset===
ECHO:
ECHO First, let's put the device back into BOOTSEL mode
..\picotool\build\picotool reboot -u -f
ECHO ...done!
ECHO:
TIMEOUT 2 >NUL
ECHO Now we'll flash our newest build
..\picotool\build\picotool load build\pico_temp_tracker.uf2
TIMEOUT 1 >NUL
ECHO: 
ECHO Finally, we'll reboot the device so the new code starts running
..\picotool\build\picotool reboot
ECHO ...done!
