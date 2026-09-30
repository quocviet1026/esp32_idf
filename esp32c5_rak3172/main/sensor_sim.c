/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

#include <stdint.h>

#include "esp_random.h"

#include "sensor_sim.h"

/* Gia lap nhiet do dao dong quanh 25.0 +/- 5.0 do C. esp_random() la RNG phan
 * cung cua ESP-IDF (khong can seed), du tot cho muc dich gia lap nay. */
float sensor_sim_read(void)
{
    uint32_t r = esp_random() % 1000;   /* 0..999 */
    return 20.0f + (float)r / 100.0f;   /* 20.00 .. 29.99 */
}
