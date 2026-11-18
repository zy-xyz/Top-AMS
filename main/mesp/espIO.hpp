/*
* Copyright (C) 2025-2026 by nccrrv
* SPDX-License-Identifier: AGPL-3.0-or-later
* 
* espIO.hpp
* 简单的esp输入输出包装
*/

#pragma once

#include "../mstd/other.hpp"
#include "config.hpp"
#include <esp_timer.h>


namespace mesp {

    inline std::array<gpio_config_t, config::MAX_GPIO> gpio_state{};

    inline mstd::call_once __init_gpio(
        []() {
            for (size_t i = 0; i < config::MAX_GPIO; ++i) {
                gpio_state[i] = {
                    1ull << i,// 设置要操作的接口,掩码结构
                    GPIO_MODE_DISABLE,// 设置是输入还是输出
                    GPIO_PULLUP_DISABLE,// 开关上拉
                    GPIO_PULLDOWN_DISABLE,// 开关下拉
                    GPIO_INTR_DISABLE// 开关中断
                };
            }
        });


    //普通输出
    inline void gpio_out(gpio_num_t IO, bool value) {

        if (gpio_state[IO].mode != GPIO_MODE_OUTPUT) {
            gpio_config_t io_conf = {
                1ull << IO,// 设置要操作的接口,掩码结构
                GPIO_MODE_OUTPUT,// 设置是输入还是输出
                GPIO_PULLUP_DISABLE,//开关上拉
                GPIO_PULLDOWN_DISABLE,//开关下拉
                GPIO_INTR_DISABLE// 开关中断
            };
            gpio_config(&io_conf);
        }

        gpio_set_level(IO, value);
        // fpr(IO,' ',value);
    }//gpio_out

    //开漏输出
    inline void gpio_out_OD(gpio_num_t IO, bool value) {
        if (gpio_state[IO].mode != GPIO_MODE_OUTPUT_OD) {
            gpio_config_t io_conf = {
                1ull << IO,// 设置要操作的接口,掩码结构
                GPIO_MODE_OUTPUT_OD,// 设置是输入还是输出
                GPIO_PULLUP_DISABLE,//开关上拉
                GPIO_PULLDOWN_DISABLE,//开关下拉
                GPIO_INTR_DISABLE// 开关中断
            };
            gpio_config(&io_conf);
        }

        gpio_set_level(IO, value);
    }//gpio_out_OD



    //中断事件:只带 pin 和微秒时间戳,全部是 POD,可在 ISR 里安全构造
    //不能用 std::chrono:steady_clock::now() 不在 IRAM,ISR 里调用有踩 flash cache 的风险
    struct gpio_event {
        gpio_num_t pin;
        int64_t us;//esp_timer_get_time()
    };

    inline QueueHandle_t gpio_channle = xQueueCreate(8, sizeof(gpio_event));//深度 8,边沿密集时不丢

    //统一将IO脚编号加入队列
    inline void IRAM_ATTR __gpio_isr_handler(void* arg) {
        BaseType_t woken = pdFALSE;
        gpio_event in{(gpio_num_t)(uintptr_t)(arg), esp_timer_get_time()};
        xQueueSendFromISR(gpio_channle, &in, &woken);
        if (woken == pdTRUE)
            portYIELD_FROM_ISR();//有更高优先级任务被唤醒时立刻切,别等 ISR 返回
    }

    inline void gpio_set_in(gpio_num_t IO) {//未来如果有别的需求可以抽象一下,只改中断触发方式
        // static_assert(IO != 9, "BOOT");//?上电前不能下拉，ESP32会进入下载模式
        // static_assert(IO != 11, "11");//GPIO11默认为SPI flash的VDD引脚，需要配置后才能作为GPIO使用
        if (IO == gpio_num_t::GPIO_NUM_NC)
            return;


        uint64_t pin_mask = 1ull << IO;
        gpio_config_t io_conf = {
            pin_mask,
            GPIO_MODE_INPUT,
            GPIO_PULLUP_ENABLE,//开上拉
            GPIO_PULLDOWN_DISABLE,
            // GPIO_INTR_NEGEDGE//下降沿触发
            // GPIO_INTR_LOW_LEVEL//低电平触发
            GPIO_INTR_ANYEDGE//上下边沿触发
        };
        gpio_config(&io_conf);
        gpio_set_intr_type(IO, GPIO_INTR_ANYEDGE);


        gpio_isr_handler_add(IO, __gpio_isr_handler, (void*)IO);
    }//gpio_set_in

    inline mstd::call_once __gpio_set_init([] {
        gpio_install_isr_service(0);
    });

}//mesp