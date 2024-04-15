#include <vector>
#include <string>
#include <cstring>
#include "FluidNCModel.h"

class ConfigItem;
extern std::vector<ConfigItem*> configRequests;

class ConfigItem {
private:
    const char* _name;
    bool        _known;

public:
    ConfigItem(const char* name) : _name(name), _known(false) {}

    virtual void set(const char* s) = 0;
    const char*  name() { return _name; }
    bool         known() { return _known; }
    void         init() {
        _known = false;
        configRequests.push_back(this);
        send_line(_name);
    }
    void got(const char* s) {
        _known = true;
        set(s);
    }
};

class IntConfigItem : public ConfigItem {
private:
    int _value;

public:
    IntConfigItem(const char* name) : ConfigItem(name) {}
    int  get() { return _value; }
    void set(const char* s) { _value = atoi(s); }
};

class PosConfigItem : public ConfigItem {
private:
    pos_t _value;

public:
    PosConfigItem(const char* name) : ConfigItem(name) {}
    pos_t get() { return _value; }
    void  set(const char* s) { _value = atopos(s); }
};

class StringConfigItem : public ConfigItem {
private:
    std::string _value;

public:
    StringConfigItem(const char* name) : ConfigItem(name) {}
    std::string get() { return _value; }
    void        set(const char* s) { _value = s; }
};

class BoolConfigItem : public ConfigItem {
private:
    bool _value;

public:
    BoolConfigItem(const char* name) : ConfigItem(name) {}
    bool get() { return _value; }
    void set(const char* s) { _value = strcmp(s, "true") == 0; }
};

void parse_dollar(const char* line);
