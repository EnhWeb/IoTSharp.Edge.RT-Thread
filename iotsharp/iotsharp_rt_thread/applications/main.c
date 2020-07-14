/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-09-01     ZeroFree     first implementation
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "paho_mqtt.h"
#include "wifi_config.h"
#include <wlan_mgnt.h>
#include <drv_lcd.h>
#include "aht10.h"
/**
 * MQTT URI farmat:
 * domain mode
 * tcp://iot.eclipse.org:1883
 *
 * ipv4 mode
 * tcp://192.168.10.1:1883
 * ssl://192.168.10.1:1884
 *
 * ipv6 mode
 * tcp://[fe80::20c:29ff:fe9a:a07e]:1883
 * ssl://[fe80::20c:29ff:fe9a:a07e]:1884
 */
#define MQTT_URI "tcp://139.9.232.10:1883"
#define MQTT_USERNAME "93c490602e974e61b892f1367ab53bde"
#define MQTT_PASSWORD "93c490602e974e61b892f1367ab53bde"
#define MQTT_SUBTOPIC "/devices/me/attributes/response/+"
#define MQTT_PUBTOPIC "/devices/me/telemetry"

/* define MQTT client context */
static MQTTClient client;
static void mq_start(void);
static void mq_publish(const char *send_str);
static void mqtt_senddatetime(MQTTClient *c);
char pub_topic[48] = {0};
char sup_topic[48] = {0};
int main(void)
{
    unsigned int count = 1;

    aht10_device_t dev;
    const char *i2c_bus_name = "i2c2";

    /* register the wlan ready callback function */
    rt_wlan_register_event_handler(RT_WLAN_EVT_READY, (void (*)(int, struct rt_wlan_buff *, void *))mq_start, RT_NULL);
    /* initialize the autoconnect configuration */
    wlan_autoconnect_init();
    /* enable wlan auto connect */
    rt_wlan_config_autoreconnect(RT_TRUE);

    /* set RGB_LED pin mode to output */
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    /* set KEY0 pin mode to input */
    rt_pin_mode(PIN_KEY0, PIN_MODE_INPUT);
    lcd_clear(BLACK);
    lcd_set_color( BLACK,WHITE);
    lcd_show_string(1, 1, 24, "IoTSharp.Edge V0.1");
    /* draw a line on lcd */
    lcd_draw_line(0, 69 + 16 + 24 + 32, 240, 69 + 16 + 24 + 32);

    /* draw a concentric circles */
    lcd_draw_point(120, 194);
    for (int i = 0; i < 46; i += 4)
    {
        lcd_draw_circle(120, 194, i);
    }

    rt_thread_mdelay(2000);

    while (count > 0)
    {
        rt_thread_mdelay(10000);
        if (dev == RT_NULL)
        {
            dev = aht10_init(i2c_bus_name);
            if (dev == RT_NULL)
            {
                rt_kprintf(" The sensor initializes failure");
                mq_publish("\"aht10_status\":false");
            }
            else
            {
                rt_kprintf(" The sensor initializes success");
                mq_publish("\"aht10_status\":true");
            }
        }
        else
        {
            float humidity, temperature;
            char application_message[256] = {0};
            humidity = aht10_read_humidity(dev);
            temperature = aht10_read_temperature(dev);
            snprintf(application_message, sizeof(application_message), "{\"humidity\":%d.%d,\"temperature\":%d.%d}", (int)humidity, (int)(humidity * 10) % 10, (int)temperature, (int)(temperature * 10) % 10);
            mq_publish(application_message);
            lcd_show_string(1, 72, 24, "Humidity:%d.%d      ", (int)humidity, (int)(humidity * 10) % 10);
						lcd_show_string(1, 72+20, 24, "Temperature:%d.%d   ",  (int)temperature, (int)(temperature * 10) % 10);
            count++;
        }
    }
}

static void mqtt_sub_callback(MQTTClient *c, MessageData *msg_data)
{
    *((char *)msg_data->message->payload + msg_data->message->payloadlen) = '\0';
    rt_kprintf("Topic: %.*s receive a message: %.*s\n",
               msg_data->topicName->lenstring.len,
               msg_data->topicName->lenstring.data,
               msg_data->message->payloadlen,
               (char *)msg_data->message->payload);

    return;
}

static void mqtt_sub_default_callback(MQTTClient *c, MessageData *msg_data)
{
    *((char *)msg_data->message->payload + msg_data->message->payloadlen) = '\0';
    rt_kprintf("mqtt sub default callback: %.*s %.*s\n",
               msg_data->topicName->lenstring.len,
               msg_data->topicName->lenstring.data,
               msg_data->message->payloadlen,
               (char *)msg_data->message->payload);
    return;
}

static void mqtt_connect_callback(MQTTClient *c)
{
    rt_kprintf("Start to connect  mqtt server\n");
		lcd_show_string(1, 24, 24, "Start to connect IoTSharp.Net");
}

static void mqtt_online_callback(MQTTClient *c)
{
    rt_kprintf("Connect mqtt server success\n");
    rt_kprintf("Publish message: Hello,IotSharp! to topic: %s\n", pub_topic);
    lcd_show_string(1, 24, 24, "Welcome  Connect to IoTSharp.Net");
    mqtt_senddatetime(c);
}

static void mqtt_senddatetime(MQTTClient *c)
{
    time_t timer;
    time(&timer);
    struct tm *tm_info = localtime(&timer);
    char timebuf[26];
    strftime(timebuf, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    char application_message[256];
    snprintf(application_message, sizeof(application_message), "{\"startup_datetime\":\"%s\"}", timebuf);
    mq_publish(application_message);
}

static void mqtt_offline_callback(MQTTClient *c)
{
    rt_kprintf("Disconnect from mqtt server\n");
		lcd_show_string(1, 24, 24, "Disconnect from IoTSharp.Net");
}

/**
 * This function create and config a mqtt client.
 *
 * @param void
 *
 * @return none
 */
static void mq_start(void)
{
    /* init condata param by using MQTTPacket_connectData_initializer */
    MQTTPacket_connectData condata = MQTTPacket_connectData_initializer;
    static char cid[20] = {0};

    static int is_started = 0;
    if (is_started)
    {
        return;
    }
    /* config MQTT context param */
    {
        client.isconnected = 0;
        client.uri = MQTT_URI;

        /* generate the random client ID */
        rt_snprintf(cid, sizeof(cid), "iotsharp_edge_%d", rt_tick_get());
        rt_snprintf(sup_topic, sizeof(sup_topic), "%s", MQTT_SUBTOPIC);
			  rt_snprintf(pub_topic, sizeof(pub_topic), "%s", MQTT_PUBTOPIC);
        /* config connect param */
        memcpy(&client.condata, &condata, sizeof(condata));
        client.condata.clientID.cstring = cid;
        client.condata.keepAliveInterval = 60;
        client.condata.cleansession = 1;
        client.condata.username.cstring = MQTT_USERNAME;
        client.condata.password.cstring = MQTT_PASSWORD;

        /* config MQTT will param. */
        client.condata.willFlag = 0;
        client.condata.will.qos = 1;
        client.condata.will.retained = 0;
        client.condata.will.topicName.cstring =pub_topic;

        /* malloc buffer. */
        client.buf_size = client.readbuf_size = 1024;
        client.buf = malloc(client.buf_size);
        client.readbuf = malloc(client.readbuf_size);
        if (!(client.buf && client.readbuf))
        {
            rt_kprintf("no memory for MQTT client buffer!\n");
            goto _exit;
        }

        /* set event callback function */
        client.connect_callback = mqtt_connect_callback;
        client.online_callback = mqtt_online_callback;
        client.offline_callback = mqtt_offline_callback;

        /* set subscribe table and event callback */
        client.messageHandlers[0].topicFilter = sup_topic;
        client.messageHandlers[0].callback = mqtt_sub_callback;
        client.messageHandlers[0].qos = QOS1;

        /* set default subscribe event callback */
        client.defaultMessageHandler = mqtt_sub_default_callback;
    }

    /* run mqtt client */
    rt_kprintf("Start mqtt client and subscribe topic:%s\n", sup_topic);
    paho_mqtt_start(&client);
    is_started = 1;

_exit:
    return;
}

/**
 * This function publish message to specific mqtt topic.
 *
 * @param send_str publish message
 *
 * @return none
 */
static void mq_publish(const char *send_str)
{
    MQTTMessage message;
    const char *msg_str = send_str;
    const char *topic =  pub_topic;
    message.qos = QOS1;
    message.retained = 0;
    message.payload = (void *)msg_str;
    message.payloadlen = strlen(message.payload);
		rt_kprintf("topic:%s message%s",send_str);
    MQTTPublish(&client, topic, &message);

    return;
}
