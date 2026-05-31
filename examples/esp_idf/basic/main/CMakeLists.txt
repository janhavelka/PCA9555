idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "."
  REQUIRES PCA9555 esp_driver_i2c esp_timer freertos log
)
