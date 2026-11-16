#include "jobs.h"

namespace Jobs {

QueueHandle_t jobQueue = nullptr;
QueueHandle_t resultQueue = nullptr;
SemaphoreHandle_t ioMutex = nullptr;

bool begin() {
    if (jobQueue && resultQueue && ioMutex) return true;
    QueueHandle_t jobs = xQueueCreate(QueueDepth, sizeof(Job*));
    QueueHandle_t results = xQueueCreate(QueueDepth, sizeof(Result*));
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    if (!jobs || !results || !mutex) {
        if (jobs) vQueueDelete(jobs);
        if (results) vQueueDelete(results);
        if (mutex) vSemaphoreDelete(mutex);
        return false;
    }
    // Publish handles together only after every allocation succeeds.
    jobQueue = jobs;
    resultQueue = results;
    ioMutex = mutex;
    return true;
}

bool submit(Job *job) {
    if (jobQueue && xQueueSend(jobQueue, &job, 0) == pdTRUE) return true;
    delete job;
    return false;
}

}
