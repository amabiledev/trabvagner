/* 
   PROJETO TCP SERVER 
   ALUNAS: MARIANE & AMÁBILE 
   CURSO: ENG DA COMPUTAÇÃO
   INSTITUIÇÃO: UNISATC
*/
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include <dht.h>
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <ultrasonic.h>
#include "driver/gpio.h"
#include "esp_err.h"

#define MAX_DISTANCE_CM 500 
#define TRIGGER_GPIO 	14
#define ECHO_GPIO 	    13
#define TRUE          	1 
#define FALSE		  	0
#define DHT_GPIO        GPIO_NUM_5

#define PORT CONFIG_EXAMPLE_PORT
static const dht_sensor_type_t sensor_type = DHT_TYPE_DHT11;
static const gpio_num_t dht_gpio = DHT_GPIO;
QueueHandle_t bufferTemperature; 
QueueHandle_t bufferHumidity;
QueueHandle_t bufferDistance;
static const char *TAG = " ";

void task_dht(void *pvParamters)
{
    int16_t temperature = 0;
    int16_t humidity = 0;
    gpio_set_pull_mode( dht_gpio , GPIO_PULLUP_ONLY);
    while(TRUE)
    {
        if(dht_read_data(sensor_type, dht_gpio, &humidity, &temperature) == ESP_OK)
        { 
            humidity = humidity / 10; //Quem tem sensor
            temperature = temperature / 10; //Quem tem sensor
            xQueueSend(bufferTemperature,&temperature,pdMS_TO_TICKS(0));
            xQueueSend(bufferHumidity,&humidity,pdMS_TO_TICKS(0));
        }
        else
        {
            ESP_LOGE(TAG, "Não foi possível ler o sensor"); 
        }        
        vTaskDelay(2000/portTICK_PERIOD_MS);
    }
}

void ultrasonic_test(void *pvParamters)
{
    ultrasonic_sensor_t sensor = {
        .trigger_pin = TRIGGER_GPIO,
        .echo_pin = ECHO_GPIO
    };

    ultrasonic_init(&sensor);

    while (true)
    {
        uint32_t distance;
        esp_err_t res = ultrasonic_measure_cm(&sensor, MAX_DISTANCE_CM, &distance);
        if (res != ESP_OK)
        {
           //printf("Error: ");
            switch (res)
            {
                case ESP_ERR_ULTRASONIC_PING:
                    //printf("Cannot ping (device is in invalid state)\n");
                    break;
                case ESP_ERR_ULTRASONIC_PING_TIMEOUT:
                    //printf("Ping timeout (no device found)\n");
                    break;
                case ESP_ERR_ULTRASONIC_ECHO_TIMEOUT:
                    //printf("Echo timeout (i.e. distance too big)\n");
                    break;
                default:
                    printf("%d\n", res);
            }
        }
        else
         xQueueSend(bufferDistance,&distance,pdMS_TO_TICKS(0));
         vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}
      

static void do_retransmit(const int sock)
{
    int len;
    char rx_buffer[128];
    uint16_t temp = 0;
    uint16_t umid = 0;
    uint16_t dist = 0;

    do {
        xQueueReceive(bufferTemperature,&temp,pdMS_TO_TICKS(2000));
        xQueueReceive(bufferHumidity,&umid,pdMS_TO_TICKS(2000));
        xQueueReceive(bufferDistance,&dist,pdMS_TO_TICKS(2000));
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            rx_buffer[len] = 0; 
            ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);

                if(len>=3 && rx_buffer[0]=='T' && rx_buffer[1]=='E' && rx_buffer[2]=='M' &&  rx_buffer[3]=='P' )
			{
                char legenda[15] = "\nTemperatura:";
                char value[6];
                sprintf(value, "%i", temp);
                send(sock, strcat(legenda, value), strlen(legenda), 0);
			 }

               if(len>=3 && rx_buffer[0]=='U' && rx_buffer[1]=='M' && rx_buffer[2]=='I' &&  rx_buffer[3]=='D' )
			{
                char legenda[15] = "\nUmidade:";
                char value[6];
                sprintf(value, "%i", umid);
                send(sock, strcat(legenda, value), strlen(legenda), 0);
			 }

                 if(len>=3 && rx_buffer[0]=='D' && rx_buffer[1]=='I' && rx_buffer[2]=='S' &&  rx_buffer[3]=='T' )
			{
                char legenda[15] = "\nDistancia:";
                char value[6];
                sprintf(value, "%i", dist);
                send(sock, strcat(legenda, value), strlen(legenda), 0);
			 }
        }
    } while (len > 0);
}

static void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family;
    int ip_protocol;


#ifdef CONFIG_EXAMPLE_IPV4
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;
    inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
#else // IPV6
    struct sockaddr_in6 dest_addr;
    bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
    dest_addr.sin6_family = AF_INET6;
    dest_addr.sin6_port = htons(PORT);
    addr_family = AF_INET6;
    ip_protocol = IPPROTO_IPV6;
    inet6_ntoa_r(dest_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
#endif

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_in6 source_addr; 
        uint addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        if (source_addr.sin6_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
        } else if (source_addr.sin6_family == PF_INET6) {
            inet6_ntoa_r(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        do_retransmit(sock);

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

void app_main(void)
{
    bufferTemperature = xQueueCreate(5, sizeof(uint32_t));
    bufferHumidity= xQueueCreate(5, sizeof(uint32_t));
    bufferDistance = xQueueCreate(5, sizeof(uint32_t));
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());
    xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);
    xTaskCreate(task_dht,"task_dht", configMINIMAL_STACK_SIZE * 3,NULL,5,NULL);
    xTaskCreate(ultrasonic_test, "ultrasonic_test", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);
}