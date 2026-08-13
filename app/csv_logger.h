#ifndef CSV_LOGGER_H
#define CSV_LOGGER_H

#include <stdbool.h>
#include "signals.h"
#include "config_parser.h"

bool csv_logger_init(const char *data_dir);
bool csv_logger_append(const VehicleData *data, const Config *config);
bool csv_logger_flush(void);
void csv_logger_close(void);

#endif // CSV_LOGGER_H
