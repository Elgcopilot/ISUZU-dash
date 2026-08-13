#include "csv_logger.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CSV_BUFFER_SIZE (256U * 1024U)
#define CSV_FLUSH_INTERVAL_SEC 10

static FILE *csv_file;
static unsigned char csv_buffer[CSV_BUFFER_SIZE];
static size_t csv_buffer_used;
static struct timespec last_flush;
static char csv_data_dir[512];
static int csv_local_year = -1;
static int csv_local_day = -1;

static const char csv_header[] =
    "device,car,datetime,lat,lon,speed,x,y,z,mag_x,mag_y,mag_z,"
    "gy_x,gy_y,gy_z,map,lambda,even,class,hr,duty_injection,"
    "battery_voltage,throttle,coolant_temp,fuel_flow_rate,"
    "fuel_rail_pressure,oil_pressure,oil_temp,rpm,sat,air_temp,"
    "speed_fl,speed_fr,speed_rl,speed_rr,drs\n";

static bool append_to_buffer(const char *data, size_t length) {
    if (length > sizeof(csv_buffer)) {
        fprintf(stderr, "CSV: Telemetry row exceeds RAM buffer\n");
        return false;
    }

    if (csv_buffer_used + length > sizeof(csv_buffer) && !csv_logger_flush()) {
        return false;
    }

    memcpy(csv_buffer + csv_buffer_used, data, length);
    csv_buffer_used += length;
    return true;
}

static void csv_escape(const char *value, char *escaped, size_t size) {
    size_t used = 0;

    if (size == 0) {
        return;
    }

    escaped[used++] = '"';
    while (*value != '\0' && used + 2 < size) {
        if (*value == '"' && used + 3 < size) {
            escaped[used++] = '"';
        }
        escaped[used++] = *value++;
    }
    if (used < size - 1) {
        escaped[used++] = '"';
    }
    escaped[used] = '\0';
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

static bool csv_open_session(const struct tm *local_time) {
    char path[sizeof(csv_data_dir) + 32];
    char filename[32];

    strftime(filename, sizeof(filename), "%y%m%d_%H:%M:%S.csv", local_time);
    snprintf(path, sizeof(path), "%s/%s", csv_data_dir, filename);

    csv_file = fopen(path, "w");
    if (csv_file == NULL) {
        perror("CSV: Failed to create telemetry file");
        return false;
    }

    setvbuf(csv_file, NULL, _IONBF, 0);
    csv_buffer_used = 0;
    clock_gettime(CLOCK_MONOTONIC, &last_flush);

    if (!append_to_buffer(csv_header, sizeof(csv_header) - 1) || !csv_logger_flush()) {
        fclose(csv_file);
        csv_file = NULL;
        return false;
    }

    csv_local_year = local_time->tm_year;
    csv_local_day = local_time->tm_yday;
    printf("CSV: Logging telemetry to %s\n", path);
    return true;
}

static bool csv_rotate_if_new_day(void) {
    time_t now = time(NULL);
    struct tm local_time;
    localtime_r(&now, &local_time);

    if (local_time.tm_year == csv_local_year && local_time.tm_yday == csv_local_day) {
        return true;
    }

    if (csv_file != NULL) {
        if (!csv_logger_flush()) {
            return false;
        }
        fclose(csv_file);
        csv_file = NULL;
    }

    return csv_open_session(&local_time);
}

bool csv_logger_init(const char *data_dir) {
    time_t now = time(NULL);
    struct tm local_time;

    if (csv_file != NULL) {
        return true;
    }

    if (mkdir(data_dir, 0755) != 0 && errno != EEXIST) {
        perror("CSV: Failed to create data directory");
        return false;
    }

    snprintf(csv_data_dir, sizeof(csv_data_dir), "%s", data_dir);
    localtime_r(&now, &local_time);
    return csv_open_session(&local_time);
}

bool csv_logger_append(const VehicleData *data, const Config *config) {
    char datetime[32];
    char escaped_car[64];
    char row[2048];
    struct timespec now;
    int row_length;

    if (csv_file == NULL || !csv_rotate_if_new_day()) {
        return false;
    }

    format_telemetry_datetime(data, datetime, sizeof(datetime));
        csv_escape(config->car_number, escaped_car, sizeof(escaped_car));

        row_length = snprintf(row, sizeof(row),
            "%d,%s,\"%s\",%.6f,%.6f,%.2f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,%.2f,%.2f,\"pt\",\"isuzu-omr\",,%.2f,"
            "%.2f,%d,%.1f,%.2f,%.2f,%.2f,%.1f,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,\n",
            config->device,
            escaped_car,
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

    if (row_length < 0 || (size_t)row_length >= sizeof(row)) {
        fprintf(stderr, "CSV: Failed to format telemetry row\n");
        return false;
    }

    if (!append_to_buffer(row, (size_t)row_length)) {
        return false;
    }

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec - last_flush.tv_sec >= CSV_FLUSH_INTERVAL_SEC) {
        return csv_logger_flush();
    }

    return true;
}

bool csv_logger_flush(void) {
    size_t written = 0;

    if (csv_file == NULL) {
        return false;
    }

    while (written < csv_buffer_used) {
        size_t result = fwrite(csv_buffer + written, 1, csv_buffer_used - written, csv_file);
        if (result == 0) {
            perror("CSV: Failed to flush telemetry buffer");
            if (written > 0) {
                memmove(csv_buffer, csv_buffer + written, csv_buffer_used - written);
                csv_buffer_used -= written;
            }
            return false;
        }
        written += result;
    }
    csv_buffer_used = 0;

    if (fflush(csv_file) != 0 || fsync(fileno(csv_file)) != 0) {
        perror("CSV: Failed to sync telemetry file");
        return false;
    }

    clock_gettime(CLOCK_MONOTONIC, &last_flush);
    return true;
}

void csv_logger_close(void) {
    if (csv_file != NULL) {
        if (!csv_logger_flush()) {
            fprintf(stderr, "CSV: Final telemetry flush failed\n");
        }
        fclose(csv_file);
        csv_file = NULL;
    }
}
