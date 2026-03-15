#include <Inkplate.h>
#include <driver/spi_slave.h>

Inkplate display(INKPLATE_1BIT); 

// 34 bytes: 2 Dummy bytes + 1 Header + 30 Chars + 1 Null
WORD_ALIGNED_ATTR uint8_t rx_buf[34];
WORD_ALIGNED_ATTR uint8_t tx_buf[34];
bool firstLoad = true;

// Shared pins with E-Ink bus (13, 14, 15)
// IO 12 is the Yellow wire (MISO)
#define PIN_MOSI 13
#define PIN_MISO 12
#define PIN_SCLK 14
#define PIN_CS   15

void setup() {
    Serial.begin(115200);
    delay(2000); 
    Serial.println("\n--- KYPHONE SPI: ROBUST POLLING MODE ---");
    
    display.begin();
    display.clearDisplay();
    display.display(); 

    memset(rx_buf, 0, 34);
    memset(tx_buf, 0, 34);

    setupSPI();
}

void setupSPI() {
    spi_bus_config_t bus_config = {
        .mosi_io_num = PIN_MOSI, 
        .miso_io_num = PIN_MISO, // Restore MISO for signaling
        .sclk_io_num = PIN_SCLK, 
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    spi_slave_interface_config_t peripheral_config = {
        .spics_io_num = PIN_CS, 
        .flags = 0,
        .queue_size = 3,
        .mode = 0, 
    };

    esp_err_t ret = spi_slave_initialize(VSPI_HOST, &bus_config, &peripheral_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        Serial.printf("SPI Slave Init Failed: %s\n", esp_err_to_name(ret));
    } else {
        Serial.println("SYSTEM READY: WAITING FOR RADXA...");
    }
}

void loop() {
    memset(rx_buf, 0, sizeof(rx_buf));
    
    // PRE-LOAD THE READY SIGNAL (0x06 ACK)
    memset(tx_buf, 0, 34);
    tx_buf[0] = 0x06; 

    spi_slave_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 34 * 8; 
    t.rx_buffer = rx_buf;
    t.tx_buffer = tx_buf;

    // Wait for a transaction from Radxa
    esp_err_t ret = spi_slave_transmit(VSPI_HOST, &t, portMAX_DELAY);

    if (ret == ESP_OK) {
        // Look for 0x02 header anywhere in the buffer
        int offset = -1;
        for(int i=0; i<34; i++) {
            if(rx_buf[i] == 0x02) { offset = i; break; }
        }

        if (offset != -1) {
            Serial.printf("SUCCESS! Found 0x02 at Offset %d. MSG: ", offset);
            Serial.println((char*)&rx_buf[offset+1]);
            
            // Shut down SPI to release pins for E-Ink Bus
            spi_slave_free(VSPI_HOST);
            
            display.clearDisplay();
            display.setTextSize(4);
            display.setCursor(50, 200);
            display.print((char*)&rx_buf[offset+1]);
            
            if (firstLoad) { display.display(); firstLoad = false; }
            else { display.partialUpdate(); }
            
            delay(100);
            setupSPI(); 
            Serial.println("READY FOR NEXT COMMAND.");
        }
    }
}
