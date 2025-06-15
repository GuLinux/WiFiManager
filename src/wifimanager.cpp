#include "wifimanager.h"
#include <ArduinoLog.h>
#include <WiFi.h>
#include <jsonresponse.h>
#include <webvalidation.h>

#define LOG_SCOPE "WiFiManager:"

using namespace std::placeholders;

GuLinux::WiFiManager &GuLinux::WiFiManager::Instance = *new GuLinux::WiFiManager();

GuLinux::WiFiManager::WiFiManager() : _status{Status::Idle} {}



void GuLinux::WiFiManager::setup(WiFiSettings *wifiSettings) {
    Log.traceln(LOG_SCOPE "setup: retries=%d, reconnectOnDisconnect=%s",
            wifiSettings->retries(), wifiSettings->reconnectOnDisconnect() ? "true" : "false");
    this->wifiSettings = wifiSettings;

    WiFi.setHostname(wifiSettings->hostname());
    _status = Status::Connecting;
    for(uint8_t i=0; i<WIFIMANAGER_MAX_STATIONS; i++) {
        auto station = wifiSettings->station(i);
        if(station) {
            Log.infoln(LOG_SCOPE "found valid station: %s", station.essid);
            wifiMulti.addAP(station.essid, station.psk);
        }
    }
    
    wifiMulti.onConnected(std::bind(&WiFiManager::onConnected, this, _1));
    wifiMulti.onFailure(std::bind(&WiFiManager::onFailure, this));
    wifiMulti.onDisconnected(std::bind(&WiFiManager::onDisconnected, this, _1, _2));

    reconnect();

    Log.infoln(LOG_SCOPE "setup finished");
}

void GuLinux::WiFiManager::setApMode() {
    Log.infoln(LOG_SCOPE "Starting softAP with essid=`%s`, ip address=`%s`",
            wifiSettings->apConfiguration().essid, WiFi.softAPIP().toString().c_str());
    WiFi.softAP(wifiSettings->apConfiguration().essid, 
        wifiSettings->apConfiguration().open() ? nullptr : wifiSettings->apConfiguration().psk);
}

void GuLinux::WiFiManager::onConnected(const AsyncWiFiMulti::ApSettings &apSettings) {
    Log.infoln(LOG_SCOPE "Connected to WiFi `%s`, ip address: %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    _status = Status::Station;
    if(onConnectedCb) onConnectedCb(apSettings);
}

void GuLinux::WiFiManager::onDisconnected(const char *ssid, uint8_t disconnectionReason) {
    Log.warningln(LOG_SCOPE "Disconnected from WiFi station `%s`, reason: %d", ssid, disconnectionReason);
    if(onDisconnectedCb) onDisconnectedCb(ssid, disconnectionReason);
    if(wifiSettings->reconnectOnDisconnect()) {
        reconnect();
    }
}

void GuLinux::WiFiManager::onFailure() {
    Log.warningln(LOG_SCOPE "Unable to connect to WiFi stations");
    if(retries < wifiSettings->retries() || wifiSettings->retries() < 0) {
        Log.warningln(LOG_SCOPE "Retrying connection (%d/%d)", retries++, wifiSettings->retries());
        connect();
    } else {
        Log.warningln(LOG_SCOPE "Max retries reached, switching to Access Point mode");
        setApMode();
        _status = Status::AccessPoint;
    }
    if(onFailureCb) onFailureCb();
}
void GuLinux::WiFiManager::reconnect()
{
    this->retries = 0;
    connect();
}
void GuLinux::WiFiManager::connect() {
    _status = Status::Connecting;
    wifiMulti.start();
}

 

const char *GuLinux::WiFiManager::statusAsString() const {
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

void GuLinux::WiFiManager::onGetConfig(JsonObject responseObject) {
    responseObject["accessPoint"]["essid"] = wifiSettings->apConfiguration().essid;
    responseObject["accessPoint"]["psk"] = wifiSettings->apConfiguration().psk;
    for(uint8_t i=0; i<WIFIMANAGER_MAX_STATIONS; i++) {
        auto station = wifiSettings->station(i);
        responseObject["stations"][i]["essid"] = station.essid;
        responseObject["stations"][i]["psk"] = station.psk;
    }
    responseObject["retries"] = wifiSettings->retries();
    responseObject["reconnectOnDisconnect"] = wifiSettings->reconnectOnDisconnect();
}

void GuLinux::WiFiManager::onGetWiFiStatus(AsyncWebServerRequest *request) {
    JsonResponse response(request);
    auto rootObject = response.root().to<JsonObject>();
    onGetWiFiStatus(rootObject);
}

void GuLinux::WiFiManager::onGetWiFiStatus(JsonObject responseObject) {
    responseObject["wifi"]["status"] = WiFiManager::Instance.statusAsString();
    responseObject["wifi"]["essid"] = WiFiManager::Instance.essid();
    responseObject["wifi"]["ip"] = WiFiManager::Instance.ipAddress();
    responseObject["wifi"]["gateway"] = WiFiManager::Instance.gateway();
}

void GuLinux::WiFiManager::onPostReconnectWiFi(AsyncWebServerRequest *request) {
    reconnect();
    onGetConfig(request);
}

void GuLinux::WiFiManager::onConfigAccessPoint(AsyncWebServerRequest *request, JsonVariant &json) {
    if(request->method() == HTTP_DELETE) {
        Log.traceln(LOG_SCOPE "onConfigAccessPoint: method=%d (%s)", request->method(), request->methodToString());
        onDeleteAccessPoint();
    }
    if(request->method() == HTTP_POST) {
        WebValidation validation{request, json};
        onConfigAccessPoint(validation);
    }
    onGetConfig(request);
}

void GuLinux::WiFiManager::onConfigAccessPoint(Validation &validation) {
        validation
            .required<const char*>({"essid", "psk"})
            .notEmpty("essid")
            .ifValid([this](JsonVariant json){
                String essid = json["essid"];
                String psk = json["psk"];
                Log.traceln(LOG_SCOPE "onConfigAccessPoint: essid=%s, psk=%s", essid.c_str(), psk.c_str());
                wifiSettings->setAPConfiguration(essid.c_str(), psk.c_str());
            });

}

void GuLinux::WiFiManager::onDeleteAccessPoint() {
    wifiSettings->setAPConfiguration("", "");
}

void GuLinux::WiFiManager::onConfigWiFiManagerSettings(AsyncWebServerRequest *request, JsonVariant &json) {
    WebValidation validation{request, json};

    if(request->method() == HTTP_POST) {
        onConfigWiFiManagerSettings(validation);
    }
    onGetConfig(request);
}

void GuLinux::WiFiManager::onConfigWiFiManagerSettings(Validation &validation) {
    validation
        .required<int16_t>("retries")
        .range("retries", {-1}, {std::numeric_limits<int16_t>::max()})
        .ifValid([this](JsonVariant json) {
            int16_t retries = json["retries"];
            Log.traceln(LOG_SCOPE "onConfigWiFiManagerSettings: retries=%d", retries);
            wifiSettings->setRetries(retries);
        });

    validation
        .required<bool>("reconnectOnDisconnect")
        .ifValid([this](JsonVariant json) {
            bool reconnectOnDisconnect = json["reconnectOnDisconnect"];
            Log.traceln(LOG_SCOPE "onConfigWiFiManagerSettings: reconnectOnDisconnect=%d", reconnectOnDisconnect);
            wifiSettings->setReconnectOnDisconnect(reconnectOnDisconnect);
        });
}

void GuLinux::WiFiManager::onConfigStation(AsyncWebServerRequest *request, JsonVariant &json) {
    WebValidation validation{request, json};

    if(request->method() == HTTP_POST) {
        onConfigStation(validation);
    }
    if(request->method() == HTTP_DELETE) {
        onDeleteStation(validation);
    }
    onGetConfig(request);
}

void GuLinux::WiFiManager::onConfigStation(Validation &validation) {
    validation
        .required<int>("index")
        .range("index", {0}, {WIFIMANAGER_MAX_STATIONS-1})
        .required<const char*>({"essid", "psk"}).notEmpty("essid")
        .ifValid([this](JsonVariant json){
            int stationIndex = json["index"];
            String essid = json["essid"];
            String psk = json["psk"];
            Log.traceln(LOG_SCOPE "onConfigStation: `%d`, essid=`%s`, psk=`%s`", stationIndex, essid.c_str(), psk.c_str());
        });
}

void GuLinux::WiFiManager::onDeleteStation(Validation &validation) {
    validation
        .required<int>("index")
        .range("index", {0}, {WIFIMANAGER_MAX_STATIONS-1})
        .ifValid([this](JsonVariant json){
            int stationIndex = json["index"];
            wifiSettings->setStationConfiguration(stationIndex, "", "");
        });
}
