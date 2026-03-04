#include <Inkplate.h>
#include <driver/spi_slave.h>

Inkplate display(INKPLATE_1BIT); 
char receive_buffer[33]; 
bool firstLoad = true;

void setup() {
    Serial.begin(115200);
    display.begin();
    display.clearDisplay();
    display.display(); 

    // Using your EXISTING pinout
    spi_bus_config_t bus_config = {
        .mosi_io_num = 13, 
        .miso_io_num = -1, // DISCONNECT PHYSICAL PIN 12
        .sclk_io_num = 14, 
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    spi_slave_interface_config_t slave_config = {
        .spics_io_num = 22, 
        .flags = 0,
        .queue_size = 3,
        .mode = 0,
    };

    spi_slave_initialize(HSPI_HOST, &bus_config, &slave_config, GPIO_INTR_DISABLED);
    Serial.println("SYSTEM READY: POLLING SPI (PINS 13, 14, 22)");
}

void loop() {
    memset(receive_buffer, 0, sizeof(receive_buffer));

    spi_slave_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 32 * 8; 
    t.rx_buffer = receive_buffer;

    // Use a very short timeout (50ms) so we don't hang if Radxa isn't sending
    esp_err_t ret = spi_slave_transmit(HSPI_HOST, &t, 50 / portTICK_PERIOD_MS);

    if (ret == ESP_OK && receive_buffer[0] == 0x02) { 
        Serial.print("RECEIVED: ");
        Serial.println(&receive_buffer[1]);

        display.clearDisplay();
        display.setTextSize(4);
        display.setTextColor(BLACK);
        display.setCursor(50, 200);
        display.print(&receive_buffer[1]);
        
        if (firstLoad) {
            display.display(); 
            firstLoad = false;
        } else {
            display.partialUpdate(); 
        }
    }
    // No "else" print here to keep the Serial monitor clean during polling
}
