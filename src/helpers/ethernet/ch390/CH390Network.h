#pragma once
#include <ESP32_CH390.h>

// Hardware startup shared by companion transport and synchronization-only use.
inline bool beginCH390Network() {
  ch390_config_t config = CH390_DEFAULT_CONFIG();
  config.spi_miso_gpio = ETH_MISO_PIN;
  config.spi_mosi_gpio = ETH_MOSI_PIN;
  config.spi_sck_gpio = ETH_SCLK_PIN;
  config.spi_cs_gpio = ETH_CS_PIN;
  config.int_gpio = ETH_INT_PIN;
  return CH390.begin(config);
}
