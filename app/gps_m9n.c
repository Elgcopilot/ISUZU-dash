#include "gps_m9n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <stdint.h>

#define GPS_NAV_RATE_MS 40

#define UBX_CLASS_CFG 0x06
#define UBX_CFG_MSG   0x01
#define UBX_CFG_RATE  0x08
#define UBX_CLASS_NMEA 0xF0

static int gps_fd = -1;
static char line_buffer[256];
static int buf_pos = 0;

static bool gps_write_all(const unsigned char *data, size_t length) {
    size_t written = 0;

    while (written < length) {
        ssize_t result = write(gps_fd, data + written, length - written);
        if (result > 0) {
            written += (size_t)result;
        } else if (result < 0 && (errno == EAGAIN || errno == EINTR)) {
            usleep(1000);
        } else {
            return false;
        }
    }
    return tcdrain(gps_fd) == 0;
}

static bool gps_send_ubx(uint8_t message_class, uint8_t message_id,
                         const uint8_t *payload, uint16_t payload_length) {
    unsigned char packet[64];
    size_t packet_length = (size_t)payload_length + 8;
    uint8_t checksum_a = 0;
    uint8_t checksum_b = 0;

    if (packet_length > sizeof(packet)) return false;

    packet[0] = 0xB5;
    packet[1] = 0x62;
    packet[2] = message_class;
    packet[3] = message_id;
    packet[4] = (uint8_t)payload_length;
    packet[5] = (uint8_t)(payload_length >> 8);
    memcpy(packet + 6, payload, payload_length);

    for (size_t i = 2; i < packet_length - 2; i++) {
        checksum_a = (uint8_t)(checksum_a + packet[i]);
        checksum_b = (uint8_t)(checksum_b + checksum_a);
    }
    packet[packet_length - 2] = checksum_a;
    packet[packet_length - 1] = checksum_b;
    return gps_write_all(packet, packet_length);
}

static bool gps_set_nmea_uart1_rate(uint8_t message_id, uint8_t rate) {
    // CFG-MSG rates: I2C, UART1, UART2, USB, SPI, reserved.
    uint8_t payload[8] = {UBX_CLASS_NMEA, message_id, 0, rate, 0, 0, 0, 0};
    return gps_send_ubx(UBX_CLASS_CFG, UBX_CFG_MSG, payload, sizeof(payload));
}

static bool gps_configure_25hz(void) {
    // RMC contains position, validity, speed, and UTC and is the only 25 Hz
    // sentence. GSA/GSV remain at 1 Hz for satellite status. Disabling the
    // other NMEA sentences keeps output safely below 38400-baud capacity.
    static const struct {
        uint8_t id;
        uint8_t rate;
    } nmea_rates[] = {
        {0x00, 0},  // GGA
        {0x01, 0},  // GLL
        {0x02, 25}, // GSA
        {0x03, 25}, // GSV
        {0x04, 1},  // RMC
        {0x05, 0},  // VTG
        {0x08, 0},  // ZDA
        {0x0D, 0},  // GNS
    };

    bool ok = true;
    for (size_t i = 0; i < sizeof(nmea_rates) / sizeof(nmea_rates[0]); i++) {
        if (!gps_set_nmea_uart1_rate(nmea_rates[i].id, nmea_rates[i].rate)) ok = false;
        usleep(20000);
    }

    uint8_t rate_payload[6] = {
        (uint8_t)GPS_NAV_RATE_MS, (uint8_t)(GPS_NAV_RATE_MS >> 8),
        1, 0, // one navigation cycle per measurement
        0, 0  // UTC time reference
    };
    if (!gps_send_ubx(UBX_CLASS_CFG, UBX_CFG_RATE, rate_payload, sizeof(rate_payload))) ok = false;
    return ok;
}

bool gps_init(void) {
    gps_fd = open("/dev/ttyS4", O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (gps_fd < 0) {
        printf("Failed to open GPS UART4: %s\n", strerror(errno));
        return false;
    }
    
    // Flush any existing data
    tcflush(gps_fd, TCIOFLUSH);

    struct termios tty;
    if (tcgetattr(gps_fd, &tty) != 0) {
        printf("Error getting GPS serial attributes: %s\n", strerror(errno));
        close(gps_fd);
        gps_fd = -1;
        return false;
    }

    // Set 38400 baud, 8N1
    cfsetospeed(&tty, B38400);
    cfsetispeed(&tty, B38400);
    
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
    tty.c_cflag |= (CLOCAL | CREAD);                // ignore modem controls, enable reading
    tty.c_cflag &= ~(PARENB | PARODD);              // no parity
    tty.c_cflag &= ~CSTOPB;                         // 1 stop bit
    tty.c_cflag &= ~CRTSCTS;                        // no hardware flow control

    tty.c_lflag = 0;                                // no signaling chars, no echo, no canonical processing
    tty.c_oflag = 0;                                // no remapping, no delays
    tty.c_cc[VMIN]  = 0;                            // non-blocking read
    tty.c_cc[VTIME] = 1;                            // 0.1 seconds read timeout

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);         // no software flow control
    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL);

    if (tcsetattr(gps_fd, TCSANOW, &tty) != 0) {
        printf("Error setting GPS serial attributes: %s\n", strerror(errno));
        close(gps_fd);
        gps_fd = -1;
        return false;
    }

    if (gps_configure_25hz()) {
        printf("GPS M9N initialized on UART4 (38400 baud, 25 Hz RMC)\n");
    } else {
        printf("GPS M9N initialized, but 25 Hz configuration failed\n");
    }
    return true;
}

void gps_close(void) {
    if (gps_fd >= 0) {
        close(gps_fd);
        gps_fd = -1;
    }
}

// Parse NMEA checksum
static bool nmea_checksum_valid(const char *sentence) {
    if (sentence[0] != '$') return false;
    
    const char *star = strchr(sentence, '*');
    if (!star) return false;
    
    int calc_checksum = 0;
    for (const char *p = sentence + 1; p < star; p++) {
        calc_checksum ^= *p;
    }
    
    int recv_checksum = (int)strtol(star + 1, NULL, 16);
    return calc_checksum == recv_checksum;
}

// Parse GSV sentence: $GPGSV,3,1,12,01,52,112,45,02,23,045,40,...*CS
static void parse_gsv(const char *sentence, GPSData *gps_data) {
    if (!nmea_checksum_valid(sentence)) return;
    
    char *tokens[64];
    char buf[256];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    int token_count = 0;
    char *token = strtok(buf, ",*");
    while (token && token_count < 64) {
        tokens[token_count++] = token;
        token = strtok(NULL, ",*");
    }
    
    if (token_count < 4) return;
    
    // tokens[3] = total satellites in view
    int total_sats = atoi(tokens[3]);
    if (total_sats > MAX_SATELLITES) total_sats = MAX_SATELLITES;
    gps_data->num_satellites = total_sats;
    
    // Parse satellite data (starts at token 4, groups of 4)
    for (int i = 4; i + 3 < token_count; i += 4) {
        int prn = atoi(tokens[i]);
        int elev = atoi(tokens[i + 1]);
        int azim = atoi(tokens[i + 2]);
        int cn0 = (tokens[i + 3][0] != '\0') ? atoi(tokens[i + 3]) : 0;
        
        // Find existing satellite or add new
        int sat_idx = -1;
        for (int j = 0; j < MAX_SATELLITES; j++) {
            if (gps_data->sats[j].prn == prn) {
                sat_idx = j;
                break;
            }
            if (gps_data->sats[j].prn == 0 && sat_idx == -1) {
                sat_idx = j;
            }
        }
        
        if (sat_idx >= 0) {
            gps_data->sats[sat_idx].prn = prn;
            gps_data->sats[sat_idx].elevation = elev;
            gps_data->sats[sat_idx].azimuth = azim;
            gps_data->sats[sat_idx].cn0 = cn0;
        }
    }
}

// Parse GSA sentence to know which satellites are used
// $GPGSA,A,3,01,02,03,04,05,06,07,08,09,10,11,12,1.0,1.0,1.0*30
static void parse_gsa(const char *sentence, GPSData *gps_data) {
    if (!nmea_checksum_valid(sentence)) return;
    
    // Clear all "used" flags first
    for (int i = 0; i < MAX_SATELLITES; i++) {
        gps_data->sats[i].used = false;
    }
    
    char *tokens[64];
    char buf[256];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    int token_count = 0;
    char *token = strtok(buf, ",*");
    while (token && token_count < 64) {
        tokens[token_count++] = token;
        token = strtok(NULL, ",*");
    }
    
    if (token_count < 3) return;
    
    // tokens[3] to tokens[14] contain PRNs of satellites used
    gps_data->satellites_used = 0;
    for (int i = 3; i < 15 && i < token_count; i++) {
        if (tokens[i][0] == '\0') continue;
        
        int prn = atoi(tokens[i]);
        if (prn == 0) continue;
        
        // Mark this satellite as used
        for (int j = 0; j < MAX_SATELLITES; j++) {
            if (gps_data->sats[j].prn == prn) {
                gps_data->sats[j].used = true;
                gps_data->satellites_used++;
                break;
            }
        }
    }
}

// Parse GGA sentence for position and time
// $GPGGA,hhmmss.ss,ddmm.mmmm,N,dddmm.mmmm,E,fix,sats,hdop,alt,M,geoid,M,,*CS
static void parse_gga(const char *sentence, GPSData *gps_data) {
    if (!nmea_checksum_valid(sentence)) return;
    
    char *tokens[16];
    char buf[256];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    int token_count = 0;
    char *token = strtok(buf, ",*");
    while (token && token_count < 16) {
        tokens[token_count++] = token;
        token = strtok(NULL, ",*");
    }
    
    if (token_count < 10) return;
    
    // Parse time FIRST (token 1: hhmmss or hhmmss.ss)
    // GPS can provide time before position fix
    gps_data->time_valid = false;
    if (tokens[1][0] != '\0' && strlen(tokens[1]) >= 6) {
        double utc_time = atof(tokens[1]);
        int hour = ((int)utc_time / 10000);
        int minute = ((int)utc_time / 100) % 100;
        int second = (int)utc_time % 100;
        
        // Validate time ranges
        if (hour >= 0 && hour < 24 && minute >= 0 && minute < 60 && second >= 0 && second < 60) {
            gps_data->utc_hour = hour;
            gps_data->utc_minute = minute;
            gps_data->utc_second = second;
            gps_data->time_valid = true;
        }
    }
    
    // Parse fix quality (token 6)
    int fix = atoi(tokens[6]);
    gps_data->fix_valid = (fix > 0);
    
    if (!gps_data->fix_valid) return;
    
    // Parse latitude (token 2: ddmm.mmmm, token 3: N/S)
    if (tokens[2][0] != '\0' && tokens[3][0] != '\0') {
        double lat_raw = atof(tokens[2]);
        int lat_deg = (int)(lat_raw / 100);
        double lat_min = lat_raw - (lat_deg * 100);
        gps_data->latitude = lat_deg + (lat_min / 60.0);
        if (tokens[3][0] == 'S') gps_data->latitude = -gps_data->latitude;
    }
    
    // Parse longitude (token 4: dddmm.mmmm, token 5: E/W)
    if (tokens[4][0] != '\0' && tokens[5][0] != '\0') {
        double lon_raw = atof(tokens[4]);
        int lon_deg = (int)(lon_raw / 100);
        double lon_min = lon_raw - (lon_deg * 100);
        gps_data->longitude = lon_deg + (lon_min / 60.0);
        if (tokens[5][0] == 'W') gps_data->longitude = -gps_data->longitude;
    }
}

// Parse RMC sentence for ground speed
// $GNRMC,hhmmss.ss,A,ddmm.mmmm,N,dddmm.mmmm,E,speed_knots,course,date,...*CS
static bool parse_rmc(const char *sentence, GPSData *gps_data) {
    if (!nmea_checksum_valid(sentence)) return false;

    char *tokens[16];
    char buf[256];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int token_count = 0;
    char *token = strtok(buf, ",*");
    while (token && token_count < 16) {
        tokens[token_count++] = token;
        token = strtok(NULL, ",*");
    }

    if (token_count < 8) return false;

    gps_data->fix_valid = tokens[2][0] == 'A';
    if (!gps_data->fix_valid || tokens[1][0] == '\0' || tokens[3][0] == '\0' ||
        tokens[4][0] == '\0' || tokens[5][0] == '\0' || tokens[6][0] == '\0' ||
        tokens[7][0] == '\0') {
        gps_data->speed_valid = false;
        return true;
    }

    double utc_time = atof(tokens[1]);
    gps_data->utc_hour = (int)utc_time / 10000;
    gps_data->utc_minute = ((int)utc_time / 100) % 100;
    gps_data->utc_second = (int)utc_time % 100;
    gps_data->time_valid = gps_data->utc_hour >= 0 && gps_data->utc_hour < 24 &&
                           gps_data->utc_minute >= 0 && gps_data->utc_minute < 60 &&
                           gps_data->utc_second >= 0 && gps_data->utc_second < 60;

    double lat_raw = atof(tokens[3]);
    int lat_degrees = (int)(lat_raw / 100.0);
    gps_data->latitude = lat_degrees + (lat_raw - lat_degrees * 100.0) / 60.0;
    if (tokens[4][0] == 'S') gps_data->latitude = -gps_data->latitude;

    double lon_raw = atof(tokens[5]);
    int lon_degrees = (int)(lon_raw / 100.0);
    gps_data->longitude = lon_degrees + (lon_raw - lon_degrees * 100.0) / 60.0;
    if (tokens[6][0] == 'W') gps_data->longitude = -gps_data->longitude;

    gps_data->speed_kmh = atof(tokens[7]) * 1.852;
    gps_data->speed_valid = true;
    return true;
}

bool gps_update(GPSData *gps_data) {
    bool navigation_updated = false;
    if (gps_fd < 0) return false;
    
    char byte;
    while (read(gps_fd, &byte, 1) == 1) {
        if (byte == '\n' || byte == '\r') {
            if (buf_pos > 0) {
                line_buffer[buf_pos] = '\0';
                
                // Process complete NMEA sentence
                if (strstr(line_buffer, "$G") && strstr(line_buffer, "GSV")) {
                    parse_gsv(line_buffer, gps_data);
                } else if (strstr(line_buffer, "$G") && strstr(line_buffer, "GSA")) {
                    parse_gsa(line_buffer, gps_data);
                } else if (strstr(line_buffer, "$G") && strstr(line_buffer, "GGA")) {
                    parse_gga(line_buffer, gps_data);
                } else if (strstr(line_buffer, "$G") && strstr(line_buffer, "RMC")) {
                    if (parse_rmc(line_buffer, gps_data)) navigation_updated = true;
                }
                
                buf_pos = 0;
            }
        } else if (buf_pos < sizeof(line_buffer) - 1) {
            line_buffer[buf_pos++] = byte;
        }
    }
    return navigation_updated;
}
