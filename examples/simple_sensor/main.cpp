#include "SensorMesh.h"

#ifdef PIR_PIN
  // PIR motion sensor: the pin goes HIGH on motion. The PIRMINI-3 holds the output only ~2 s,
  // far shorter than SENSOR_READ_INTERVAL_SECS, so the edge is latched by an interrupt.
  #ifndef PIR_ALERT_INTERVAL_SECS
    #define PIR_ALERT_INTERVAL_SECS  60     // minimum gap between two motion alerts
  #endif
  #ifndef PIR_WARMUP_SECS
    #define PIR_WARMUP_SECS  30             // PIR output is unreliable right after power-on
  #endif
  #ifndef PIR_ALERT_TEXT
    #define PIR_ALERT_TEXT  "Mozgás érzékelve!"
  #endif

  #define PIR_SUSPEND_FILE  "/pir_suspend"
  #define PIR_SUSPEND_MAX_SECS  86400       // upper bound for the 'suspend' CLI value

  static volatile bool pir_motion = false;
  static volatile uint32_t pir_edges = 0;   // rising edges seen since boot (diagnostics)

  static void IRAM_ATTR onPirRising() {
    pir_motion = true;
    pir_edges++;
  }
#endif

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

class MyMesh : public SensorMesh {
public:
  MyMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables)
     : SensorMesh(board, radio, ms, rng, rtc, tables), 
       battery_data(12*24, 5*60)    // 24 hours worth of battery data, every 5 minutes
  {
  }

protected:
  /* ========================== custom logic here ========================== */
  Trigger low_batt, critical_batt;
#ifdef PIR_PIN
  Trigger motion;
  unsigned long last_motion_alert = 0;
  bool motion_alerted = false;
  uint32_t pir_suspend_secs = PIR_ALERT_INTERVAL_SECS;   // minimum gap between two alerts, set by 'suspend'
  FILESYSTEM* pir_fs = NULL;

  void loadPirSuspend() {
  #if defined(ESP32)
    if (pir_fs == NULL || !pir_fs->exists(PIR_SUSPEND_FILE)) return;
    File f = pir_fs->open(PIR_SUSPEND_FILE, "r");
    if (!f) return;
    uint32_t v;
    if (f.read((uint8_t *)&v, sizeof(v)) == sizeof(v) && v <= PIR_SUSPEND_MAX_SECS) pir_suspend_secs = v;
    f.close();
  #endif
  }

  bool savePirSuspend() {
  #if defined(ESP32)
    if (pir_fs == NULL) return false;
    File f = pir_fs->open(PIR_SUSPEND_FILE, "w");
    if (!f) return false;
    bool ok = f.write((const uint8_t *)&pir_suspend_secs, sizeof(pir_suspend_secs)) == sizeof(pir_suspend_secs);
    f.close();
    return ok;
  #else
    return false;   // no persistence on this platform: the value lasts until reboot
  #endif
  }

public:
  void beginPir(FILESYSTEM* fs) {
    pir_fs = fs;
    loadPirSuspend();
    // plain INPUT, no pull-down: the PIRMINI-3 output is weak (measured 2026-10-28: 3.01 V unloaded,
    // 1.66 V with the ~45 kOhm internal pull-down, below the ESP32-S3 HIGH threshold); it drives low by itself
    pinMode(PIR_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIR_PIN), onPirRising, RISING);
  }

  void loopPir() {
    if (!pir_motion) return;
    pir_motion = false;

    unsigned long now = millis();
    Serial.printf("PIR edge #%lu: ", (unsigned long)pir_edges);
    if (now < PIR_WARMUP_SECS * 1000UL) {   // ignore power-on noise
      Serial.println("ignored (warm-up)");
      return;
    }
    if (motion_alerted && now - last_motion_alert < pir_suspend_secs * 1000UL) {   // rate limit
      Serial.printf("suppressed (suspend, %lu s left)\n", (unsigned long)((pir_suspend_secs * 1000UL - (now - last_motion_alert)) / 1000));
      return;
    }
    queueMotionAlert();
    Serial.println("alert queued");
  }

  void queueMotionAlert() {
    alertIf(false, motion, HIGH_PRI_ALERT, PIR_ALERT_TEXT);   // re-arm: a finished alert stays 'triggered' until cleared
    alertIf(true, motion, HIGH_PRI_ALERT, PIR_ALERT_TEXT);    // queue alert: direct msg to every client with alert permission
    last_motion_alert = millis();
    motion_alerted = true;
  }

protected:
#endif
  TimeSeriesData  battery_data;

  void onSensorDataRead() override {
    float batt_voltage = getVoltage(TELEM_CHANNEL_SELF);

    battery_data.recordData(getRTCClock(), batt_voltage);   // record battery
    alertIf(batt_voltage < 3.4f, critical_batt, HIGH_PRI_ALERT, "Battery is critical!");
    alertIf(batt_voltage < 3.6f, low_batt, LOW_PRI_ALERT, "Battery is low");
  }

  int querySeriesData(uint32_t start_secs_ago, uint32_t end_secs_ago, MinMaxAvg dest[], int max_num) override {
    battery_data.calcMinMaxAvg(getRTCClock(), start_secs_ago, end_secs_ago, &dest[0], TELEM_CHANNEL_SELF, LPP_VOLTAGE);
    return 1;
  }

  bool handleCustomCommand(uint32_t sender_timestamp, char* command, char* reply) override {
#ifdef PIR_PIN
    // diagnostics: "pir pin" reads the input, "pir test" queues an alert without motion
    if (strcmp(command, "pir pin") == 0) {
      sprintf(reply, "PIR pin: %s, edges: %lu", digitalRead(PIR_PIN) ? "HIGH" : "LOW", (unsigned long)pir_edges);
      return true;
    }
    if (strcmp(command, "pir test") == 0) {
      queueMotionAlert();
      strcpy(reply, "PIR test alert queued");
      return true;
    }
    // "suspend" shows, "suspend {secs}" sets the minimum gap between two motion alerts (persisted)
    if (strcmp(command, "suspend") == 0) {
      sprintf(reply, "PIR suspend: %lu s", (unsigned long)pir_suspend_secs);
      return true;
    }
    if (memcmp(command, "suspend ", 8) == 0) {
      char* end;
      unsigned long v = strtoul(&command[8], &end, 10);
      if (end == &command[8] || *end != 0 || v > PIR_SUSPEND_MAX_SECS) {
        sprintf(reply, "Err - usage: suspend {0..%d}", PIR_SUSPEND_MAX_SECS);
      } else {
        pir_suspend_secs = v;
        sprintf(reply, savePirSuspend() ? "PIR suspend: %lu s" : "PIR suspend: %lu s (not saved)", v);
      }
      return true;
    }
#endif
    if (strcmp(command, "magic") == 0) {    // example 'custom' command handling
      strcpy(reply, "**Magic now done**");
      return true;   // handled
    }
    return false;  // not handled
  }
  /* ======================================================================= */
};

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[160];

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_driver.getRngSeed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Sensor ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;

  sensors.begin();

  the_mesh.begin(fs);

#ifdef PIR_PIN
  the_mesh.beginPir(fs);
#endif

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

  // send out initial zero hop Advertisement to the mesh
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif
}

void loop() {
  int len = strlen(command);
  // `command` must stay NUL-terminated within its bounds. If it ever isn't,
  // strlen() above can return >= sizeof(command) and the loop below would then
  // index past the buffer, so clamp defensively.
  if (len >= (int)sizeof(command)) {
    command[0] = 0;
    len = 0;
  }
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
    }
    Serial.print(c);
  }
  if (len == sizeof(command)-1) {  // buffer full: treat as a completed line
    command[sizeof(command)-2] = '\r';  // place end-of-line marker inside the buffer
    command[sizeof(command)-1] = 0;     // keep the buffer NUL-terminated
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    the_mesh.handleCommand(NULL, 0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

  board.loop();   // let the board feed its watchdog, run periodic housekeeping

#ifdef PIR_PIN
  the_mesh.loopPir();
#endif
  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif
}
