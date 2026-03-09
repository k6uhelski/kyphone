#include <Inkplate.h>
#include <driver/spi_slave.h>

Inkplate display(INKPLATE_1BIT); 

void setup() {
    Serial.begin(115200);
    display.begin();
    
    spi_bus_config_t bus_config = {
        .mosi_io_num = 13, 
        .miso_io_num = -1, 
        .sclk_io_num = 14, 
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    spi_slave_interface_config_t peripheral_config = {
        .spics_io_num = 15, // Using IO 15 for CS to avoid SCL conflict
        .flags = 0,
        .queue_size = 3,
        .mode = 0,
    };
    spi_slave_initialize(HSPI_HOST, &bus_config, &peripheral_config, GPIO_INTR_DISABLE);

    Serial.println("SPI Initialized. Attempting to draw to screen...");
    display.clearDisplay();
    display.setTextSize(4);
    display.setTextColor(BLACK);
    display.setCursor(50, 200);
    display.print("IO 15 DRAW TEST");
    display.display(); 
    Serial.println("Draw command sent.");
}

void loop() {
}
