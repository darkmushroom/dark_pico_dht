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
static uint byte_array[40];
static int formatted_data[5];

void request_reading();
bool sensor_acknowledge();
bool read_data();
bool format_data();
void wipe_data();

int main() {
    stdio_init_all();
    gpio_init(DHT_PIN);

    while(true) {
        request_reading();
        if (sensor_acknowledge() == false) {
            printf("Sensor did not acknowledge read request.\n");
        }
        else if (read_data() == false) {
            printf("Did not receive a full 40bits of data.\n");
        }
        else if (format_data() == false) {
            printf("Checksum failed.\n");
        }
        else {
            printf("Actual reading: ");
            float humidity = ((256 * ((float)formatted_data[0] + ((float)formatted_data[1]/256)))/10);
            float temp = ((256 * ((float)formatted_data[2] + ((float)formatted_data[3]/256)))/10);
            printf("Humidity: %.1f%% || ", humidity);
            printf("Temperature: %.1fC\n", temp);
        }

        wipe_data();
        sleep_ms(2000);
    }
}

/*
    Initiates communication with the DHT22 by pulling
    DHT_PIN low for a hefty 20ms, driving it high, then
    immediately handing control over to the sensor.
*/
void request_reading() {
    gpio_set_dir(DHT_PIN, GPIO_OUT);
    gpio_put(DHT_PIN, 0);
    sleep_ms(20);
    gpio_put(DHT_PIN, 1);
    gpio_set_dir(DHT_PIN, GPIO_IN);
}

/*
    After requesting a reading, the DHT22 drives our
    DHT_PIN low for 80us then high for 80us. This function
    is calibrated to count this two-part acknowledgement
    for 65us and 60us respectively since we lose some time
    doing comparisons and assignments.

    @returns true if both parts of the acknowledgement
    were successful
*/
bool sensor_acknowledge() {
    bool ack_part_1 = false;
    bool ack_part_2 = false;
    uint count = 0;
    
    for (uint frame = 0; frame < 255; frame++) {
        if (gpio_get(DHT_PIN) == 0) count++;
        if (count > 65) {
            ack_part_1 = true;
            break;
        }
        sleep_us(1);
    }

    count = 0;

    for (uint frame = 0; frame < 255; frame++) {
        if (gpio_get(DHT_PIN) == 1) count++;
        if (count > 60) {
            ack_part_2 = true;
            break;
        }
        sleep_us(1);
    }

    return ack_part_1 && ack_part_2;
}

/*
    Let's read the sensor values. Better programmers
    will stuff the bits into bytes as values are read, but
    I will settle on a static int array. This uses 16x the
    memory (80bytes to store 40 bits).. but that's about
    0.003% of our available mem
*/
bool read_data() {

    sleep_us(4); // buffer between sensor ack and data

    uint last = 0;
    uint j = 0;
    while (j < 40) {
        uint hi_count = 0;
        for (uint kill = 0; kill < 256; kill++) {
            // we sleep
            if (gpio_get(DHT_PIN)) break;
            sleep_us(1);
        }
        while (gpio_get(DHT_PIN)) {
            // real shit
            hi_count++;
            sleep_us(1);
        }

        /*
            For some reason, the short pulse-width (0) is *very*
            stable and accurate, always coming in at a count of 20.
            For this reason, we'll count anything longer than 30
            as a high bit (1).
        */
        if (hi_count > 30) {
            byte_array[j] = 1;
        }
        else {
            byte_array[j] = 0;
        }

        j++;
    }

    return j == 40;
}

/*
    Turn our big byte_array into a more manageable int
    array. There are fewer places in the code it is more
    clear I am uncomfortable with bitwise operations.
*/
bool format_data() {
    // FIXME: Does not work for negative temp values... which is sort of the point
    uint k = 4;
    int accumulator = 0;
    uint power = 1;
    for (uint i = 0; i < 40; i++) {
        accumulator += byte_array[(40 - 1) - i] * power;
        power *= 2;
        if ((i+1) % 8 == 0 && i != 40) {
            formatted_data[k] = accumulator;
            k--;
            accumulator = 0;
            power = 1;
        }
    }
    
    return formatted_data[0] + formatted_data[1] + formatted_data[2] + formatted_data[3] == formatted_data[4];
}

void wipe_data() {
    for (uint i = 0; i < 40; i++) {
        byte_array[i] = 0;
    }
    for (uint i = 0; i < 5; i++) {
        formatted_data[i] = 0;
    }
}

