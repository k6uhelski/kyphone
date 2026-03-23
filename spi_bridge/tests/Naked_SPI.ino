#include <driver/spi_slave.h>
#include <SPI.h>

#define PIN_MOSI 13
#define PIN_SCLK 14
#define PIN_CS   15

WORD_ALIGNED_ATTR uint8_t rx_buf[34];
WORD_ALIGNED_ATTR uint8_t tx_buf[34];

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- NAKED ESP32 SPI TEST ---");
    Serial.println("No display libraries loaded. Pure SPI Slave.");

    spi_bus_config_t bus_config = {
        .mosi_io_num = PIN_MOSI, 
        .miso_io_num = -1, 
        .sclk_io_num = PIN_SCLK, 
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    spi_slave_interface_config_t peripheral_config = {
        .spics_io_num = PIN_CS, 
        .flags = 0,
        .queue_size = 1,
        .mode = 0, 
    };

    esp_err_t ret = spi_slave_initialize(HSPI_HOST, &bus_config, &peripheral_config, 0);
    if (ret != ESP_OK) {
        Serial.printf("Init Failed: %s\n", esp_err_to_name(ret));
    } else {
        Serial.println(">> LISTENING ON HSPI_HOST...");
    }
}

void loop() {
    memset(rx_buf, 0, 34);
    spi_slave_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 34 * 8; 
    t.rx_buffer = rx_buf;

    esp_err_t ret = spi_slave_transmit(HSPI_HOST, &t, portMAX_DELAY);

    if (ret == ESP_OK) {
        Serial.printf(">> RX COMPLETE: %d bits | Raw: %02X %02X %02X\n", t.trans_len, rx_buf[0], rx_buf[1], rx_buf[2]);
        
        int offset = -1;
        for(int i=0; i<34; i++) {
            if(rx_buf[i] == 0x02) { offset = i; break; }
        }
        if (offset != -1) {
            Serial.printf("SUCCESS! MSG: %s\n", (char*)&rx_buf[offset+1]);
        }
    }
}
