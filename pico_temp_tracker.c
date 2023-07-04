/*
    My understanding of dht22 communication

    Relevant Pico hardware considerations:
    Pi pico runs at 125mhz by default, so each clock is ~ .008µs.
    Plenty fast for this communication

    Relevant DHT22 hardware considerations:
    In most instances, the line is pulled high by default.
    This means there is no activity or communication going on.

    Init/requesting a reading
    1. Pi must set GPIO DHT_PIN to output mode
    2. Pi pulls DHT_PIN low for at least 1ms (most sources recommend ~18ms) (20ms ACTUALLY WORKS)
    NOPE (3. Pi then pulls the line high for 20~40µs) NOPE
    4. Pi moves DHT_PIN to input mode, relinquishing control to the DHT
    5. Acknowledgement part 1: DHT pulls the DHT_PIN low for 80µs
    6. Acknowledgement part 2: DHT pulls the DHT_PIN high for 80µs
    
    If everything goes well, the DHT will begin sending out sensor data

    Expected data format is 5 bytes long, MSB (big endian)
    byte 1 = Relative Humidity (RH) high byte
    byte 2 = RH low byte (0% to 1% using 0 to 255)
    byte 3 = Temperature high byte (Celsius)
    byte 4 = Temperature low byte (0C to 1C using 0 to 255)
    byte 5 = Checksum*

    * The checksum should equal the last 8 bits of the product of bytes 1-4
    Last 8 bits of bytes 1+2+3+4 == checksum

    Actually reading the data:
    7. Every bit is preceeded by the DHT pulling the line low for 50µs
    8. DHT will then pull the line high for 70µs to indicate a '1'
    9. If the DHT transitions back to low after 26~28µs, we've received a '0'

    Total communication time (worst case scenario) is:
    18000µs + (NOPE)40µs(NOPE) + 80µs + 80µs + (50µs + 70µs)*40 = 23000µs
    |-----------init-----------|-acknowledge-|------read------|

    Data should only be requested every 2 seconds (to allow the DHT22 internal
    sensor to accumulate a reading). Since total communication only takes 23ms,
    we can throw in a full 2s delay before the next reading.
*/

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/gpio.h"

const uint DHT_PIN = 15;

int main() {
    stdio_init_all();

    // FIXME: init cyw43 here just for funsies. Refactor to networking core later
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        printf("Wi-Fi init failed");
        return -1;
    }

    gpio_init(DHT_PIN);

    while(true) {
            
        //request reading    
        gpio_set_dir(DHT_PIN, GPIO_OUT);
        gpio_put(DHT_PIN, 0);
        sleep_ms(20);
        gpio_put(DHT_PIN, 1);
        // sleep_us(40); wtf this shit doesn't work
        gpio_set_dir(DHT_PIN, GPIO_IN);

        bool ack_part_1 = false;
        bool ack_part_2 = false;
        uint count = 0;

        // TODO: Part 1 of the acknowledgement can probably be skipped, line high is more interesting

        /*
            Acknowledgement part 1, line low for 80µs.
            (We count anything over 65 low readings as a true because reading
            and processing data eats up a few cycles)
        */
        for (uint frame = 0; frame < 255; frame++) {
            if (gpio_get(DHT_PIN) == 0) count++;
            if (count > 65) {
                ack_part_1 = true;
                break;
            }
            sleep_us(1);
        }

        count = 0;

        /*
            Acknowledgement part 2, line high for 80µs.
            (We count anything over 60 high readings as a true because reading
            and processing data eats up a few cycles)
        */
        for (uint frame = 0; frame < 255; frame++) {
            if (gpio_get(DHT_PIN) == 1) count++;
            if (count > 60) {
                ack_part_2 = true;
                break;
            }
            sleep_us(1);
        }

            sleep_us(4); // buffer between sensor ack and data
        /*
            Let's read the sensor values. Better programmers
            will stuff the bits into bytes as values are read,
            but I will settle on an int array. This uses 16x
            the memory (80bytes to store 40 bits).. but that's
            about 0.003% of our available mem
        */

        uint byte_array[40];
        uint last = 0;
        uint j = 0;
        while (j < 40) {
            count = 0;
            for (uint kill = 0; kill < 256; kill++) {
                // we sleep
                if (gpio_get(DHT_PIN)) break;
                sleep_us(1);
            }
            while (gpio_get(DHT_PIN)) {
                // real shit
                count++;
                sleep_us(1);
            }

            /*
                For some reason, the short pulse-width (0) is *very*
                stable and accurate, always coming in at a count of 20.
                For this reason, we'll count anything longer than 30
                as a high bit (1).
            */
            if (count > 30) {
                byte_array[j] = 1;
            }
            else {
                byte_array[j] = 0;
            }

            j++;
            sleep_us(1);
        }

        // FIXME: Does not work for negative temp values... which is sort of the point
        int formatted_data[5];
        uint k = 4;
        int builder = 0;
        uint power = 1;
        for (uint i = 0; i < 40; i++) {
            builder += byte_array[(40 - 1) - i] * power;
            power *= 2;
            if ((i+1) % 8 == 0 && i != 40) {
                formatted_data[k] = builder;
                k--;
                builder = 0;
                power = 1;
            }
        }

        printf("Actual reading: ");
        float humidity = ((256 * ((float)formatted_data[0] + ((float)formatted_data[1]/256)))/10);
        float temp = ((256 * ((float)formatted_data[2] + ((float)formatted_data[3]/256)))/10);
        printf("Humidity: %.1f%% || ", humidity);
        printf("Temperature: %.1fC\n", temp);

        sleep_ms(2000);
    }
}
