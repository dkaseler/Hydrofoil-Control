#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESP32Servo.h>
#include <QuickPID.h>
#include <TickTwo.h>
#include <WebServer.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <driver/rmt.h>
#include <vector>
#include <algorithm>
#include <freertos/semphr.h>
#include "filehandling.h"
#include "sensor_wait.h"
#include "controller_validation.h"
#include "calibration_session.h"
#include "bench_recording.h"
#include "controller_assets.h"

constexpr int servoPin=1, capacitancePin=6, ledPin=8, resetPin=5;
extern WebServer benchServer;
extern bool benchFilesystem,benchAutomatic,benchSingle;
extern String benchHostname;
extern bench_recording::Recorder benchRecording;
void setupBenchRoutes();
void serviceBenchSamples();
void stopBenchSamples(bench_recording::FinishReason reason=bench_recording::FinishReason::Stopped);

bool first=false,slave=false,settingsReady=false,benchActive=false,otaInProgress=false;
bool simulateSensorTimeout=false,sensorHealthy=false,positionValid=false;
uint32_t sensorTimeouts=0,sensorGoodSamples=0,sensorLastTicks=0;
std::vector<int> rawValues;
hw_timer_t *timer=nullptr;
int median=0,position=0,servoPos=1500;
float setpoint=1500,input=1500,output=0;
int control=0,pwmRead=0;
uint32_t receiverLastValid=0;
bool receiverValid=false;
constexpr uint32_t kReceiverTimeoutMs=250;
Servo elevator;
dataStruct controlParams{};
allBoards boardsParams{};
CalibrationSession calibrationSession;
String calibrationNotice;
bool calibrationSampling(){return settingsReady&&controlParams.calibration==1&&calibrationSession.active;}
String macAddresses[2];
String boardSelector="master";
const char *jsonConfigsPath="/configs.json",*jsonWifiPath="/wifi.json",*jsonAddressesPath="/addresses.json";
QuickPID elevatorPid(&input,&output,&setpoint,0,0,0,
 QuickPID::pMode::pOnError,QuickPID::dMode::dOnError,
 QuickPID::iAwMode::iAwClamp,QuickPID::Action::direct);
bool servoTimeOut=false;
uint32_t servoTimeOutStart=0;
constexpr uint32_t servoTimeOutInterval=2000;
RingbufHandle_t rb=nullptr;
uint32_t receiverClockHz=2000000;
QueueHandle_t receivedConfigs=nullptr;
bool espNowReady=false;
uint32_t rejectedPackets=0,receivedPackets=0;
int lastPeerSendResult=0;
bool hideHotspot=false,hotspotVisible=true,restartPending=false;
uint32_t hideHotspotAt=0,restartAt=0;
String controllerError;
SemaphoreHandle_t controlMutex=nullptr;
TaskHandle_t controlTaskHandle=nullptr;
class ControlLock {
 public: ControlLock(){if(controlMutex)xSemaphoreTakeRecursive(controlMutex,portMAX_DELAY);}
 ~ControlLock(){if(controlMutex)xSemaphoreGiveRecursive(controlMutex);}
};


void enterBenchMode() {
 ControlLock lock;
 if(!benchActive) {
  benchActive=true;
  elevatorPid.SetMode(QuickPID::Control::manual);
  elevator.detach();
  stopBenchSamples();
  servoTimeOut=false;
 }
}
void writeServoCommand(int pulse) {
 if(benchActive||otaInProgress||!settingsReady||first||!elevator.attached())return;
 const int lo=std::min(controlParams.servoMin,controlParams.servoMax);
 const int hi=std::max(controlParams.servoMin,controlParams.servoMax);
 servoPos=std::max(lo,std::min(hi,pulse));
 elevator.writeMicroseconds(servoPos);
}
bool resumeController() {
 ControlLock lock;
 if(!settingsReady||!timer||first||otaInProgress||!controlTaskHandle)return false;
 if(controlParams.calibration||!usableCalibrationRange(controlParams.minMeasured,controlParams.maxMeasured)){controllerError="Finish sensor calibration before resuming control";return false;}
 stopBenchSamples();
 benchActive=false;
 // Discard receiver pulses buffered while control was paused; only new pulses
 // may re-establish a live receiver command.
 if(rb){size_t bytes=0;void *item;while((item=xRingbufferReceive(rb,&bytes,0)))vRingbufferReturnItem(rb,item);}
 receiverValid=false;control=0;
 output=0;input=1500;setpoint=controlParams.setpoint;
 elevatorPid.SetMode(QuickPID::Control::manual);
 elevatorPid.SetSampleTimeUs(10000);
 elevatorPid.SetTunings(controlParams.p,controlParams.i,controlParams.d);
 elevatorPid.SetOutputLimits(-500,500);
 elevator.attach(servoPin,1000,2000);
 if(!elevator.attached()){benchActive=true;controllerError="Servo output could not start";return false;}
 writeServoCommand((controlParams.servoMin+controlParams.servoMax)/2);
 elevatorPid.SetMode(QuickPID::Control::timer);
 controllerError="";return true;
}
void addControllerDiagnostics(JsonObject d) {
 ControlLock lock;
 d["sensor_healthy"]=sensorHealthy;d["sensor_timeouts"]=sensorTimeouts;
 d["sensor_good_samples"]=sensorGoodSamples;d["last_raw_ticks"]=sensorLastTicks;
 d["servo_attached"]=elevator.attached();
 d["controller_mode"]=benchActive?"bench":"controller";
 d["settings_valid"]=settingsReady;
 d["first_setup"]=first;d["slave"]=slave;
 d["station_mac"]=WiFi.macAddress();d["ssid"]=benchHostname;
 d["position_valid"]=positionValid;d["median20_ticks"]=median;
 d["servo_us"]=servoPos;d["receiver_valid"]=receiverValid;
 d["receiver_pulse_us"]=pwmRead;d["receiver_offset"]=control;
 d["controller_error"]=controllerError;d["esp_now_ready"]=espNowReady;
 d["esp_now_received"]=receivedPackets;d["esp_now_rejected"]=rejectedPackets;
 d["last_peer_send_result"]=lastPeerSendResult;
 d["normal_sample_target_hz"]=500;d["pid_interval_ms"]=10;
 d["ota_in_progress"]=otaInProgress;
 d["calibration_active"]=calibrationSession.active;
 d["calibration_samples"]=calibrationSession.samples;
 d["calibration_range_ready"]=calibrationSession.usable();
 d["calibration_saved_valid"]=usableCalibrationRange(controlParams.minMeasured,controlParams.maxMeasured);
 d["calibration_notice"]=calibrationNotice;
}
int getMedian(const std::vector<int> &values) {
  std::vector<int> temp = values;
  std::sort(temp.begin(), temp.end());
  return temp[temp.size() / 2];
}
void startMeasurement() {
  if (timer == nullptr) {
    sensorHealthy = false;
    positionValid = false;
    rawValues.clear();
    return;
  }
  pinMode(capacitancePin, OUTPUT);
  digitalWrite(capacitancePin, LOW);
  delayMicroseconds(50);
  noInterrupts();
  pinMode(capacitancePin, INPUT);
  timerRestart(timer);
  const auto result = waitForSensorHigh(
      []() { return !simulateSensorTimeout && digitalRead(capacitancePin) == HIGH; },
      []() { return timerRead(timer); });
  interrupts();

  sensorLastTicks = static_cast<uint32_t>(result.ticks);
  if (result.timedOut) {
    sensorTimeouts++;
    // Fault counts are reported by diagnostics; never block control on UART.
    sensorHealthy = false;
    positionValid = false;
    rawValues.clear(); // Do not feed stale samples into the PID.
    return;
  }
  sensorHealthy = true;
  sensorGoodSamples++;
  rawValues.push_back(static_cast<int>(result.ticks));
  if (rawValues.size() > 20) {
    rawValues.erase(rawValues.begin());
  }
  if(calibrationSession.active&&rawValues.size()==20&&!simulateSensorTimeout)
    {median=getMedian(rawValues);calibrationSession.observe(median);}
}
void calculatePosition() {
  positionValid = false;
  if (!sensorHealthy || rawValues.size() < 20) {
    return;
  }

  median = getMedian(rawValues);
  const int minimum=calibrationSession.active?calibrationSession.minimum:controlParams.minMeasured;
  const int maximum=calibrationSession.active?calibrationSession.maximum:controlParams.maxMeasured;
  if (!usableCalibrationRange(minimum,maximum) || (calibrationSession.active&&!calibrationSession.usable()))
    return;
  float progress = static_cast<float>(median - minimum) / (maximum - minimum);

  // Scale the progress value between 1000 and 2000us
  position = 1000 + 1000 * progress;
  positionValid = true;
}
void calculatePid() {
  static bool pausedForSensor = false;
  calculatePosition();
  if (!positionValid) {
    elevatorPid.SetMode(QuickPID::Control::manual);
    pausedForSensor = true;
    return; // Existing PWM remains at its last command; do not invent a safe angle.
  }

  input = position;
  if (pausedForSensor) {
    elevatorPid.SetMode(QuickPID::Control::timer);
    pausedForSensor = false;
  }
  setpoint = controlParams.setpoint + control;
  if (setpoint < 1000)
    setpoint = 1000;
  else if (setpoint > 2000)
    setpoint = 2000;
  elevatorPid.Compute();
  servoPos =
      map(output, -500, 500, controlParams.servoMin, controlParams.servoMax);
  writeServoCommand(servoPos);
}

void getPulseDuration() {
 if(rb) {
  size_t bytes=0; rmt_item32_t *items=nullptr;
  while((items=(rmt_item32_t*)xRingbufferReceive(rb,&bytes,0))) {
   for(size_t n=0;n<bytes/sizeof(rmt_item32_t);++n) {
    const auto &p=items[n];
    uint32_t ticks=p.level0?p.duration0:(p.level1?p.duration1:0);
    int pulse=receiverClockHz?uint64_t(ticks)*1000000/receiverClockHz:0;
    if(pulse>=985&&pulse<=2015){pwmRead=pulse;receiverLastValid=millis();receiverValid=true;}
   }
   vRingbufferReturnItem(rb,items);
  }
 }
 if(receiverValid&&uint32_t(millis()-receiverLastValid)>kReceiverTimeoutMs)receiverValid=false;
 control=receiverValid?(pwmRead-1500)*controlParams.factor/100:0;
}
void setupRMTReceiver() {
 rmt_config_t c={};c.rmt_mode=RMT_MODE_RX;c.channel=RMT_CHANNEL_3;
 c.gpio_num=GPIO_NUM_3;c.clk_div=40;c.mem_block_num=1;
 c.rx_config.filter_en=true;c.rx_config.filter_ticks_thresh=100;
 c.rx_config.idle_threshold=25000;
 if(rmt_config(&c)!=ESP_OK||rmt_driver_install(c.channel,2048,0)!=ESP_OK)return;
 if(rmt_get_ringbuf_handle(c.channel,&rb)!=ESP_OK){rb=nullptr;return;}
 rmt_get_counter_clock(c.channel,&receiverClockHz);
 rmt_rx_start(c.channel,true);
}
TickTwo measurementTicker([](){startMeasurement();},2,0,MILLIS);
TickTwo pidTicker([](){
 if(controlParams.enable)calculatePid();
 else {calculatePosition();elevatorPid.SetMode(QuickPID::Control::manual);
  int mid=(controlParams.servoMin+controlParams.servoMax)/2;
  writeServoCommand(mid+(controlParams.servoMax>=controlParams.servoMin?control:-control));}
},10,0,MILLIS);
TickTwo rmtTicker([](){getPulseDuration();},20,0,MILLIS);

void onDataRecv(const uint8_t*,const uint8_t *data,int len) {
 dataStruct next;
 if(!decodeControlPacket(data,len,next)||!receivedConfigs){++rejectedPackets;return;}
 if(xQueueSend(receivedConfigs,&next,0)!=pdTRUE)++rejectedPackets;
}
void addPeer(const String &address) {
 uint8_t mac[6];if(!espNowReady||!parseMacAddress(address.c_str(),mac))return;
 if(esp_now_is_peer_exist(mac))return;
 esp_now_peer_info_t peer={};std::copy(mac,mac+6,peer.peer_addr);
 peer.channel=1;peer.ifidx=WIFI_IF_STA;peer.encrypt=false;
 lastPeerSendResult=esp_now_add_peer(&peer);
}
void sendPeer(unsigned index) {
 uint8_t mac[6];if(index>1||!parseMacAddress(macAddresses[index].c_str(),mac))return;
 addPeer(macAddresses[index]);
 dataStruct &p=index?boardsParams.slave2:boardsParams.slave1;
 lastPeerSendResult=esp_now_send(mac,reinterpret_cast<uint8_t*>(&p),sizeof(p));
}
void setupEspNow() {
 receivedConfigs=xQueueCreate(4,sizeof(dataStruct));
 espNowReady=esp_now_init()==ESP_OK;
 if(!espNowReady)return;
 esp_now_register_recv_cb(onDataRecv);
 if(!slave){for(unsigned i=0;i<2;i++)addPeer(macAddresses[i]);}
}
void applyLocalParams(const dataStruct &next) {
 ControlLock lock;
 bool minChanged=next.servoMin!=controlParams.servoMin;
 bool maxChanged=next.servoMax!=controlParams.servoMax;
 bool enabled=next.enable!=controlParams.enable;
 dataStruct merged=next;
 if(next.calibration&&!controlParams.calibration){
  calibrationSession.begin();rawValues.clear();calibrationNotice="Collecting range: raise and lower sensor in water, then turn calibration OFF";
 }else if(!next.calibration&&controlParams.calibration){
  calibrationNotice=calibrationSession.usable()?"Calibration saved":
   usableCalibrationRange(next.minMeasured,next.maxMeasured)?"No usable new range; previous calibration retained":"No usable range; calibration OFF, calibrate before resuming";
  calibrationSession.finish();
 }
 controlParams=merged;
 if(!next.calibration&&!usableCalibrationRange(next.minMeasured,next.maxMeasured))enterBenchMode();
 elevatorPid.SetTunings(next.p,next.i,next.d);
 if(enabled)elevatorPid.SetMode(next.enable?QuickPID::Control::timer:QuickPID::Control::manual);
 if(!benchActive&&(minChanged||maxChanged)){
  writeServoCommand(minChanged?next.servoMin:next.servoMax);
  servoTimeOut=true;servoTimeOutStart=millis();
 }
}
void serviceReceivedConfigs() {
 dataStruct next;
 while(receivedConfigs&&xQueueReceive(receivedConfigs,&next,0)==pdTRUE) {
  if(!slave)continue;
  ControlLock transaction;
  ++receivedPackets;
  {ControlLock lock;next.minMeasured=controlParams.minMeasured;next.maxMeasured=controlParams.maxMeasured;
  if(!next.calibration&&controlParams.calibration&&calibrationSession.usable()){next.minMeasured=calibrationSession.minimum;next.maxMeasured=calibrationSession.maximum;}}
  if(!validControlParams(next)){++rejectedPackets;continue;}
  {ControlLock lock;if(std::memcmp(&next,&controlParams,sizeof(next))==0)continue;}
  allBoards proposed=boardsParams;proposed.slave1=next;
  if(writeStructJson(SPIFFS,jsonConfigsPath,proposed)){
   boardsParams=proposed;applyLocalParams(next);
  }else controllerError="Could not save slave settings";
 }
}
void sendJson(JsonDocument &d) {
 if(d.overflowed()){benchServer.send(500,"text/plain","Response too large");return;}
 String body;serializeJson(d,body);benchServer.sendHeader("Cache-Control","no-store");
 benchServer.send(200,"application/json",body);
}
void redirect(const char *path) {benchServer.sendHeader("Location",path);benchServer.send(303);}
int selectedIndex() {
 if(slave)return 1;
 if(boardSelector=="master")return 0;
 return boardSelector=="slave-1"?1:2;
}
dataStruct selectedParams() {int n=selectedIndex();
 if(!slave&&benchServer.hasArg("board")){String x=benchServer.arg("board");n=x=="slave-1"?1:x=="slave-2"?2:0;}return n==0?boardsParams.master:n==1?boardsParams.slave1:boardsParams.slave2;}
bool parseNumber(const char *name,float &v) {
 if(!benchServer.hasArg(name))return false;
 String s=benchServer.arg(name);char *end=nullptr;
 if(!s.length())return false;v=strtof(s.c_str(),&end);
 return end!=s.c_str()&&*end=='\0'&&std::isfinite(v);
}
bool parseInteger(const char *name,int &v) {float x;if(!parseNumber(name,x)||x!=floorf(x)||x<-100000||x>100000)return false;v=int(x);return true;}
void setSliders() {
 ControlLock transaction;
 if(!settingsReady){benchServer.send(503,"text/plain","Settings unavailable");return;}
 String selector=benchServer.arg("board-selector");
 if(selector!="master"&&selector!="slave-1"&&selector!="slave-2"){benchServer.send(400,"text/plain","Unknown board");return;}
 int index=slave?1:selector=="master"?0:selector=="slave-1"?1:2;
 dataStruct next=index==0?boardsParams.master:index==1?boardsParams.slave1:boardsParams.slave2;
 bool ok=parseNumber("slider-p",next.p)&&parseNumber("slider-i",next.i)&&parseNumber("slider-d",next.d)&&
 parseInteger("slider-setpoint",next.setpoint)&&parseInteger("slider-factor",next.factor)&&
 parseInteger("slider-enable",next.enable)&&parseInteger("slider-calibration",next.calibration)&&
 parseInteger("slider-servo-min",next.servoMin)&&parseInteger("slider-servo-max",next.servoMax);
 if(!ok){benchServer.send(400,"text/plain","Invalid slider values");return;}
 bool local=slave||index==0;
 if(local){ControlLock lock;next.minMeasured=controlParams.minMeasured;next.maxMeasured=controlParams.maxMeasured;
 if(!next.calibration&&controlParams.calibration&&calibrationSession.usable()){next.minMeasured=calibrationSession.minimum;next.maxMeasured=calibrationSession.maximum;}}
 if(!validControlParams(next)){benchServer.send(400,"text/plain","Invalid slider values or incomplete calibration");return;}
 allBoards proposed=boardsParams;
 if(index==0)proposed.master=next;else if(index==1)proposed.slave1=next;else proposed.slave2=next;
 if(!writeStructJson(SPIFFS,jsonConfigsPath,proposed)){benchServer.send(500,"text/plain","Could not save settings");return;}
 boardsParams=proposed;boardSelector=selector;
 if(local)applyLocalParams(next);else sendPeer(index-1);
 benchServer.send(200,"text/plain","OK");
}
// Explicit recovery for incomplete legacy configs. Never called during boot.
void restoreControllerDefaults() {
 ControlLock lock;
 if(!benchActive||otaInProgress){benchServer.send(409,"text/plain","Pause the controller before restoring defaults");return;}
 if(!benchFilesystem){benchServer.send(503,"text/plain","Filesystem unavailable");return;}
 if(benchServer.arg("confirm")!="reset-controller-settings"){
  benchServer.send(400,"text/plain","Explicit controller-default confirmation required");return;
 }
 uint8_t expected[6],actual[6];
 if(!parseMacAddress(benchServer.arg("expected-mac").c_str(),expected)||
    !parseMacAddress(WiFi.macAddress().c_str(),actual)||std::memcmp(expected,actual,6)!=0){
  benchServer.send(409,"text/plain","Station MAC does not match the requested board");return;
 }
 String firstFlag=readFileJson(SPIFFS,jsonWifiPath,"first"),slaveFlag=readFileJson(SPIFFS,jsonWifiPath,"slave");
 auto validFlag=[](const String &v){return v=="true"||v=="false"||v=="True"||v=="False"||v=="0"||v=="1";};
 auto truth=[](const String &v){return v=="true"||v=="True"||v=="1";};
 if(!validFlag(firstFlag)||!validFlag(slaveFlag)){
  benchServer.send(503,"text/plain","Saved device role flags are unavailable");return;
 }
 // Same eleven factory values per board as data/configs.json, including all
 // six fields lost by the original undersized JSON allocation on slave2.
 const dataStruct defaults={1.0f,0.0f,0.0f,1500,1,1000,2000,40,0,2000,24000};
 allBoards proposed={defaults,defaults,defaults};
 stopBenchSamples();
 elevatorPid.SetMode(QuickPID::Control::manual);
 elevator.detach();
 servoTimeOut=false;
 if(!writeStructJson(SPIFFS,jsonConfigsPath,proposed)){
  controllerError="Controller defaults could not be saved";
  benchServer.send(500,"text/plain",controllerError);return;
 }
 allBoards checked{};
 if(!readStructJson(SPIFFS,jsonConfigsPath,checked)||
    !validControlParams(checked.master)||!validControlParams(checked.slave1)||!validControlParams(checked.slave2)||
    std::memcmp(&checked,&proposed,sizeof(checked))!=0){
  controllerError="Controller defaults readback failed; controller remains paused";
  benchServer.send(500,"text/plain",controllerError);return;
 }
 boardsParams=checked;
 calibrationSession.finish();calibrationNotice="";
 first=truth(firstFlag);slave=truth(slaveFlag);
 controlParams=slave?boardsParams.slave1:boardsParams.master;
 if(controlParams.calibration==1)calibrationNotice="Calibration interrupted by restart. Turn OFF, then ON to start a new calibration";
 settingsReady=true;
 rawValues.clear();sensorHealthy=false;positionValid=false;median=0;position=0;
 receiverValid=false;control=0;pwmRead=0;
 output=0;input=1500;setpoint=controlParams.setpoint;
 elevatorPid.Reset();
 elevatorPid.SetSampleTimeUs(10000);
 elevatorPid.SetTunings(controlParams.p,controlParams.i,controlParams.d);
 elevatorPid.SetOutputLimits(-500,500);
 controllerError="";
 benchServer.send(200,"text/plain","Controller defaults saved and verified. Controller remains paused; resume separately.");
}
void configureRoutes() {
 benchServer.on("/",HTTP_GET,[](){benchServer.sendHeader("Cache-Control","no-store");benchServer.send_P(200,"text/html",kControllerIndex);});
 benchServer.on("/script.js",HTTP_GET,[](){benchServer.send_P(200,"application/javascript",kControllerScript);});
 benchServer.on("/style.css",HTTP_GET,[](){benchServer.send_P(200,"text/css",kControllerStyle);});
 benchServer.on("/setup",HTTP_GET,[](){benchServer.send_P(200,"text/html",kControllerSetup);});
 benchServer.on("/get-mac",HTTP_GET,[](){benchServer.send(200,"text/plain",WiFi.macAddress());});
 benchServer.on("/device-status",HTTP_GET,[](){StaticJsonDocument<512>d;d["slave"]=slave;d["first"]=first;d["controller_mode"]=benchActive?"bench":"controller";d["settings_valid"]=settingsReady;d["error"]=controllerError;sendJson(d);});
 benchServer.on("/setup",HTTP_POST,[](){
  if(!benchFilesystem){benchServer.send(503,"text/plain","Filesystem unavailable");return;}
  String role=benchServer.arg("role");if(role!="master"&&role!="slave"){benchServer.send(400,"text/plain","Invalid role");return;}
  enterBenchMode();
  if(!writeFileJson(SPIFFS,jsonWifiPath,"slave",role=="slave"?"True":"False")||!writeFileJson(SPIFFS,jsonWifiPath,"first","False")){
   benchServer.send(500,"text/plain","Could not save role");return;}
  benchServer.send(200,"text/html","<meta name='viewport' content='width=device-width'><p>Role saved. Reconnect to the same HF-Bench network after restart, then open <a href='/'>Controller</a>.</p>");
  restartPending=true;restartAt=millis()+1000;
 });
 benchServer.on("/controller/resume",HTTP_POST,[](){if(!resumeController()){benchServer.send(409,"text/plain","Controller cannot start; check diagnostics and device setup");return;}redirect("/");});
 benchServer.on("/controller/pause",HTTP_POST,[](){enterBenchMode();redirect("/bench");});
 benchServer.on("/controller/defaults",HTTP_POST,restoreControllerDefaults);
 benchServer.on("/get-settings",HTTP_GET,[](){
  auto p=selectedParams();StaticJsonDocument<1024>d;
  d["slider-p"]=p.p;d["slider-i"]=p.i;d["slider-d"]=p.d;d["slider-setpoint"]=p.setpoint;
  d["slider-factor"]=p.factor;d["slider-enable"]=p.enable;d["slider-calibration"]=p.calibration;
  d["slider-servo-min"]=p.servoMin;d["slider-servo-max"]=p.servoMax;d["board-selector"]=boardSelector;sendJson(d);
 });
 benchServer.on("/get-values",HTTP_GET,[](){StaticJsonDocument<1024>d;{ControlLock lock;
  d["PID-input"]=input;d["PID-output"]=output;d["PID-setpoint"]=setpoint;d["PWM-input"]=pwmRead;d["Servo-position"]=servoPos;
  d["Min-measured-value"]=calibrationSession.active?calibrationSession.minimum:controlParams.minMeasured;d["Max-measured-value"]=calibrationSession.active?calibrationSession.maximum:controlParams.maxMeasured;d["Actual-measured-value"]=median;}sendJson(d);
 });
 benchServer.on("/get-addresses",HTTP_GET,[](){StaticJsonDocument<512>d;d["broadcastAddress0"]=macAddresses[0];d["broadcastAddress1"]=macAddresses[1];sendJson(d);});
 benchServer.on("/select-board",HTTP_POST,[](){String v=benchServer.arg("board-selector");if(v!="master"&&v!="slave-1"&&v!="slave-2"){benchServer.send(400,"text/plain","Unknown board");return;}boardSelector=v;benchServer.send(200,"text/plain","OK");});
 benchServer.on("/set-sliders",HTTP_POST,setSliders);
 benchServer.on("/add-mac",HTTP_POST,[](){
  if(slave){benchServer.send(409,"text/plain","Add peers on the master");return;}
  uint8_t bytes[6];String address=benchServer.arg("mac");address.replace('-',':');address.toUpperCase();
  if(!parseMacAddress(address.c_str(),bytes)){benchServer.send(400,"text/plain","Invalid MAC address");return;}
  if(address==macAddresses[0]||address==macAddresses[1]){benchServer.send(200,"text/plain","Already added");return;}
  int n=macAddresses[0].isEmpty()?0:macAddresses[1].isEmpty()?1:-1;
  if(n<0){benchServer.send(409,"text/plain","Two peers already configured");return;}
  String proposed[2]={macAddresses[0],macAddresses[1]};proposed[n]=address;
  if(!writeArrayJson(SPIFFS,jsonAddressesPath,"addresses",proposed,2)){benchServer.send(500,"text/plain","Could not save peer");return;}
  macAddresses[n]=address;sendPeer(n);benchServer.send(200,"text/plain","OK");
 });
 benchServer.on("/remove-slaves",HTTP_POST,[](){
  if(!benchServer.hasArg("remove-slaves")){benchServer.send(400,"text/plain","Missing action");return;}
  String empty[2];if(!writeArrayJson(SPIFFS,jsonAddressesPath,"addresses",empty,2)){benchServer.send(500,"text/plain","Could not save peers");return;}
  for(auto &address:macAddresses){uint8_t mac[6];if(parseMacAddress(address.c_str(),mac))esp_now_del_peer(mac);address="";}benchServer.send(200,"text/plain","OK");
 });
 benchServer.on("/wifi-off",HTTP_POST,[](){if(!benchServer.hasArg("wifi-off")){benchServer.send(400,"text/plain","Missing action");return;}
  benchServer.send(200,"text/plain","Hotspot will hide until restart; ESP-NOW stays on");hideHotspot=true;hideHotspotAt=millis()+1000;
 });
}
void runControllerTask(void*) {
 for(;;) {
  {
   ControlLock lock;
   if(!benchActive&&!otaInProgress&&settingsReady&&!first) {
    rmtTicker.update();measurementTicker.update();
    if(servoTimeOut&&uint32_t(millis()-servoTimeOutStart)>=servoTimeOutInterval)servoTimeOut=false;
    if(!servoTimeOut)pidTicker.update();
    digitalWrite(ledPin,positionValid&&abs(int(input)-int(setpoint))<30?LOW:HIGH);
   }
  }
  vTaskDelay(1);
 }
}
void setup() {
 Serial.begin(115200);pinMode(capacitancePin,INPUT);pinMode(ledPin,OUTPUT);pinMode(resetPin,INPUT_PULLUP);
 controlMutex=xSemaphoreCreateRecursiveMutex();
 rawValues.reserve(20);
 benchFilesystem=initFS();
 bool configs=benchFilesystem&&readStructJson(SPIFFS,jsonConfigsPath,boardsParams);
 String firstFlag=readFileJson(SPIFFS,jsonWifiPath,"first"),slaveFlag=readFileJson(SPIFFS,jsonWifiPath,"slave");
 auto validFlag=[](const String &v){return v=="true"||v=="false"||v=="True"||v=="False"||v=="0"||v=="1";};
 auto truth=[](const String &v){return v=="true"||v=="True"||v=="1";};
 first=truth(firstFlag);slave=truth(slaveFlag);
 settingsReady=configs&&validControlParams(boardsParams.master)&&validControlParams(boardsParams.slave1)&&validControlParams(boardsParams.slave2)&&validFlag(firstFlag)&&validFlag(slaveFlag);
 if(!readArrayJson(SPIFFS,jsonAddressesPath,"addresses",macAddresses,2))controllerError="Saved peer addresses unavailable";
 controlParams=slave?boardsParams.slave1:boardsParams.master;
 if(controlParams.calibration==1)calibrationNotice="Calibration interrupted by restart. Turn OFF, then ON to start a new calibration";
 if(!settingsReady)controllerError="Saved settings unavailable or invalid; controller paused";
 WiFi.mode(WIFI_AP_STA);WiFi.setSleep(false);
 String suffix=WiFi.softAPmacAddress();suffix.replace(":","");
 benchHostname="HF-Bench-"+suffix;
 WiFi.softAP(benchHostname.c_str(),nullptr,1);
 timer=timerBegin(0,2,true);
 setupEspNow();setupRMTReceiver();
 benchActive=true;
 configureRoutes();setupBenchRoutes();benchServer.begin();
 measurementTicker.start();pidTicker.start();rmtTicker.start();
 if(controlMutex&&xTaskCreate(runControllerTask,"foil-control",4096,nullptr,2,&controlTaskHandle)==pdPASS) {
  if(settingsReady&&!first)resumeController();
 }else controllerError="Control scheduler unavailable; bench access retained";
 if(settingsReady&&!slave){sendPeer(0);sendPeer(1);}
 // No startup settings writes or factory reset. Existing router credentials stay stored.
}
void loop() {
 ArduinoOTA.handle();benchServer.handleClient();
 if(restartPending&&int32_t(millis()-restartAt)>=0)ESP.restart();
 if(hideHotspot&&int32_t(millis()-hideHotspotAt)>=0){WiFi.softAPdisconnect(true);hideHotspot=false;hotspotVisible=false;}
 // A brief jumper-to-ground gesture restores the hotspot without changing saved settings.
 if(!hotspotVisible&&digitalRead(resetPin)==LOW){WiFi.softAP(benchHostname.c_str(),nullptr,1);hotspotVisible=true;}
 if(!otaInProgress){
  serviceReceivedConfigs();
  if(benchActive){ControlLock lock;serviceBenchSamples();}

 }
 delay(1);
}
