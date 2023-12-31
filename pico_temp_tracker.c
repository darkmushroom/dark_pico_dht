// TODO God help me when I have to break this code into separate libraries

// standard libs
#include <stdio.h>
#include "pico/stdlib.h"

// hardware libs
#include "hardware/gpio.h"

// dual core support
#include "pico/multicore.h"
#include "pico/util/queue.h"

// network libs
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"

// contains passwords
#include "secrets.h"

void core1_entry();
err_t accept_connection(void*, struct tcp_pcb*, err_t);
err_t data_received(void*, struct tcp_pcb*, struct pbuf*, err_t);

queue_t sensor_output1;
queue_t sensor_output2;
struct Readings {
    float humidity;
    float temperature;
};

int main(void) {
    struct Readings fridge;
    fridge.humidity = 0.0f;
    fridge.temperature = 0.0f;

    struct Readings freezer;
    freezer.humidity = 0.0f;
    freezer.temperature = 0.0f;

    // wake up our stdio for debug output, init reading queues, and launch reading
    stdio_init_all();
    queue_init(&sensor_output1, sizeof(fridge), 2);
    queue_init(&sensor_output2, sizeof(freezer), 2);
    multicore_launch_core1(core1_entry);

    // while core 1 is busy warming up our sensors, let's connect to the internet

    // hardware init
    cyw43_arch_init_with_country(CYW43_COUNTRY_USA);
    cyw43_arch_enable_sta_mode();

    // link layer init
    while (true) {
        int connected = PICO_ERROR_CONNECT_FAILED;
        connected = cyw43_arch_wifi_connect_timeout_ms(SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000);
        if (connected == PICO_OK) break;
        printf("Failed to connect. Retrying in 10 seconds. Error: %d\n", connected);
        sleep_ms(10000);
    }
    printf("Connected!\n");

    // transport layer init
    struct tcp_pcb *prom_night = tcp_new();


    tcp_bind(prom_night, IP_ANY_TYPE, 80);
    prom_night = tcp_listen(prom_night);
    tcp_accept(prom_night, accept_connection);

    // FIXME: First few values may be invalid or 0 while DHT warms up, throw these out

    while (true) {
        cyw43_arch_lwip_begin();
        cyw43_arch_poll();
        cyw43_arch_lwip_end();

        queue_try_remove(&sensor_output1, &fridge);
//        printf("fridge  > humidity: %.1f%%, temp: %.1fC (%.1fF)\n", fridge.humidity, fridge.temperature, fridge.temperature * 9 / 5 + 32);
//        sleep_ms(1000);
        queue_try_remove(&sensor_output2, &freezer);
//        printf("freezer > humidity: %.1f%%, temp: %.1fC (%.1fF)\n", freezer.humidity, freezer.temperature, freezer.temperature * 9 / 5 + 32);
//        sleep_ms(1000); 
        sleep_ms(10);
    }
}

err_t accept_connection(void *arg, struct tcp_pcb *newpcb, err_t err){
    printf("connection accept\n");
    tcp_recv(newpcb, data_received);
    return ERR_OK;
}

// FIXME this variable doesn't get cleared correctly
char temp[1000];
uint pos = 0;

err_t data_received(void *arg, struct tcp_pcb *tcpb, struct pbuf *p, err_t err) {

    if (p==NULL) {
        printf("Client closed (or acknowledged we closed) the connection\n");
        return ERR_OK;
    } 

    printf("received data from %s.\nTotal length: %d Length received: %d\n",ipaddr_ntoa(&tcpb->local_ip), p->tot_len, p->len);
    // FIXME This code almost certainly doesn't handle large reads correctly
    pbuf_copy_partial(p,temp,p->tot_len-pos,pos);
    pos+=p->tot_len;
    tcp_recved(tcpb, p->tot_len);

    //testing
    printf("Data received so far:\n%s\n", temp);
    if (pos == p->tot_len) {
        //FIXME this output code should /really/ belong in its own function
        //FIXME preparing the headers and content body would be easier as two separate steps
        //TODO actually output sensor data
        char hi[]="HTTP/1.1 200 OK\r\nContent-Length: 20\r\nContent-Type: text/html; charset=utf-8\r\n\r\n<html>haiii</html>\r";
        hi[strlen(hi)] = '\n'; //replace null terminator
        tcp_write(tcpb, hi, sizeof(hi), TCP_WRITE_FLAG_COPY);
        tcp_output(tcpb); //write immediately
        tcp_close(tcpb);
        pos = 0;
    }
    pbuf_free(p);
    return ERR_OK;
}

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
    2. + Pi pulls DHT_PIN low for at least 1ms (most sources recommend ~18ms)
    3. ++ Pi then pulls the line high for 20~40µs
    4. Pi moves DHT_PIN to input mode, relinquishing control to the DHT
    5. Acknowledgement part 1: DHT pulls the DHT_PIN low for 80µs
    6. Acknowledgement part 2: DHT pulls the DHT_PIN high for 80µs

    + In practice, 20ms is required
    ++ Despite the second 'pull high for 20µs' instruction being clearly
    documented in the data sheet, we end up missing the first half of the
    acknowledgement following these instructions. Instead, I opted to pull
    the line high and /immediately/ hand control to the DHT_22

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
    18000µs + 80µs + 80µs + (50µs + 70µs)*40 = 23000µs
    |-init--|-acknowledge-|------read-------|

    Data should only be requested every 2 seconds (to allow the DHT22 internal
    sensor to accumulate a reading). Since total communication only takes 23ms,
    we can throw in a full 2s delay before the next reading.

    Imprecise but still incredibly helpful links:
    * https://www.sparkfun.com/datasheets/Sensors/Temperature/DHT22.pdf
    * https://www.nutsvolts.com/magazine/article/march2013_Henry
*/

struct Readings request_reading(uint);
void wake_up_dht(uint);
bool sensor_acknowledge(uint);
bool read_data(uint, uint*);
bool format_data(uint*, int*, int*);
bool validate_checksum(int*);
void dump_sensor_data(uint*, int*);

void core1_entry () {

    const uint DHT_PIN1 = 28;
    const uint DHT_PIN2 = 15;

    gpio_init(DHT_PIN1);
    gpio_init(DHT_PIN2);

    /*
        FIXME The sleeps in this loop badly slow down
        core0. Preliminary research suggests sleep is
        handled by a timer on core0.
        Ideas on how to fix:
        * Switch our netcode to core1, sensors core0
        * write a more 'busy' wait, allowing core0 to poll
        between sleeps?
        * setup a callback timer that wakes up the sensors
        after a certain amount of time (if sleep doesn't
        do this already)
    */

   // FIXME sensors should push new data into the queue if it is full
    while(true) {
        struct Readings reading1 = request_reading(DHT_PIN1);
        queue_try_add(&sensor_output1, &reading1);
        sleep_ms(1000);
        struct Readings reading2 = request_reading(DHT_PIN2);
        queue_try_add(&sensor_output2, &reading2);
        sleep_ms(1000);
    }
}

struct Readings request_reading(uint DHT_PIN) {
    uint byte_array[40];
    int formatted_data[5];
    int sign = 1;
    struct Readings reading;
    reading.humidity = 0.0f;
    reading.temperature = 0.0f;

    wake_up_dht(DHT_PIN);
    if (sensor_acknowledge(DHT_PIN) == false) {
        printf("Sensor did not acknowledge read request.\n");
    }
    else if (read_data(DHT_PIN, byte_array) == false) {
        printf("Did not receive a full 40bits of data.\n");
    }
    else if (format_data(byte_array, formatted_data, &sign) == false) {
        printf("Checksum failed.\n");
        dump_sensor_data(byte_array, formatted_data);
    }
    else {
        reading.humidity = ((256 * ((float)formatted_data[0] + ((float)formatted_data[1]/256)))/10);
        reading.temperature = ((256 * ((float)formatted_data[2] + ((float)formatted_data[3]/256)))/10) * sign;
    }

    return reading;
}

/*
    Initiates communication with the DHT22 by pulling
    DHT_PIN low for a hefty 20ms, driving it high, then
    immediately handing control over to the sensor.
*/
void wake_up_dht(uint DHT_PIN) {
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
bool sensor_acknowledge(uint DHT_PIN) {
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

    @returns true if we successfully received 40 bits
*/
bool read_data(uint DHT_PIN, uint *byte_array) {

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

    @returns true if the converted bytes matches the checksum
*/
bool format_data(uint *byte_array, int *formatted_data, int *sign) {
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

    bool validate_before_conversion = validate_checksum(formatted_data);

    /*
        if the top bit of byte 3 is 1, we're dealing with 
        a negative temp. We'll set the sign to negative and
        fix the formatted data. (data - 10000000 or 128)
    */ 
    *sign = 1;
    if (byte_array[16] == 1) {
        *sign = -1;
        formatted_data[2] -= 128;
    }

    return validate_before_conversion;
}

/*
    DHT_22 checksum only cares about the last 8 bits of
    the sum of all the other bytes. Values greater than
    11111111 (255) will wrap.
*/
bool validate_checksum (int *formatted_data) {
    int test = formatted_data[0] + formatted_data[1] + formatted_data[2] + formatted_data[3];
    if (test > 255) {
        return formatted_data[4] == (test - 256);
    }
    else return formatted_data[4] == test;
}

void dump_sensor_data (uint *byte_array, int *formatted_data) {
    for (uint i = 0; i < 5; i++) {
        printf("%d ", formatted_data[i]);
    }
    printf("\n");
    for (uint i = 0; i < 40; i++) {
        printf("%d", byte_array[i]);
    }
    printf("\n");
}
