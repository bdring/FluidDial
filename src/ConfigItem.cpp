#include "ConfigItem.h"
#include "Scene.h"
#include "System.h"

std::vector<ConfigItem*> configRequests;

static constexpr uint32_t CONFIG_REQUEST_RETRY_MS = 500;
static uint32_t           configRequestSentMs     = 0;

static void send_next_config_request() {
    if (configRequests.empty()) {
        return;
    }
    configRequests.front()->send_request();
    configRequestSentMs = millis();
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
}

void service_config_requests() {
    if (!configRequests.empty() &&
        (uint32_t)(millis() - configRequestSentMs) >= CONFIG_REQUEST_RETRY_MS) {
        send_next_config_request();
    }
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
