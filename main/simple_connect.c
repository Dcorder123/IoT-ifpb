/**
 * @file simple_connect.c
 * @brief Demonstração de conexão Wi-Fi robusta + sincronização SNTP com servidores ntp.br
 * 
 * Componentes:
 * - tutorial.c/h: APIs de Wi-Fi e SNTP
 * - Servidores SNTP: a.ntp.br, b.ntp.br, c.ntp.br (de ntp.br)
 * - Credenciais: Carregadas do sdkconfig
 */

#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "esp_system.h"
#include "tutorial.h"

#define TAG "main"

// Credenciais do Wi-Fi (carregadas via menuconfig)
#define WIFI_SSID CONFIG_EXAMPLE_WIFI_SSID
#define WIFI_PASSWORD CONFIG_EXAMPLE_WIFI_PASSWORD

static bool is_time_synchronized(void)
{
    time_t now = time(NULL);
    struct tm timeinfo = *localtime(&now);
    int year = timeinfo.tm_year + 1900;
    return (year >= 2024);
}

void app_main(void)
{
    ESP_LOGI(TAG, "\n\n");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  ESP32 WI-FI + SNTP (ntp.br)              ║");
    ESP_LOGI(TAG, "║  Protocolo: WPA2-PSK                       ║");
    ESP_LOGI(TAG, "║  SNTP Sync: Servidores Brasileiros         ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════╝\n");

    // ========== PASSO 1: Inicializar hardware ==========
    esp_err_t ret = tutorial_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "✗ FALHA NA INICIALIZAÇÃO!");
        ESP_ERROR_CHECK(ret);  // Para execução
        return;
    }

    // ========== PASSO 2: Conectar ao Wi-Fi ==========
    ret = tutorial_connect(WIFI_SSID, WIFI_PASSWORD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "✗ FALHA NA CONEXÃO WI-FI!");
        goto cleanup;  // Ir para limpeza
    }

    // ========== PASSO 3: Obter informações do AP ==========
    wifi_ap_record_t ap_info = tutorial_get_ap_info();
    if (ap_info.ssid[0] == 0) {
        ESP_LOGW(TAG, "⚠ Não foi possível obter info do AP");
    }

    // ========== PASSO 4: Sincronizar data/hora via SNTP ==========
    ret = tutorial_sntp_sync();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠ SNTP falhou, continuando sem sincronização");
        // Não abortamos aqui, apenas alertamos
    }

    // ========== PASSO 5: Exibir data/hora atual ==========
    time_t now = time(NULL);
    struct tm timeinfo = *localtime(&now);
    
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    ESP_LOGI(TAG, "───────────────────────────────────────────");
    ESP_LOGI(TAG, "  DATA/HORA ATUAL (São Paulo)");
    ESP_LOGI(TAG, "───────────────────────────────────────────");
    ESP_LOGI(TAG, "  %s", strftime_buf);
    ESP_LOGI(TAG, "───────────────────────────────────────────\n");

    // ========== PASSO 6: Exibir horário em loop infinito (a cada 5s) ==========
    ESP_LOGI(TAG, "Iniciando loop infinito de horário (intervalo: 5 segundos)...");
    while (true) {
        if (!is_time_synchronized()) {
            ESP_LOGW(TAG, "⚠ Horário ainda não sincronizado. Tentando SNTP novamente...");
            ret = tutorial_sntp_sync();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "⚠ SNTP ainda não sincronizou (sem IP/DNS ou rede instável)");
            }
        }

        if (is_time_synchronized()) {
            time_t now_loop = time(NULL);
            struct tm timeinfo_loop = *localtime(&now_loop);
            char loop_time_buf[64];
            strftime(loop_time_buf, sizeof(loop_time_buf), "%Y-%m-%d %H:%M:%S", &timeinfo_loop);
            ESP_LOGI(TAG, "Hora atual: %s", loop_time_buf);
        } else {
            ESP_LOGW(TAG, "Hora ainda indisponível (aguardando DHCP/SNTP)");
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    // ========== PASSO 7: Desconectar e limpar ==========
cleanup:
    ESP_LOGI(TAG, "\nEncerrando aplicação...\n");
    
    ret = tutorial_disconnect();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠ Erro ao desconectar");
    }

    ret = tutorial_deinit();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠ Erro ao deinicializar");
    }

    ESP_LOGI(TAG, "╔════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  FIM DA APLICAÇÃO                          ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════╝\n");
}
