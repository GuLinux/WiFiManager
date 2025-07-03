#ifndef APB_WIFIMANAGER_H
#define APB_WIFIMANAGER_H

#include <AsyncWiFiMulti.h>
#include "wifisettings.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "wifisettings.h"
#include <validation.h>

#include <queue>



namespace GuLinux {

class WiFiManager {
public:
    static WiFiManager &Instance;
    enum Status { Idle, Connecting, Station, AccessPoint, Error };
    WiFiManager();
    void setup(WiFiSettings *wifiSettings);
    
    void reconnect();
    void rescan();
    Status status() const { return _status; }
    const char *statusAsString() const;
    String essid() const;
    String ipAddress() const;
    String gateway() const;

    void onGetConfig(AsyncWebServerRequest *request);
    void onGetConfig(JsonObject responseObject);

    void onGetWiFiStatus(AsyncWebServerRequest *request);
    void onGetWiFiStatus(JsonObject responseObject);

    void onPostReconnectWiFi(AsyncWebServerRequest *request);
    
    void onConfigStation(AsyncWebServerRequest *request, JsonVariant &json);
    void onConfigStation(Validation &validation);
    void onDeleteStation(Validation &validation);

    void onConfigAccessPoint(AsyncWebServerRequest *request, JsonVariant &json);
    void onConfigAccessPoint(Validation &validation);
    void onDeleteAccessPoint();

    void onConfigWiFiManagerSettings(AsyncWebServerRequest *request, JsonVariant &json);
    void onConfigWiFiManagerSettings(Validation &validation);

    
    void setOnConnectedCallback(const AsyncWiFiMulti::OnConnected &callback) { this->onConnectedCb = callback; }
    void setOnConnectionFailedCallback(const AsyncWiFiMulti::OnFailure &callback) { this->onFailureCb = callback; }
    void setOnDisconnectedCallback(const AsyncWiFiMulti::OnDisconnected &callback) { this->onDisconnectedCb = callback; }

    void loop();
private:
    GuLinux::WiFiSettings *wifiSettings;
    AsyncWiFiMulti wifiMulti;
    Status _status;
    void connect();

    void onConnected(const AsyncWiFiMulti::ApSettings &apSettings);
    void onDisconnected(const char *ssid, uint8_t disconnectionReason);
    void onFailure();

    AsyncWiFiMulti::OnConnected onConnectedCb;
    AsyncWiFiMulti::OnDisconnected onDisconnectedCb;
    AsyncWiFiMulti::OnFailure onFailureCb;

    void setApMode();
    uint8_t retries = 0;
    
    std::queue<std::function<void()>> _loopCallbacks;
};
}
#endif
