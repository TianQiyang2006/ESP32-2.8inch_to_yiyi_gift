#pragma once
// Outbound HTTPS requests: weather and reading lookups.
//
// All of these run on the IO worker task. Every request validates the TLS chain
// against the root bundle in gift_ca.h, so a service presenting an
// untrusted certificate is refused rather than silently accepted.
#include "jobs.h"
#include <ArduinoJson.h>

namespace Net {
void fetchReading(const Jobs::Job &job,Jobs::Result &result);

// Fills result.weather from UApi's Chinese city query. Missing readings stay NAN so the UI can
// show "--" instead of inventing a value.
void fetchWeather(const Jobs::Job &job, Jobs::Result &result);

// GETs a URL and parses the body as JSON. Shared by both callers above.
bool getJson(const String &url, JsonDocument &json, String &error, int *httpStatus = nullptr);

}
