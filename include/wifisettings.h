#ifndef GULINUX_WIFI_SETTINGS
#define GULINUX_WIFI_SETTINGS

#include <vector>
#include <Preferences.h>
#include <WString.h>
#include <FS.h>

#ifndef WIFIMANAGER_MAX_ESSID_PSK_SIZE
#define WIFIMANAGER_MAX_ESSID_PSK_SIZE 256
#endif

namespace GuLinux {
class WiFiSettings {
public:
    struct WiFiStation {
        char essid[WIFIMANAGER_MAX_ESSID_PSK_SIZE] = {0};
        char psk[WIFIMANAGER_MAX_ESSID_PSK_SIZE] = {0};
        operator bool() const { return valid(); }
        bool valid() const { return !empty(); }
        bool empty() const;
        bool open() const;
    };
    WiFiSettings(Preferences &preferences, FS &fs, const char *defaultHostname="ESP32", bool appendMacSuffix=true, uint16_t maxStations=5, bool reconnectByDefault=false, uint16_t defaultRetries=2);
    void setup();
    void load();
    void loadDefaults();
    void save();

    WiFiStation apConfiguration() const { return _apConfiguration; }
    const char *hostname() const;
    void setAPConfiguration(const char *essid, const char *psk);

    WiFiStation station(uint8_t index) const { return _stations[index]; }
    std::vector<WiFiStation> stations() const { return _stations; }
    void setStationConfiguration(uint8_t index, const char *essid, const char *psk);
    
    
    
    
    bool hasStation(const String &essid) const;
    bool hasValidStations() const;

    int16_t retries() const;
    void setRetries(int16_t retries);

    bool reconnectOnDisconnect() const;
    void setReconnectOnDisconnect(bool reconnectOnDisconnect);
private:
    Preferences &preferences;
    FS &fs;
    const char *defaultHostname;
    bool appendMacSuffix;
    std::vector<WiFiStation> _stations;
    WiFiStation _apConfiguration;
    
    void loadDefaultStations();
    int16_t _retries;
    bool _reconnectOnDisconnect;
    const bool reconnectByDefault;
    const uint16_t defaultRetries;
};
}
#endif
