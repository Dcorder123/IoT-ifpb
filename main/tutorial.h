#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include <time.h>

/**
 * @brief Inicializa o hardware e interfaces necessárias para Wi-Fi e SNTP
 * @return ESP_OK se bem-sucedido, ESP_FAIL caso contrário
 */
esp_err_t tutorial_init(void);

/**
 * @brief Conecta a uma rede Wi-Fi usando SSID e senha
 * @param wifi_ssid SSID da rede (máx 32 caracteres)
 * @param wifi_password Senha WPA2-PSK (máx 64 caracteres)
 * @return ESP_OK se conectado, ESP_FAIL caso contrário
 */
esp_err_t tutorial_connect(char* wifi_ssid, char* wifi_password);

/**
 * @brief Obtém informações do AP (ponto de acesso) conectado
 * @return Estrutura com dados do AP
 */
wifi_ap_record_t tutorial_get_ap_info(void);

/**
 * @brief Sincroniza data/hora via SNTP com servidores brasileiros
 * @return ESP_OK se sincronizado, ESP_FAIL caso contrário
 */
esp_err_t tutorial_sntp_sync(void);

/**
 * @brief Desconecta do Wi-Fi
 * @return ESP_OK se bem-sucedido
 */
esp_err_t tutorial_disconnect(void);

/**
 * @brief Desativa o Wi-Fi e libera recursos
 * @return ESP_OK se bem-sucedido
 */
esp_err_t tutorial_deinit(void);
