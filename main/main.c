/*
 * LED blink with FreeRTOS
 */
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "ssd1306.h"
#include "gfx.h"

#include "pico/stdlib.h"
#include <stdio.h>

const uint TRIGGER_PIN = 4;
const uint ECHO = 5;

SemaphoreHandle_t xSemaphoreTrigger;
QueueHandle_t xQueueTime;
QueueHandle_t xQueueDistance;

void pin_callback(uint gpio, uint32_t events)
{

    uint32_t time;
    if (events == 0x4)
    {

        time = to_us_since_boot(get_absolute_time());
        // printf("start_time %.2f  \n", time);
    }
    else if (events == 0x8)
    {
        time = to_us_since_boot(get_absolute_time());
        // printf("- Duration calculada: %.2f cm \n" , time);
    }
    xQueueSendFromISR(xQueueTime, &time, 0);
}

void echoTask(void *p)
{
    gpio_init(ECHO);
    gpio_set_dir(ECHO, GPIO_IN);
    gpio_set_irq_enabled_with_callback(ECHO, GPIO_IRQ_EDGE_RISE | 
                                        GPIO_IRQ_EDGE_FALL,
                                       true,
                                       &pin_callback);

    uint32_t tempos[2];
    int lidos = 0;

    while (true)
    {
        if (xQueueReceive(xQueueTime, &tempos[lidos], pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            lidos++;

            double distance;
            if (lidos > 1)
            {
                distance = (tempos[1] - tempos[0]) * 0.01715;
                xQueueSend(xQueueDistance, &distance, 0);
                lidos = 0;
            }
        }
        else
        {
            double distance = -300.0;
            xQueueSend(xQueueDistance, &distance, 0);
            lidos = 0;
        }

        // liguei semafaro de que de calculei a distancia
    }
}
void trigger_task(void *p)
{
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);

    while (true)
    {
        gpio_put(TRIGGER_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_put(TRIGGER_PIN, 0);
        xSemaphoreGive(xSemaphoreTrigger);
        vTaskDelay(pdMS_TO_TICKS(990));
    }
}

void oled_task(void *p)
{
    double distance;
    char distance_str[20];
    const int maxWidth = 128;

    printf("Inicializando Driver\n");
    ssd1306_init();

    printf("Inicializando GLX\n");
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    printf("Inicializando btn and LEDs\n");

    while (1)
    {
        if (xSemaphoreTake(xSemaphoreTrigger, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            vTaskDelay(10);
            if (xQueueReceive(xQueueDistance, &distance, pdMS_TO_TICKS(1000)) == pdTRUE)
            {
                if (distance == -300.0)
                {
                    gfx_clear_buffer(&disp);
                    gfx_draw_string(&disp, 0, 0, 2, "falha");
                    gfx_show(&disp);
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                else
                {
                    gfx_clear_buffer(&disp);
                    snprintf(distance_str, sizeof(distance_str), "- distancia: %d", (int)distance);
                    gfx_draw_string(&disp, 0, 0, 1, distance_str);
                    int tamanhobarra = (int)((distance / 80.0) * maxWidth);
                    if (tamanhobarra > maxWidth)
                    {
                        tamanhobarra = maxWidth;
                    }
                    gfx_draw_line(&disp, 0, 31, tamanhobarra, 31);
                    gfx_show(&disp);
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        }
    }
}

int main()
{
    stdio_init_all();

    xQueueTime = xQueueCreate(32, sizeof(uint32_t));
    xQueueDistance = xQueueCreate(32, sizeof(double));
    xSemaphoreTrigger = xSemaphoreCreateBinary();

    xTaskCreate(oled_task, "OLED", 4095, NULL, 1, NULL);
    xTaskCreate(trigger_task, "Trigger", 4095, NULL, 1, NULL);
    xTaskCreate(echoTask, "Echo", 4095, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true)
        ;
}