#pragma once
// The asynchronous job pipeline's types and queues.
//
// The IO worker task owns every blocking operation (HTTP).
// The UI thread submits a Job and later collects a Result, so no view ever blocks
// on IO. These types used to sit in desk.cpp's anonymous namespace, which gave them
// internal linkage and made them unusable from any other module — the first
// obstacle to splitting the service layer out.
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <stdint.h>
#include <stdlib.h>
#include "app_state.h"

namespace Jobs {

enum class JobKind { Weather, Reading };

struct Job {
    JobKind kind;
    String text;
    uint32_t generation = 0;
};

struct Result {
    JobKind kind;
    bool success = false;
    String error, text;
    uint32_t generation = 0;
    Weather weather;
};

// Depth 4 is enough for one in-flight weather/reading request.
constexpr size_t QueueDepth = 4;

extern QueueHandle_t jobQueue;
extern QueueHandle_t resultQueue;
// Serialises the loop thread's blocking binary screenshot frame (Display::sendFrame)
// with the IO worker's HTTPS/TLS requests, whose stack logs would otherwise be
// interleaved into that frame on the shared USB serial link. It protects the serial
// transport, not any shared data structure: the loop thread takes it for the
// screenshot while the IO worker holds it around Net::fetch*.
extern SemaphoreHandle_t ioMutex;

// Creates the queues and mutex. Call once during setup, before starting the worker.
bool begin();

// Queues a job. Takes ownership and deletes it when the queue is full.
bool submit(Job *job);

}
