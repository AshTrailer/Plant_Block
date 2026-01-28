#include <stdio.h>          // 标准输入输出，用于printf
#include <string.h>         // 字符串操作，用于命令解析
#include "driver/gpio.h"    // ESP32 GPIO驱动库
#include "freertos/FreeRTOS.h"   // FreeRTOS内核
#include "freertos/task.h"       // FreeRTOS任务管理

#include "driver/uart.h"

// #include "driver/adc.h"           // 用于土壤湿度传感器ADC
// #include "ds18b20.h"              // DS18B20温度传感器库 (需另外安装)

#define GPIO_PIN_PUMP          GPIO_NUM_4     // 蠕动泵控制引脚 (通过IRFZ44N)
#define GPIO_PIN_FAN           GPIO_NUM_5     // 散热风扇控制引脚 (通过2N7000)
#define GPIO_PIN_LED           GPIO_NUM_6     // LED灯组控制引脚 (通过IRFZ44N)
#define GPIO_PIN_TEC           GPIO_NUM_7     // TEC半导体制冷片控制引脚 (通过IRFZ44N)
#define GPIO_PIN_SENSOR_POWER  GPIO_NUM_8     // 土壤湿度传感器电源控制 (通过SS8050)

// 串口输入缓冲区
#define INPUT_BUFFER_SIZE      64

/* ========== 3. 函数声明 ========== */
// 硬件初始化函数
static void hardware_init(void);

// 执行器控制函数 (你的"_"命名规范)
static void mosfet_control(gpio_num_t pin, uint8_t state);
static void bjt_control(gpio_num_t pin, uint8_t state);

// 串口命令处理函数
static void process_command(char* cmd);

/* ========== 4. 函数实现 ========== */
static void hardware_init(void) {
    // 1. 配置控制执行器的GPIO (MOSFET驱动)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_PIN_PUMP) | 
                        (1ULL << GPIO_PIN_FAN) | 
                        (1ULL << GPIO_PIN_LED) | 
                        (1ULL << GPIO_PIN_TEC),
        .mode = GPIO_MODE_OUTPUT,          // 输出模式
        .pull_up_en = GPIO_PULLUP_DISABLE, // 禁用上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // 禁用下拉
        .intr_type = GPIO_INTR_DISABLE     // 禁用中断
    };
    gpio_config(&io_conf);

    // 2. 配置控制传感器的GPIO (三极管驱动)
    gpio_config_t sensor_power_conf = {
        .pin_bit_mask = (1ULL << GPIO_PIN_SENSOR_POWER),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&sensor_power_conf);

    gpio_set_level(GPIO_PIN_PUMP, 0);
    gpio_set_level(GPIO_PIN_FAN, 0);
    gpio_set_level(GPIO_PIN_LED, 0);
    gpio_set_level(GPIO_PIN_TEC, 0);
    gpio_set_level(GPIO_PIN_SENSOR_POWER, 0); // 传感器默认断电

    printf("[硬件] 初始化完成，所有执行器已关闭。\n");
}

static void mosfet_control(gpio_num_t pin, uint8_t state) {
    if (state == 0 || state == 1) {
        gpio_set_level(pin, state);
        printf("[MOSFET控制] GPIO_%d -> %s\n", pin, state ? "开启" : "关闭");
    } else {
        printf("[错误] 无效的控制状态: %d (只能为0或1)\n", state);
    }
}

static void bjt_control(gpio_num_t pin, uint8_t state) {
    if (state == 0 || state == 1) {
        gpio_set_level(pin, state);
        printf("[三极管控制] 传感器电源 GPIO_%d -> %s\n", pin, state ? "上电" : "断电");
        
        if (state == 1) {
            vTaskDelay(pdMS_TO_TICKS(50)); // 50ms稳定时间
        }
    } else {
        printf("[错误] 无效的控制状态: %d\n", state);
    }
}

static void process_command(char* cmd) {
    char device[16];
    int state;
    
    if (sscanf(cmd, "%15s %d", device, &state) == 2) {
        device[strcspn(device, "\r\n")] = 0;
        
        // 根据设备名调用对应的控制函数
        if (strcmp(device, "pump") == 0) {
            mosfet_control(GPIO_PIN_PUMP, state);
        } else if (strcmp(device, "fan") == 0) {
            mosfet_control(GPIO_PIN_FAN, state);
        } else if (strcmp(device, "led") == 0) {
            mosfet_control(GPIO_PIN_LED, state);
        } else if (strcmp(device, "tec") == 0) {
            mosfet_control(GPIO_PIN_TEC, state);
        } else if (strcmp(device, "sensor") == 0) {
            bjt_control(GPIO_PIN_SENSOR_POWER, state);
        } else if (strcmp(device, "all") == 0) {
            // 特殊命令: 控制所有执行器
            mosfet_control(GPIO_PIN_PUMP, state);
            mosfet_control(GPIO_PIN_FAN, state);
            mosfet_control(GPIO_PIN_LED, state);
            mosfet_control(GPIO_PIN_TEC, state);
            printf("[全局控制] 所有执行器已%s\n", state ? "开启" : "关闭");
        } else {
            printf("[错误] 未知设备: %s\n", device);
            printf("可用设备: pump, fan, led, tec, sensor, all\n");
        }
    } else {
        printf("[错误] 命令格式无效。请使用: \"设备 状态\"\n");
        printf("示例: \"pump 1\" 或 \"fan 0\"\n");
    }
}

static void uart_command_task(void *arg) {
    char input_buffer[INPUT_BUFFER_SIZE];
    int input_index = 0;

    // 任务主循环
    while (1) {
        // 使用标准输入读取单个字符
        int ch = fgetc(stdin);
        
        if (ch != EOF) {
            // 处理换行符 - 表示命令结束
            if (ch == '\n' || ch == '\r') {
                if (input_index > 0) {
                    // 确保字符串以空字符结尾
                    input_buffer[input_index] = '\0';
                    
                    // 可选：回显接收到的完整命令
                    printf("\n[调试] 收到命令: %s\n", input_buffer);
                    
                    // 处理命令
                    process_command(input_buffer);
                    
                    // 重置索引，准备接收下一条命令
                    input_index = 0;
                }
                // 无论是否有内容，收到换行符后都打印新的提示符
                printf("> ");
                fflush(stdout);
            } 
            // 处理退格键（可选功能）
            else if (ch == '\b' || ch == 127) { // 127是DEL键的ASCII码
                if (input_index > 0) {
                    input_index--;
                    // 回显退格效果：退格、空格、再退格
                    printf("\b \b");
                    fflush(stdout);
                }
            }
            // 处理普通字符
            else if (input_index < (INPUT_BUFFER_SIZE - 1)) {
                // 回显字符
                putchar(ch);
                fflush(stdout);
                
                // 存储字符到缓冲区
                input_buffer[input_index++] = (char)ch;
            }
            // 缓冲区已满
            else {
                printf("\n[错误] 命令过长，已丢弃\n");
                input_index = 0;
                printf("> ");
                fflush(stdout);
            }
        } else {
            // 没有数据时让出CPU
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

// 定义任务句柄（可选，用于未来管理任务）
static TaskHandle_t uart_cmd_task_handle = NULL;

/* ========== app_main 函数 ========== */
void app_main(void) {
    printf("\n=========================================\n");
    printf("  自动植物养护系统 - 执行器测试程序\n");
    printf("  ESP32-C6 + ESP-IDF (USB控制台模式)\n");
    printf("=========================================\n\n");

    // 1. 初始化硬件
    hardware_init();

    printf("[系统] 硬件初始化完成，所有执行器已关闭。\n");
    printf("[系统] 正在启动命令接收任务...\n");

    // 2. 创建一个独立的任务来处理串口命令
    //    优先级5，栈大小4096，足够处理命令
    BaseType_t task_created = xTaskCreate(
        uart_command_task,        // 任务函数
        "uart_cmd",               // 任务名称
        4096,                     // 栈大小
        NULL,                     // 任务参数
        5,                        // 优先级（适中）
        &uart_cmd_task_handle     // 任务句柄
    );

    if (task_created != pdPASS) {
        printf("[严重错误] 无法创建命令处理任务！系统停止。\n");
        // 主任务挂起，因为核心功能无法运行
        vTaskSuspend(NULL);
    }

    printf("[系统] 命令处理任务已启动。\n");
    printf("[系统] 等待串口命令输入...\n");
    printf("[命令格式] <设备> <状态>\n");
    printf("[可用设备] pump, fan, led, tec, sensor, all\n");
    printf("[状态] 0=关闭, 1=开启\n");
    printf("示例: 开启蠕动泵 -> \"pump 1\"\n");
    printf("      关闭所有设备 -> \"all 0\"\n\n");

    // 3. 主任务可以进入低功耗循环或执行其他管理功能
    while (1) {
        // 这里可以添加系统状态监测、看门狗喂食等
        vTaskDelay(pdMS_TO_TICKS(1000)); // 每1秒循环一次，让出CPU
    }
}
