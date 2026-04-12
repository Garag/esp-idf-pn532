idf_component_register(
	SRCS
		src/pn532.c
		src/pn532_driver.c
		src/pn532_driver_i2c.c
		src/pn532_driver_hsu.c
		src/pn532_driver_spi.c
	INCLUDE_DIRS
		include
	REQUIRES
		driver esp_driver_gpio esp_driver_spi esp_driver_uart esp_driver_i2c
)
