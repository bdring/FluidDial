#include "ConfigItem.h"
#include "Scene.h"
#include "System.h"
#include "FileParser.h"  // json_in_progress()

std::vector<ConfigItem*> configRequests;

static constexpr uint32_t CONFIG_REQUEST_RETRY_MS  = 500;
static constexpr int      CONFIG_REQUEST_MAX_TRIES = 4;
static uint32_t           configRequestSentMs      = 0;
static int                configRequestTries       = 0;

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
    if (configRequests.empty() || (uint32_t)(millis() - configRequestSentMs) < CONFIG_REQUEST_RETRY_MS) {
        return;
    }
    // The reply would land in the middle of a JSON document being received
    if (json_in_progress()) {
        return;
    }
    if (configRequestTries >= CONFIG_REQUEST_MAX_TRIES) {
        // No answer is coming. Drop it (it stays !known()) so the queue can drain.
        configRequests.erase(configRequests.begin());
        send_next_config_request();
        return;
    }
    send_config_request();
}

// FluidNC rejected a command. If a config query is outstanding, the error is
// its answer: this machine doesn't have that setting, so don't retry it.
void config_request_failed() {
    if (configRequests.empty() || configRequestTries == 0) {
        return;
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
