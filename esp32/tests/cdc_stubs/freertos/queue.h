#pragma once
#include <stddef.h>
typedef struct mock_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned,size_t);
int xQueueSend(QueueHandle_t,const void *,unsigned);
int xQueueReceive(QueueHandle_t,void *,unsigned);
int xQueuePeek(QueueHandle_t,void *,unsigned);
int xQueueOverwrite(QueueHandle_t,const void *);
void xQueueReset(QueueHandle_t);
