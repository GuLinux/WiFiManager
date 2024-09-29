#ifndef APB_WIFIMANAGER_H
#define APB_WIFIMANAGER_H

#include <WiFiMulti.h>
#include <TaskSchedulerDeclarations.h>
#include "wifisettings.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "wifisettings.h"

#ifndef WIFIMANAGER_MAX_STATIONS
#define WIFIMANAGER_MAX_STATIONS
#endif

namespace GuLinux {

class WiFiManager {
public:
    using OnConnectCallback = std::function<void()>;
    static WiFiManager &Instance;
    enum Status { Idle, Connecting, Station, AccessPoint, Error };
    WiFiManager();
    void setup(Scheduler &scheduler, WiFiSettings *wifiSettings);
    
    void reconnect() { scheduleReconnect = true; }
    Status status() const { return _status; }
    const char *statusAsString() const;
    String essid() const;
    String ipAddress() const;
    String gateway() const;
    void loop();
    void addOnConnectedListener(const OnConnectCallback &onConnected);

    void onGetConfig(AsyncWebServerRequest *request);
    void onGetConfig(JsonObject &responseObject);
    void onPostReconnectWiFi(AsyncWebServerRequest *request);
    void onGetWiFiStatus(AsyncWebServerRequest *request);
    void onConfigStation(AsyncWebServerRequest *request, JsonVariant &json);
    void onConfigAccessPoint(AsyncWebServerRequest *request, JsonVariant &json);

    void setOnConnectedCallback(const std::function<void()> callback) { this->onConnected = callback; }
    void setOnConnectionFailedCallback(const std::function<void()> callback) { this->onConnectionFailed = callback; }
    void setOnNoStationsFoundCallback(const std::function<void()> callback) { this->onNoStationsFound = callback; }
private:
    GuLinux::WiFiSettings *wifiSettings;
    WiFiMulti wifiMulti;
    Status _status;
    bool scheduleReconnect = false;
    bool connectionFailed = false;
    Task rescanWiFiTask;
    std::list<OnConnectCallback> onConnectedCallbacks;

    std::function<void()> onConnected;
    std::function<void()> onConnectionFailed;
    std::function<void()> onNoStationsFound;



    void connect();
    void setApMode();
    void onEvent(arduino_event_id_t event, arduino_event_info_t info);
    void onScanDone(const wifi_event_sta_scan_done_t &scan_done);
    void startScanning();
};
}
#endif
