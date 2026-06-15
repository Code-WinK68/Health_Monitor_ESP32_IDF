#ifndef TASKS_PIPELINE_H
#define TASKS_PIPELINE_H

#include "app_config.h"   // raw_record_t, csv_batch_t, Queue handles

#ifdef __cplusplus
extern "C" {
#endif

// task_lo_poll đã bỏ — task_collector tự đọc LO + ADC đồng thời
// để đảm bảo ECG và PCG được lấy mẫu tại cùng một thời điểm

void task_collector(void *pvParameters);
void task_csv_parser(void *pvParameters);
void task_sd_flash(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // TASKS_PIPELINE_H