idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "." "../../common" "../../.."
  REQUIRES PCA9555 esp_driver_i2c esp_driver_gpio esp_timer freertos vfs
)
