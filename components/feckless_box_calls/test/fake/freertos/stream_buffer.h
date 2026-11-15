#pragma once
#include "freertos/FreeRTOS.h"
typedef struct fake_sb *StreamBufferHandle_t;
typedef struct { uint8_t opaque[48]; } StaticStreamBuffer_t;
typedef void (*StreamBufferCallbackFunction_t)(StreamBufferHandle_t, BaseType_t, BaseType_t *);
StreamBufferHandle_t xStreamBufferGenericCreateStatic(size_t size, size_t trigger, BaseType_t is_msg, uint8_t *store,
                                                      StaticStreamBuffer_t *ctl, StreamBufferCallbackFunction_t s,
                                                      StreamBufferCallbackFunction_t r);
void vStreamBufferDelete(StreamBufferHandle_t h);
size_t xStreamBufferSend(StreamBufferHandle_t h, const void *p, size_t n, TickType_t t);
size_t xStreamBufferReceive(StreamBufferHandle_t h, void *p, size_t n, TickType_t t);
size_t xStreamBufferBytesAvailable(StreamBufferHandle_t h);
size_t xStreamBufferSpacesAvailable(StreamBufferHandle_t h);
BaseType_t xStreamBufferIsEmpty(StreamBufferHandle_t h);
BaseType_t xStreamBufferIsFull(StreamBufferHandle_t h);
BaseType_t xStreamBufferReset(StreamBufferHandle_t h);
