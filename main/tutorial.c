#include "tutorial.h"
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/event_groups.h"
#include "lwip/apps/sntp.h"

#define TAG "wifi_tutorial"

/* Definições de eventos Wi-Fi */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define SNTP_SYNCED_BIT BIT2

#define WIFI_AUTHMODE WIFI_AUTH_WPA2_PSK
#define WIFI_RETRY_ATTEMPT 5

/* Variáveis estáticas */
static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t *tutorial_netif = NULL;
static esp_event_handler_instance_t wifi_event_handler = NULL;
static esp_event_handler_instance_t ip_event_handler = NULL;
static int wifi_retry_count = 0;

static bool has_valid_ipv4(void)
{
    if (tutorial_netif == NULL) {
        return false;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(tutorial_netif, &ip_info) != ESP_OK) {
        return false;
    }

    return (ip_info.ip.addr != 0);
}

static esp_err_t wait_for_ipv4(uint32_t timeout_ms)
{
    const uint32_t step_ms = 1000;
    uint32_t elapsed_ms = 0;

    while (elapsed_ms < timeout_ms) {
        if (has_valid_ipv4()) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(tutorial_netif, &ip_info) == ESP_OK) {
                ESP_LOGI(TAG, "✓ IPv4 disponível: " IPSTR, IP2STR(&ip_info.ip));
            }
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            return ESP_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(step_ms));
        elapsed_ms += step_ms;
    }

    return ESP_ERR_TIMEOUT;
}

/* Servidores SNTP brasileiros (ntp.br) */
static const char* sntp_servers[] = {
    "a.ntp.br",      // Operado por IX (Internet eXchange) Brasil
    "b.ntp.br",      // Operado por IX Brasil
    "c.ntp.br",      // Operado por Braspag
    "pool.ntp.org"   // Fallback: pool.ntp.org
};
#define SNTP_SERVERS_COUNT (sizeof(sntp_servers) / sizeof(sntp_servers[0]))

/**
 * @brief Event handler para eventos do Wi-Fi (camada de enlace)
 */
static void wifi_event_cb(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "✓ Estação Wi-Fi iniciada, tentando conectar...");
        esp_wifi_connect();
    } 
    else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "✓ Conectado ao AP no nível de enlace (Link Layer)");
        wifi_retry_count = 0; // Reset tentativas
    }
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = 
            (wifi_event_sta_disconnected_t*) event_data;
        
        ESP_LOGW(TAG, "✗ Desconectado do AP (código de razão: %d)", 
                 disconnected->reason);
        
        if (wifi_retry_count < WIFI_RETRY_ATTEMPT) {
            wifi_retry_count++;
            ESP_LOGI(TAG, "  → Tentativa %d/%d de reconexão...", 
                     wifi_retry_count, WIFI_RETRY_ATTEMPT);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "✗ Falha na conexão Wi-Fi após %d tentativas", 
                     WIFI_RETRY_ATTEMPT);
        }
    }
}

/**
 * @brief Event handler para eventos de IP (camada de rede)
 */
static void ip_event_cb(void* arg, esp_event_base_t event_base,
                        int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "✓ Obtido endereço IPv4: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "  → Máscara: " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "  → Gateway: " IPSTR, IP2STR(&event->ip_info.gw));
        
        // Sinaliza que Wi-Fi está conectado
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
#ifdef CONFIG_IPV6_ENABLED
    else if (event_id == IP_EVENT_GOT_IP6) {
        ip_event_got_ip6_t* event = (ip_event_got_ip6_t*) event_data;
        ESP_LOGI(TAG, "✓ Obtido endereço IPv6: " IPV6STR, 
                 IPV62STR(event->ip6_info.ip));
    }
#endif
}

/**
 * @brief Verifica se SNTP sincronizou comparando o tempo do sistema
 */
static bool is_system_time_synced(void)
{
    // Tempo mínimo esperado (1 de janeiro de 2024)
    time_t reference_time = 1704067200;
    time_t current_time = time(NULL);
    
    return (current_time > reference_time);
}

esp_err_t tutorial_init(void)
{
    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  INICIALIZANDO WI-FI + SNTP (ntp.br)");
    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    
    // 1. Inicializar NVS (armazenamento não-volátil)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "⚠ NVS corrompido, apagando e reinicializando...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "✗ Falha ao inicializar NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✓ NVS inicializado");

    // 2. Criar grupo de eventos
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "✗ Falha ao criar event group");
        return ESP_FAIL;
    }

    // 3. Inicializar stack TCP/IP
    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "✗ Falha ao inicializar netif: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✓ Stack TCP/IP inicializado");

    // 4. Criar event loop padrão
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "✗ Falha ao criar event loop: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✓ Event loop criado");

    // 5. Registrar handlers Wi-Fi e IP
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              &wifi_event_cb, NULL,
                                              &wifi_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "✗ Falha ao registrar handler Wi-Fi: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                              &ip_event_cb, NULL,
                                              &ip_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "✗ Falha ao registrar handler IP: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✓ Event handlers registrados");

    // 6. Criar interface Wi-Fi STA (Station)
    ret = esp_wifi_set_default_wifi_sta_handlers();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "✗ Falha ao fazer set handlers: %s", esp_err_to_name(ret));
        return ret;
    }

    tutorial_netif = esp_netif_create_default_wifi_sta();
    if (tutorial_netif == NULL) {
        ESP_LOGE(TAG, "✗ Falha ao criar interface Wi-Fi STA");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "✓ Interface Wi-Fi STA criada");

    // 7. Inicializar driver Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "✗ Falha ao inicializar Wi-Fi: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✓ Driver Wi-Fi inicializado");

    // 8. Desabilitar promiscuous mode e configurar para STA
    esp_wifi_set_promiscuous(false);

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  INICIALIZAÇÃO CONCLUÍDA");
    ESP_LOGI(TAG, "═══════════════════════════════════════════\n");

    return ESP_OK;
}

esp_err_t tutorial_connect(char* wifi_ssid, char* wifi_password)
{
    if (wifi_ssid == NULL || wifi_password == NULL) {
        ESP_LOGE(TAG, "✗ SSID ou senha NULL");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "───────────────────────────────────────────");
    ESP_LOGI(TAG, "  CONECTANDO AO WI-FI");
    ESP_LOGI(TAG, "───────────────────────────────────────────");
    ESP_LOGI(TAG, "  SSID: %s", wifi_ssid);

    // Configurar conexão Wi-Fi
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTHMODE,
            .pmf_cfg = {
                .capable = true,
                .required = false  // Permite APs sem PMF
            },
        },
    };

    // Copiar SSID e senha com segurança
    strncpy((char*)wifi_config.sta.ssid, wifi_ssid, 
            sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, wifi_password,
            sizeof(wifi_config.sta.password) - 1);

    // Configurações de potência e armazenamento
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));  // Sem power save
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));  // Config em RAM

    // Aplicar configuração
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "✗ Falha ao configurar modo STA: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "✗ Falha ao aplicar config: %s", esp_err_to_name(ret));
        return ret;
    }

    // Iniciar Wi-Fi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "✗ Falha ao iniciar Wi-Fi: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✓ Wi-Fi iniciado\n");

    // Aguardar evento de conexão ou falha (timeout: 15 segundos)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,  // Não limpar bits automaticamente
                                           pdFALSE,  // Qualquer bit dispara
                                           pdMS_TO_TICKS(15000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "───────────────────────────────────────────");
        ESP_LOGI(TAG, "✓ CONECTADO COM SUCESSO AO WI-FI!");
        ESP_LOGI(TAG, "───────────────────────────────────────────\n");
        return ESP_OK;
    } 
    else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "───────────────────────────────────────────");
        ESP_LOGE(TAG, "✗ FALHA NA CONEXÃO WI-FI");
        ESP_LOGE(TAG, "───────────────────────────────────────────\n");
        return ESP_FAIL;
    }
    else {
        // Se associou ao AP, mas não recebeu IP a tempo, permite continuar.
        // Isso evita abortar a aplicação antes dos logs de horário em loop.
        wifi_ap_record_t ap_info = {0};
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGW(TAG, "⚠ Timeout aguardando IP, mas link Wi-Fi está ativo (SSID: %s)", ap_info.ssid);

            if (tutorial_netif != NULL) {
                esp_netif_dhcpc_stop(tutorial_netif);
                esp_netif_dhcpc_start(tutorial_netif);
                ESP_LOGI(TAG, "→ DHCP reiniciado. Aguardando IPv4 por mais 30 segundos...");
            }

            if (wait_for_ipv4(30000) == ESP_OK) {
                ESP_LOGI(TAG, "✓ IPv4 obtido após espera extra de DHCP");
                return ESP_OK;
            }

            ESP_LOGW(TAG, "⚠ Ainda sem IPv4. Prosseguindo sem IP. SNTP pode falhar até o DHCP responder.");
            return ESP_OK;
        }

        ESP_LOGE(TAG, "✗ Timeout aguardando conexão WI-FI");
        return ESP_FAIL;
    }
}

wifi_ap_record_t tutorial_get_ap_info(void)
{
    wifi_ap_record_t ap_info = {0};
    
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "✗ Falha ao obter info do AP: %s", esp_err_to_name(ret));
        return ap_info;
    }

    ESP_LOGI(TAG, "───────────────────────────────────────────");
    ESP_LOGI(TAG, "  INFORMAÇÕES DO PONTO DE ACESSO");
    ESP_LOGI(TAG, "───────────────────────────────────────────");
    ESP_LOGI(TAG, "  SSID: %s", ap_info.ssid);
    
    // Exibir MAC address
    ESP_LOGI(TAG, "  MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2],
             ap_info.bssid[3], ap_info.bssid[4], ap_info.bssid[5]);
    
    ESP_LOGI(TAG, "  Canal Primário: %d", ap_info.primary);
    ESP_LOGI(TAG, "  RSSI (força sinal): %d dBm", ap_info.rssi);
    ESP_LOGI(TAG, "───────────────────────────────────────────\n");

    return ap_info;
}

esp_err_t tutorial_sntp_sync(void)
{
    ESP_LOGI(TAG, "───────────────────────────────────────────");
    ESP_LOGI(TAG, "  SINCRONIZANDO DATA/HORA COM SNTP");
    ESP_LOGI(TAG, "───────────────────────────────────────────");

    // Garantir IPv4 válido antes de iniciar SNTP
    if (!has_valid_ipv4()) {
        ESP_LOGW(TAG, "⚠ Ainda sem IPv4, tentando renovar DHCP...");
        if (tutorial_netif != NULL) {
            esp_netif_dhcpc_stop(tutorial_netif);
            esp_netif_dhcpc_start(tutorial_netif);
        }

        if (wait_for_ipv4(10000) != ESP_OK) {
            ESP_LOGE(TAG, "✗ Wi-Fi sem IPv4. Aborte SNTP por enquanto.");
            return ESP_FAIL;
        }
    }

    // Aguardar que TCP/IP resolva DNS
    vTaskDelay(pdMS_TO_TICKS(100));

    // Configurar timezone para Brasil (UTC-3, sem horário de verão)
    setenv("TZ", "BRT3", 1);
    tzset();
    ESP_LOGI(TAG, "✓ Timezone configurado: São Paulo (UTC-3)");

    // Desabilitar SNTP antes (segurança)
    sntp_stop();

    // Inicializar SNTP com os servidores
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, (char*)sntp_servers[0]);
    sntp_setservername(1, (char*)sntp_servers[1]);
    sntp_setservername(2, (char*)sntp_servers[2]);
    sntp_setservername(3, (char*)sntp_servers[3]);
    
    ESP_LOGI(TAG, "✓ Servidores SNTP configurados:");
    for (int i = 0; i < SNTP_SERVERS_COUNT; i++) {
        ESP_LOGI(TAG, "    [%d] %s", i, sntp_servers[i]);
    }

    // Iniciar SNTP
    sntp_init();
    ESP_LOGI(TAG, "✓ SNTP iniciado, aguardando sincronização...\n");

    // Polling do horário até estar sincronizado (timeout: 30 segundos)
    int sync_attempts = 0;
    int max_attempts = 30; // 30 segundos (1 segundo por tentativa)
    
    while (!is_system_time_synced() && sync_attempts < max_attempts) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        sync_attempts++;
        
        if (sync_attempts % 5 == 0) {
            ESP_LOGI(TAG, "  → Aguardando sincronização (%d/%d)s", 
                     sync_attempts, max_attempts);
        }
    }

    if (is_system_time_synced()) {
        // Exibir hora sincronizada
        time_t now = time(NULL);
        struct tm timeinfo = *localtime(&now);
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        ESP_LOGI(TAG, "✓ Horário sincronizado via SNTP!");
        ESP_LOGI(TAG, "  → Data/Hora: %s (UTC-3)", strftime_buf);
        
        ESP_LOGI(TAG, "───────────────────────────────────────────");
        ESP_LOGI(TAG, "✓ DATA/HORA SINCRONIZADA COM SUCESSO!");
        ESP_LOGI(TAG, "───────────────────────────────────────────\n");
        return ESP_OK;
    } 
    else {
        ESP_LOGE(TAG, "✗ Timeout sincronizando SNTP (30s)");
        return ESP_FAIL;
    }
}

esp_err_t tutorial_disconnect(void)
{
    ESP_LOGI(TAG, "Desconectando do Wi-Fi...");
    esp_err_t ret = esp_wifi_disconnect();
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "✗ Erro ao desconectar: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "✓ Desconectado");
    }
    
    return ret;
}

esp_err_t tutorial_deinit(void)
{
    ESP_LOGI(TAG, "Encerrando e liberando recursos...");

    // Parar SNTP
    sntp_stop();

    // Deletar event group
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }

    // Parar Wi-Fi
    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGE(TAG, "✗ Erro ao parar Wi-Fi: %s", esp_err_to_name(ret));
    }

    // Deinicializar Wi-Fi
    ret = esp_wifi_deinit();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGE(TAG, "✗ Erro ao deinicializar Wi-Fi: %s", esp_err_to_name(ret));
    }

    // Limpar drivers e handlers
    if (tutorial_netif != NULL) {
        esp_wifi_clear_default_wifi_driver_and_handlers(tutorial_netif);
        esp_netif_destroy(tutorial_netif);
        tutorial_netif = NULL;
    }

    // Desregistrar event handlers
    if (wifi_event_handler != NULL) {
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT,
                                                              ESP_EVENT_ANY_ID,
                                                              wifi_event_handler));
    }

    if (ip_event_handler != NULL) {
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT,
                                                              ESP_EVENT_ANY_ID,
                                                              ip_event_handler));
    }

    ESP_LOGI(TAG, "✓ Recursos liberados");
    return ESP_OK;
}
