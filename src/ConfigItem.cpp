#include "ConfigItem.h"
#include "Scene.h"

std::vector<ConfigItem*> configRequests;

void parse_dollar(const char* line) {
    for (auto it = configRequests.begin(); it != configRequests.end(); ++it) {
        auto item = *it;

        size_t cmdlen = strlen(item->name());

        if (strncmp(line, item->name(), cmdlen) == 0 && line[cmdlen] == '=') {
            line += cmdlen + 1;
            item->got(line);

            current_scene->reDisplay();
            configRequests.erase(it);
            break;
        }
    }
}
