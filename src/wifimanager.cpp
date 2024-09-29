#include "wifimanager.h"
#include <ArduinoLog.h>
#include <WiFi.h>
#include <jsonresponse.h>
#include <validation.h>

#define LOG_SCOPE "WiFiManager:"

using namespace std::placeholders;

GuLinux::WiFiManager &GuLinux::WiFiManager::Instance = *new GuLinux::WiFiManager();

GuLinux::WiFiManager::WiFiManager() : 
    _status{Status::Idle},
    rescanWiFiTask{3'000, TASK_ONCE, std::bind(&WiFiManager::startScanning, this)} {
    WiFi.onEvent(std::bind(&GuLinux::WiFiManager::onEvent, this, _1, _2));
}

void GuLinux::WiFiManager::onScanDone(const wifi_event_sta_scan_done_t &scan_done) {
    bool success = scan_done.status == 0;
    Log.traceln(LOG_SCOPE "[EVENT] Scan done: success=%d, APs found: %d, scheduleReconnect=%d, connectionFailed=%d", success, scan_done.number, scheduleReconnect, connectionFailed);
    if(success) {
        for(uint8_t i=0; i<scan_done.number; i++) {
            Log.traceln(LOG_SCOPE "[EVENT] AP[%d]: ESSID=%s, channel: %d, RSSI: %d", i, WiFi.SSID(i), WiFi.channel(i), WiFi.RSSI(i));
        }
        if(!connectionFailed) {
            return;
        }
        for(uint8_t i=0; i<scan_done.number; i++) {
            if(wifiSettings->hasStation(WiFi.SSID(i))) {
                Log.infoln(LOG_SCOPE "[EVENT] Found at least one AP from configuration, scheduling reconnection");
                scheduleReconnect = true;
                return;
            } else {
                Log.infoln(LOG_SCOPE "[EVENT] No known APs found, scheduling rescan");
                rescanWiFiTask.enable();
            }
        }
    }
}

void GuLinux::WiFiManager::startScanning() {
    WiFi.scanNetworks(true);
}

void GuLinux::WiFiManager::onEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch(event) {
        case(ARDUINO_EVENT_WIFI_SCAN_DONE):
            onScanDone(info.wifi_scan_done);
            break;
        case(ARDUINO_EVENT_WIFI_AP_START):
            _status = Status::AccessPoint;
            Log.infoln(LOG_SCOPE "[EVENT] Access Point started");
            break;
        case(ARDUINO_EVENT_WIFI_STA_CONNECTED):
            Log.infoln(LOG_SCOPE "[EVENT] Connected to station `%s`, channel %d",
                reinterpret_cast<char*>(info.wifi_sta_connected.ssid),
                info.wifi_sta_connected.channel);
            _status = Status::Station;
            std::for_each(onConnectedCallbacks.begin(), onConnectedCallbacks.end(), std::bind(&OnConnectCallback::operator(), _1));
            break;
        case(ARDUINO_EVENT_WIFI_STA_DISCONNECTED):
        case(ARDUINO_EVENT_WIFI_STA_STOP):
            Log.infoln(LOG_SCOPE "[EVENT] WiFi disconnected: SSID=%s, reason=%d",
                reinterpret_cast<char*>(info.wifi_sta_disconnected.ssid),
                info.wifi_sta_disconnected.reason);
            _status = Status::Idle;
            scheduleReconnect = true;
            break;
        case(ARDUINO_EVENT_WIFI_AP_STOP):
            Log.infoln(LOG_SCOPE "[EVENT] WiFi AP stopped");
            _status = Status::Idle;
            break;
        default:
            Log.traceln("[EVENT] Unknown event %d", event);
    }
}

void GuLinux::WiFiManager::setup(Scheduler &scheduler, WiFiSettings *wifiSettings) {
    Log.traceln(LOG_SCOPE "setup");
    this->wifiSettings = wifiSettings;
    scheduler.addTask(rescanWiFiTask);

    WiFi.setHostname(wifiSettings->hostname());
    _status = Status::Connecting;
    for(uint8_t i=0; i<WIFIMANAGER_MAX_STATIONS; i++) {
        auto station = wifiSettings->station(i);
        if(station) {
            Log.infoln(LOG_SCOPE "found valid station: %s", station.essid);
            wifiMulti.addAP(station.essid, station.psk);
        }
    }
    connect();
    Log.infoln(LOG_SCOPE "setup finished");
}

void GuLinux::WiFiManager::setApMode() {
    Log.infoln(LOG_SCOPE "Starting softAP with essid=`%s`, ip address=`%s`",
            wifiSettings->apConfiguration().essid, WiFi.softAPIP().toString().c_str());
    WiFi.softAP(wifiSettings->apConfiguration().essid, 
        wifiSettings->apConfiguration().open() ? nullptr : wifiSettings->apConfiguration().psk);
}

void GuLinux::WiFiManager::connect() {
    connectionFailed = false;
    bool hasValidStations = wifiSettings->hasValidStations();
    if(!hasValidStations) {
        Log.warningln(LOG_SCOPE "No valid stations found");
        setApMode();
        if(onNoStationsFound) onNoStationsFound();
        return;
    }
    WiFi.mode(WIFI_MODE_STA);
    if( wifiMulti.run() != WL_CONNECTED) {
        Log.warningln(LOG_SCOPE "Unable to connect to WiFi stations");
        if(onConnectionFailed) onConnectionFailed();
        setApMode();
        connectionFailed = true;
        rescanWiFiTask.enable();
        return;
    }
    if(onConnected) onConnected();
    Log.infoln(LOG_SCOPE "Connected to WiFi `%s`, ip address: %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void GuLinux::WiFiManager::loop() {
    if(scheduleReconnect) {
        scheduleReconnect = false;
        connect();
    }
}

void GuLinux::WiFiManager::addOnConnectedListener(const OnConnectCallback &onConnected) {
    onConnectedCallbacks.push_back(onConnected);
}

const char *GuLinux::WiFiManager::statusAsString() const
{
    switch (_status) {
    case Status::AccessPoint:
        return "AccessPoint";
    case Status::Connecting:
        return "Connecting";
    case Status::Error:
        return "Error";
    case Status::Idle:
        return "Idle";
    case Status::Station:
        return "Station";
    default:
        return "N/A";
    }
}

String GuLinux::WiFiManager::essid() const
{
    if(_status == +Status::Station) {
        return WiFi.SSID();
    }
    if(_status == +Status::AccessPoint) {
        return WiFi.softAPSSID();
    }
    return "N/A";
}

String GuLinux::WiFiManager::ipAddress() const {
    if(_status == +Status::AccessPoint || _status == +Status::Station) {
        return WiFi.localIP().toString();
    }
    return "N/A"; 
}

String GuLinux::WiFiManager::gateway() const {
    if(_status == +Status::Station) {
        return WiFi.gatewayIP().toString();
    }
    return "N/A"; 
}

void GuLinux::WiFiManager::onGetConfig(AsyncWebServerRequest *request) {
    JsonResponse response(request);
    auto rootObject = response.root().to<JsonObject>();
    onGetConfig(rootObject);
}

void GuLinux::WiFiManager::onGetConfig(JsonObject &responseObject) {
    responseObject["accessPoint"]["essid"] = wifiSettings->apConfiguration().essid;
    responseObject["accessPoint"]["psk"] = wifiSettings->apConfiguration().psk;
    for(uint8_t i=0; i<WIFIMANAGER_MAX_STATIONS; i++) {
        auto station = wifiSettings->station(i);
        responseObject["stations"][i]["essid"] = station.essid;
        responseObject["stations"][i]["psk"] = station.psk;
    }
}

void GuLinux::WiFiManager::onGetWiFiStatus(AsyncWebServerRequest *request) {
    JsonResponse response(request);
    response.root()["wifi"]["status"] = WiFiManager::Instance.statusAsString();
    response.root()["wifi"]["essid"] = WiFiManager::Instance.essid();
    response.root()["wifi"]["ip"] = WiFiManager::Instance.ipAddress();
    response.root()["wifi"]["gateway"] = WiFiManager::Instance.gateway();
}

void GuLinux::WiFiManager::onPostReconnectWiFi(AsyncWebServerRequest *request) {
    reconnect();
    onGetConfig(request);
}

void GuLinux::WiFiManager::onConfigAccessPoint(AsyncWebServerRequest *request, JsonVariant &json) {
    if(request->method() == HTTP_DELETE) {
        Log.traceln(LOG_SCOPE "onConfigAccessPoint: method=%d (%s)", request->method(), request->methodToString());
        wifiSettings->setAPConfiguration("", "");
    }
    if(request->method() == HTTP_POST) {
        if(Validation{request, json}.required<const char*>({"essid", "psk"}).notEmpty("essid").invalid()) return;

        String essid = json["essid"];
        String psk = json["psk"];
        Log.traceln(LOG_SCOPE "onConfigAccessPoint: essid=%s, psk=%s, method=%d (%s)",
            essid.c_str(), psk.c_str(), request->method(), request->methodToString());
        wifiSettings->setAPConfiguration(essid.c_str(), psk.c_str());

    }
    onGetConfig(request);
}



void GuLinux::WiFiManager::onConfigStation(AsyncWebServerRequest *request, JsonVariant &json) {
    Validation validation{request, json};
    validation.required<int>("index").range("index", {0}, {WIFIMANAGER_MAX_STATIONS-1});

    if(request->method() == HTTP_POST) {
        validation.required<const char*>({"essid", "psk"}).notEmpty("essid");
    }
    if(validation.invalid()) return;
    int stationIndex = json["index"];
    String essid = json["essid"];
    String psk = json["psk"];
    Log.traceln(LOG_SCOPE "onConfigStation: `%d`, essid=`%s`, psk=`%s`, method=%d (%s)", 
        stationIndex, essid.c_str(), psk.c_str(), request->method(), request->methodToString());
    if(request->method() == HTTP_POST) {
        wifiSettings->setStationConfiguration(stationIndex, essid.c_str(), psk.c_str());
    } else if(request->method() == HTTP_DELETE) {
        wifiSettings->setStationConfiguration(stationIndex, "", "");
    }
    onGetConfig(request);
}
