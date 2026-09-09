#include "ConfigItem.h"
#include "Scene.h"
#include "System.h"
#include "FileParser.h"  // json_in_progress()

std::vector<ConfigItem*> configRequests;

static constexpr uint32_t CONFIG_REQUEST_RETRY_MS = 500;

// Bound the retries. FluidNC rejects a setting it doesn't have (an axis that
// isn't configured, a key this firmware build lacks) with a bare "error:3",
// which never matches parse_dollar(), so the item never leaves the queue. An
// unbounded retry loop then re-sends it every 500 ms forever. That floods the
// link, blocks every request queued behind it (we only ever send front()), and
// splices error responses into an in-flight $File/SendJSON document -- which is
// what empties the macro list. Give up on the item instead and move on.
static constexpr int CONFIG_REQUEST_MAX_TRIES = 4;

static uint32_t configRequestSentMs = 0;
static int      configRequestTries  = 0;

static bool can_send_config_request() {
    // ensure config requests are sent only when a job is not running - as it won't be processed otherwise
    return state == Idle || state == Alarm;
}

static void send_config_request() {
    if (configRequests.empty() || !can_send_config_request()) {
        return;
    }
    configRequests.front()->send_request();
    configRequestSentMs = millis();
    ++configRequestTries;
}

// Move on to a different request. The try budget is per item, so reset it.
static void send_next_config_request() {
    configRequestTries = 0;
    send_config_request();
}

void ConfigItem::init() {
    _known = false;

    for (auto it = configRequests.begin(); it != configRequests.end(); ++it) {
        if (*it == this) {
            configRequests.erase(it);
            break;
        }
    }

    bool start_request = configRequests.empty();
    configRequests.push_back(this);
    if (start_request) {
        send_next_config_request();
    }
}

void clear_config_requests() {
    configRequests.clear();
    configRequestSentMs = 0;
    configRequestTries  = 0;
}

void service_config_requests() {
    if (configRequests.empty()) {
        return;
    }
    if ((uint32_t)(millis() - configRequestSentMs) < CONFIG_REQUEST_RETRY_MS) {
        return;
    }
    // Never transmit while a JSON document is streaming in. FluidNC answers on
    // the same link, so the reply (or its error) lands in the middle of the
    // document and derails the parser mid-object.
    if (json_in_progress()) {
        return;
    }
    if (configRequestTries >= CONFIG_REQUEST_MAX_TRIES) {
        // FluidNC is never going to answer this one. Drop it -- it stays
        // !known(), which callers already handle -- so the rest of the queue
        // can drain instead of being stuck behind it.
        configRequests.erase(configRequests.begin());
        send_next_config_request();
        return;
    }
    send_config_request();
}

// Called from show_error() when FluidNC rejects a command and no JSON document
// is in flight. If a config query is outstanding, that error is almost certainly
// its answer: the setting doesn't exist on this machine (an axis that isn't
// configured, a key this firmware lacks). Retrying cannot change that, so drop
// it now rather than burning the whole retry budget at 500 ms a go -- twelve
// homing items would otherwise spend ~24 s flooding the link with error:3.
void config_request_failed() {
    if (configRequests.empty() || configRequestTries == 0) {
        return;  // nothing outstanding, so the error belongs to someone else
    }
    configRequests.erase(configRequests.begin());
    send_next_config_request();
}

void parse_dollar(const char* line) {
    for (auto it = configRequests.begin(); it != configRequests.end(); ++it) {
        auto item = *it;

        size_t cmdlen = strlen(item->name());

        if (strncmp(line, item->name(), cmdlen) == 0 && line[cmdlen] == '=') {
            line += cmdlen + 1;
            item->got(line);

            request_redisplay();
            configRequests.erase(it);
            send_next_config_request();
            break;
        }
    }
}
