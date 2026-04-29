/*
 * MQTT Client - Implementation
 */

#include "mqtt_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <MQTTClient.h>

#define QOS         0
#define TIMEOUT     1000L
#define KEEPALIVE   60

static MQTTClient client = NULL;
static char topic[128];
static bool connected = false;

// Connection lost callback
void connection_lost(void *context, char *cause) {
    printf("MQTT: Connection lost - %s\n", cause ? cause : "unknown");
    connected = false;
}

// Message delivered callback
void message_delivered(void *context, MQTTClient_deliveryToken dt) {
    // Silent confirmation
}

bool mqtt_init(const Config *config) {
    char address[256];
    char client_id[64];
    
    FILE *log = fopen("/tmp/mqtt_debug.log", "a");
    
    // Build MQTT broker address
    snprintf(address, sizeof(address), "tcp://%s:%d", config->server, config->port);
    snprintf(client_id, sizeof(client_id), "isuzu_device_%d", config->device);
    
    if (log) {
        fprintf(log, "Creating MQTT client: %s (ID: %s)\n", address, client_id);
        fflush(log);
    }
    
    // Create MQTT client
    int rc = MQTTClient_create(&client, address, client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        if (log) {
            fprintf(log, "MQTT: Failed to create client, return code %d\n", rc);
            fclose(log);
        }
        printf("MQTT: Failed to create client, return code %d\n", rc);
        return false;
    }
    
    // Set callbacks
    MQTTClient_setCallbacks(client, NULL, connection_lost, NULL, message_delivered);
    
    // Connection options
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = KEEPALIVE;
    conn_opts.cleansession = 1;
    conn_opts.username = config->mqtt_user;
    conn_opts.password = config->mqtt_password;
    
    // Connect to broker
    if (log) {
        fprintf(log, "MQTT: Connecting to %s with user=%s...\n", address, config->mqtt_user);
        fflush(log);
    }
    printf("MQTT: Connecting to %s...\n", address);
    rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        if (log) {
            fprintf(log, "MQTT: Failed to connect, return code %d\n", rc);
            fclose(log);
        }
        printf("MQTT: Failed to connect, return code %d\n", rc);
        return false;
    }
    
    // Build topic
    snprintf(topic, sizeof(topic), "/isuzu/omr/%d", config->device);
    
    connected = true;
    
    if (log) {
        fprintf(log, "MQTT: Connected successfully to %s\n", address);
        fprintf(log, "MQTT: Publishing to topic: %s\n", topic);
        fclose(log);
    }
    
    printf("MQTT: Connected successfully to %s\n", address);
    printf("MQTT: Publishing to topic: %s\n", topic);
    
    return true;
}

bool mqtt_publish_telemetry(const VehicleData *data, const Config *config) {
    if (!client || !connected) {
        return false;
    }
    
    // Build datetime string from GPS time (DD/MM/YY HH:MM:SS)
    char datetime[32];
    if (data->gps_data.time_valid) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        snprintf(datetime, sizeof(datetime), "%02d/%02d/%02d %02d:%02d:%02d",
                 tm_info->tm_mday, tm_info->tm_mon + 1, tm_info->tm_year % 100,
                 data->gps_data.utc_hour, data->gps_data.utc_minute, data->gps_data.utc_second);
    } else {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        snprintf(datetime, sizeof(datetime), "%02d/%02d/%02d %02d:%02d:%02d",
                 tm_info->tm_mday, tm_info->tm_mon + 1, tm_info->tm_year % 100,
                 tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    }
    
    // Build JSON payload (large buffer for complete telemetry)
    char payload[2048];
    snprintf(payload, sizeof(payload),
        "{"
        "\"device\":%d,"
        "\"car\":\"%s\","
        "\"datetime\":\"%s\","
        "\"lat\":%.6f,"
        "\"lon\":%.6f,"
        "\"speed\":%.2f,"
        "\"x\":%.6f,"
        "\"y\":%.6f,"
        "\"z\":%.6f,"
        "\"mag_x\":%.6f,"
        "\"mag_y\":%.6f,"
        "\"mag_z\":%.6f,"
        "\"gy_x\":%.6f,"
        "\"gy_y\":%.6f,"
        "\"gy_z\":%.6f,"
        "\"map\":%.2f,"
        "\"lambda\":%.2f,"
        "\"even\":\"pt\","
        "\"class\":\"isuzu-omr\","
        "\"hr\":null,"
        "\"duty_injection\":%.2f,"
        "\"coolant_temp\":%.1f,"
        "\"fuel_flow_rate\":%.2f,"
        "\"fuel_rail_pressure\":%.2f,"
        "\"oil_pressure\":%.2f,"
        "\"oil_temp\":%.1f,"
        "\"rpm\":%d,"
        "\"sat\":%d,"
        "\"air_temp\":%d,"
        "\"speed_fl\":%.2f,"
        "\"speed_fr\":%.2f,"
        "\"speed_rl\":%.2f,"
        "\"speed_rr\":%.2f,"
        "\"drs\":null"
        "}",
        config->device,
        config->car_number,
        datetime,
        data->gps_data.latitude,
        data->gps_data.longitude,
        data->speed_avg,
        (double)data->g_force_lat,
        (double)data->g_force_long,
        0.0,  // Z-axis (not used for racing telemetry)
        (double)data->mag_x,
        (double)data->mag_y,
        (double)data->mag_z,
        (double)data->gyro_x,
        (double)data->gyro_y,
        (double)data->gyro_z,
        (double)data->boost / 100.0,  // Convert kPa to bar-ish scale
        (double)data->lambda,
        (double)data->duty_injection,
        (double)data->coolant_temp,
        (double)data->fuel_rate,
        (double)data->fuel_rail_press,
        (double)data->oil_pressure,
        (double)data->oil_temp,
        data->rpm,
        data->gps_data.satellites_used,
        data->intake_temp,
        (double)data->speed_fl,
        (double)data->speed_fr,
        (double)data->speed_rl,
        (double)data->speed_rr
    );
    
    // Publish message
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    
    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("MQTT: Failed to publish message, return code %d\n", rc);
        connected = false;
        return false;
    }
    
    // QoS 0: Fire and forget, no wait for completion (needed for 25Hz rate)
    return true;
}

bool mqtt_is_connected(void) {
    return connected && (client != NULL) && MQTTClient_isConnected(client);
}

void mqtt_cleanup(void) {
    if (client) {
        if (connected) {
            MQTTClient_disconnect(client, TIMEOUT);
        }
        MQTTClient_destroy(&client);
        client = NULL;
        connected = false;
    }
    printf("MQTT: Cleanup completed\n");
}
