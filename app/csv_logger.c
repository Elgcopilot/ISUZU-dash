#include "csv_logger.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static FILE *csv_file;

static void csv_write_escaped(const char *value) {
    fputc('"', csv_file);
    while (*value != '\0') {
        if (*value == '"') {
            fputc('"', csv_file);
        }
        fputc(*value, csv_file);
        value++;
    }
    fputc('"', csv_file);
}

static void format_telemetry_datetime(const VehicleData *data, char *datetime, size_t size) {
    time_t now = time(NULL);
    struct tm tm_info;

    if (data->gps_data.time_valid) {
        gmtime_r(&now, &tm_info);
        snprintf(datetime, size, "%02d/%02d/%02d %02d:%02d:%02d",
                 tm_info.tm_mday, tm_info.tm_mon + 1, tm_info.tm_year % 100,
                 data->gps_data.utc_hour, data->gps_data.utc_minute,
                 data->gps_data.utc_second);
    } else {
        gmtime_r(&now, &tm_info);
        strftime(datetime, size, "%d/%m/%y %H:%M:%S", &tm_info);
    }
}

bool csv_logger_init(const char *data_dir) {
    char path[512];
    char filename[32];
    time_t now = time(NULL);
    struct tm tm_info;

    if (csv_file != NULL) {
        return true;
    }

    if (mkdir(data_dir, 0755) != 0 && errno != EEXIST) {
        perror("CSV: Failed to create data directory");
        return false;
    }

    localtime_r(&now, &tm_info);
    strftime(filename, sizeof(filename), "%y%m%d_%H:%M:%S.csv", &tm_info);
    snprintf(path, sizeof(path), "%s/%s", data_dir, filename);

    csv_file = fopen(path, "w");
    if (csv_file == NULL) {
        perror("CSV: Failed to create telemetry file");
        return false;
    }

    fprintf(csv_file,
            "device,car,datetime,lat,lon,speed,x,y,z,mag_x,mag_y,mag_z,"
            "gy_x,gy_y,gy_z,map,lambda,even,class,hr,duty_injection,"
            "battery_voltage,throttle,coolant_temp,fuel_flow_rate,"
            "fuel_rail_pressure,oil_pressure,oil_temp,rpm,sat,air_temp,"
            "speed_fl,speed_fr,speed_rl,speed_rr,drs\n");
    fflush(csv_file);
    printf("CSV: Logging telemetry to %s\n", path);
    return true;
}

bool csv_logger_append(const VehicleData *data, const Config *config) {
    char datetime[32];

    if (csv_file == NULL) {
        return false;
    }

    format_telemetry_datetime(data, datetime, sizeof(datetime));

    fprintf(csv_file, "%d,", config->device);
    csv_write_escaped(config->car_number);
    fprintf(csv_file,
            ",\"%s\",%.6f,%.6f,%.2f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,%.2f,%.2f,\"pt\",\"isuzu-omr\",,%.2f,"
            "%.2f,%d,%.1f,%.2f,%.2f,%.2f,%.1f,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,\n",
            datetime,
            data->gps_data.latitude,
            data->gps_data.longitude,
            data->speed_avg,
            (double)data->g_force_lat,
            (double)data->g_force_long,
            0.0,
            (double)data->mag_x,
            (double)data->mag_y,
            (double)data->mag_z,
            (double)data->gyro_x,
            (double)data->gyro_y,
            (double)data->gyro_z,
            (double)data->boost,
            (double)data->lambda,
            (double)data->duty_injection,
            (double)data->battery_voltage,
            data->pedal_pos,
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
            (double)data->speed_rr);

    if (ferror(csv_file)) {
        perror("CSV: Failed to write telemetry");
        clearerr(csv_file);
        return false;
    }

    fflush(csv_file);
    return true;
}

void csv_logger_close(void) {
    if (csv_file != NULL) {
        fflush(csv_file);
        fclose(csv_file);
        csv_file = NULL;
    }
}
