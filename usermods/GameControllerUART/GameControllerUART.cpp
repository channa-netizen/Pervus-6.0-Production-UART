#include "wled.h"

class GameControllerUART : public Usermod {
private:

  // =====================================================
  // HARDWARE
  // =====================================================

  static constexpr int RX_PIN = 18;
  static constexpr uint32_t BAUD = 115200;

  HardwareSerial gameSerial = HardwareSerial(2);

  String rxBuffer;


  // =====================================================
  // PRESET TRANSITIONS
  // =====================================================

  static constexpr uint16_t NORMAL_TRANSITION_MS = 700;
  static constexpr uint16_t BANGER_TRANSITION_MS = 0;


  // =====================================================
  // STROBE
  // =====================================================

  bool strobeActive = false;
  bool strobePhaseOn = true;

  uint32_t lastStrobeToggle = 0;

  // About 5.9 complete flashes/sec:
  // 85 ms ON + 85 ms OFF.
  static constexpr uint32_t STROBE_INTERVAL_MS = 85;


  // =====================================================
  // STATE UPDATE
  // =====================================================

  void commitChange() {
    stateChanged = true;
    stateUpdated(CALL_MODE_DIRECT_CHANGE);
  }


  // =====================================================
  // PRESET WITH TRANSITION
  // =====================================================

  void applyControllerPreset(uint8_t preset) {

    uint16_t transitionMs = NORMAL_TRANSITION_MS;

    // D-pad presets 9-12 = instant bangers.
    if (preset >= 9 && preset <= 12) {
      transitionMs = BANGER_TRANSITION_MS;
    }

    // Use WLED's native transition engine directly. The transition is
    // marked one-shot so WLED restores its normal configured transition
    // after this preset change. applyPreset() queues the preset through
    // WLED's normal asynchronous preset loader.
    jsonTransitionOnce = true;
    strip.setTransition(transitionMs);
    applyPreset(preset, CALL_MODE_BUTTON_PRESET);
  }


  // =====================================================
  // POWER TOGGLE
  // =====================================================

  void togglePower() {

    String request = "win&T=2";

    handleSet(nullptr, request, false);

    stateUpdated(CALL_MODE_BUTTON);
  }


  // =====================================================
  // STROBE CONTROL
  //
  // IMPORTANT:
  // This does NOT change bri itself.
  //
  // bri = actual WLED brightness
  // briT = temporary/output brightness
  //
  // This lets the underlying effect continue running.
  // =====================================================

  void startStrobe() {

    if (strobeActive) return;

    strobeActive = true;
    strobePhaseOn = true;

    lastStrobeToggle = millis();

    // Start immediately at normal brightness.
    briT = bri;

    applyBri();
  }


  void stopStrobe() {

    if (!strobeActive) return;

    strobeActive = false;

    // Restore the current real WLED brightness.
    briT = bri;

    applyBri();
  }


  void handleStrobe() {

    if (!strobeActive) return;

    uint32_t now = millis();

    if (
      now - lastStrobeToggle <
      STROBE_INTERVAL_MS
    ) {
      return;
    }

    lastStrobeToggle = now;

    strobePhaseOn = !strobePhaseOn;

    if (strobePhaseOn) {
      briT = bri;
    }
    else {
      briT = 0;
    }

    // Apply directly to output brightness.
    // No stateUpdated() spam.
    applyBri();
  }


  // =====================================================
  // GLOBAL SPEED
  //
  // WLED technically stores speed per segment.
  // We apply the same value/delta to every active segment.
  //
  // Range:
  // 64  = 25%
  // 191 = 75%
  // =====================================================

  void setGlobalSpeed(int value) {

    value = constrain(value, 64, 191);

    bool changed = false;

    for (
      size_t s = 0;
      s < strip.getSegmentsNum();
      s++
    ) {

      Segment& seg = strip.getSegment(s);

      if (!seg.isActive()) {
        continue;
      }

      if (seg.speed != value) {
        seg.speed = value;
        changed = true;
      }
    }

    if (changed) {
      commitChange();
    }
  }


  void changeGlobalSpeed(int delta) {

    bool changed = false;

    for (
      size_t s = 0;
      s < strip.getSegmentsNum();
      s++
    ) {

      Segment& seg = strip.getSegment(s);

      if (!seg.isActive()) {
        continue;
      }

      int newValue = constrain(
        (int)seg.speed + delta,
        64,
        191
      );

      if (newValue != seg.speed) {
        seg.speed = newValue;
        changed = true;
      }
    }

    if (changed) {
      commitChange();
    }
  }


  // =====================================================
  // ABSOLUTE CONTROLS
  // =====================================================

  void setAbsolute(
    const String& type,
    int value
  ) {

    // Speed is special:
    // apply it to ALL active segments.
    if (type == "SX") {

      setGlobalSpeed(value);

      return;
    }


    Segment& seg =
      strip.getMainSegment();

    bool changed = false;


    // ---------------------------------------------------
    // CUSTOM 1
    // ---------------------------------------------------

    if (type == "C1") {

      value =
        constrain(value, 0, 255);

      if (seg.custom1 != value) {

        seg.custom1 = value;

        changed = true;
      }
    }


    // ---------------------------------------------------
    // CUSTOM 2
    // ---------------------------------------------------

    else if (type == "C2") {

      value =
        constrain(value, 0, 255);

      if (seg.custom2 != value) {

        seg.custom2 = value;

        changed = true;
      }
    }


    // ---------------------------------------------------
    // CUSTOM 3
    //
    // WLED custom3 is 0-31.
    // ---------------------------------------------------

    else if (type == "C3") {

      value =
        constrain(value, 0, 31);

      if (seg.custom3 != value) {

        seg.custom3 = value;

        changed = true;
      }
    }


    // ---------------------------------------------------
    // INTENSITY
    // ---------------------------------------------------

    else if (type == "IX") {

      value =
        constrain(value, 0, 255);

      if (seg.intensity != value) {

        seg.intensity = value;

        changed = true;
      }
    }


    if (changed) {
      commitChange();
    }
  }


  // =====================================================
  // RELATIVE CONTROLS
  // =====================================================

  void applyDelta(
    const String& type,
    int delta
  ) {

    // Speed is global-ish.
    if (type == "SXD") {

      changeGlobalSpeed(delta);

      return;
    }


    Segment& seg =
      strip.getMainSegment();

    int newValue = 0;

    bool changed = false;


    // ---------------------------------------------------
    // CUSTOM 1
    // ---------------------------------------------------

    if (type == "C1D") {

      newValue = constrain(
        (int)seg.custom1 + delta,
        0,
        255
      );

      if (newValue != seg.custom1) {

        seg.custom1 = newValue;

        changed = true;
      }
    }


    // ---------------------------------------------------
    // CUSTOM 2
    // ---------------------------------------------------

    else if (type == "C2D") {

      newValue = constrain(
        (int)seg.custom2 + delta,
        0,
        255
      );

      if (newValue != seg.custom2) {

        seg.custom2 = newValue;

        changed = true;
      }
    }


    // ---------------------------------------------------
    // CUSTOM 3
    // ---------------------------------------------------

    else if (type == "C3D") {

      newValue = constrain(
        (int)seg.custom3 + delta,
        0,
        31
      );

      if (newValue != seg.custom3) {

        seg.custom3 = newValue;

        changed = true;
      }
    }


    // ---------------------------------------------------
    // INTENSITY
    // ---------------------------------------------------

    else if (type == "IXD") {

      newValue = constrain(
        (int)seg.intensity + delta,
        0,
        255
      );

      if (newValue != seg.intensity) {

        seg.intensity = newValue;

        changed = true;
      }
    }


    if (changed) {
      commitChange();
    }
  }


  // =====================================================
  // PALETTE
  // =====================================================

  void setPalette(int value) {

    Segment& seg =
      strip.getMainSegment();

    // Sender currently chooses from this range.
    value =
      constrain(value, 0, 71);

    uint8_t oldPalette =
      seg.palette;


    seg.setPalette(
      (uint8_t)value
    );


    if (seg.palette != oldPalette) {

      commitChange();
    }
  }




  // =====================================================
  // LIVE CONTROLLER EXTRAS
  // =====================================================

  void cycleEffect(int direction) {
    Segment& seg = strip.getMainSegment();
    int modeCount = (int)strip.getModeCount();
    if (modeCount <= 0) return;

    int next = ((int)seg.mode + (direction >= 0 ? 1 : -1) + modeCount) % modeCount;
    if (next != seg.mode) {
      seg.setMode((uint8_t)next, true);
      commitChange();
    }
  }

  void randomizeLiveParameters() {
    Segment& seg = strip.getMainSegment();
    uint8_t modeCount = strip.getModeCount();
    if (modeCount > 1) {
      uint8_t fx = (uint8_t)random(0, modeCount);
      seg.setMode(fx, true); // load that effect's own defaults first
    }
    seg.setPalette((uint8_t)random(0, 72));
    seg.speed = (uint8_t)random(64, 192);
    seg.intensity = (uint8_t)random(0, 256);
    seg.custom1 = (uint8_t)random(0, 256);
    seg.custom2 = (uint8_t)random(0, 256);
    seg.custom3 = (uint8_t)random(0, 32);
    commitChange();
  }

  // LY is effect-aware: use Custom 3 when the current effect
  // exposes c3; otherwise use Intensity.
  bool currentEffectHasC3() {
    Segment& seg = strip.getMainSegment();
    return extractModeDefaults(seg.mode, "c3") >= 0;
  }

  void applyLyDelta(int delta) {
    Segment& seg = strip.getMainSegment();

    if (currentEffectHasC3()) {
      int newValue = constrain((int)seg.custom3 + delta, 0, 31);
      if (newValue != seg.custom3) {
        seg.custom3 = newValue;
        commitChange();
      }
    } else {
      // LY sender is intentionally gentle (max +/-2). Apply that
      // same relative step to intensity when c3 is unavailable.
      int newValue = constrain((int)seg.intensity + delta, 0, 255);
      if (newValue != seg.intensity) {
        seg.intensity = newValue;
        commitChange();
      }
    }
  }

  void resetLeftStickParameters() {
    Segment& seg = strip.getMainSegment();
    int sx = extractModeDefaults(seg.mode, "sx");
    int c3 = extractModeDefaults(seg.mode, "c3");
    int ix = extractModeDefaults(seg.mode, "ix");

    setGlobalSpeed(sx >= 0 ? sx : DEFAULT_SPEED);

    if (c3 >= 0) {
      seg.custom3 = (uint8_t)constrain(c3, 0, 31);
    } else {
      seg.intensity = (uint8_t)constrain(ix >= 0 ? ix : DEFAULT_INTENSITY, 0, 255);
    }

    commitChange();
  }

  void resetRightStickParameters() {
    Segment& seg = strip.getMainSegment();
    int c1 = extractModeDefaults(seg.mode, "c1");
    int c2 = extractModeDefaults(seg.mode, "c2");
    seg.custom1 = (uint8_t)constrain(c1 >= 0 ? c1 : DEFAULT_C1, 0, 255);
    seg.custom2 = (uint8_t)constrain(c2 >= 0 ? c2 : DEFAULT_C2, 0, 255);
    commitChange();
  }


  // =====================================================
  // COMMAND PARSER
  // =====================================================

  void processCommand(
    String command
  ) {

    command.trim();


    if (
      command.length() == 0
    ) {
      return;
    }


    int separator =
      command.indexOf(':');


    if (separator <= 0) {
      return;
    }


    String type =
      command.substring(
        0,
        separator
      );


    String valueString =
      command.substring(
        separator + 1
      );


    type.toUpperCase();


    int value =
      valueString.toInt();


    // ===================================================
    // PRESET
    //
    // P:1
    //
    // 9-12 = 0 ms
    // everything else = 700 ms
    // ===================================================

    if (type == "P") {

      if (
        value >= 1 &&
        value <= 250
      ) {

        applyControllerPreset(
          (uint8_t)value
        );
      }

      return;
    }


    // ===================================================
    // POWER
    //
    // PWR:1
    //
    // Value is ignored.
    // ===================================================

    if (type == "PWR") {

      togglePower();

      return;
    }


    // ===================================================
    // MOMENTARY STROBE
    //
    // STB:1 = start
    // STB:0 = stop
    // ===================================================

    if (type == "STB") {

      if (value == 1) {

        startStrobe();
      }

      else {

        stopStrobe();
      }

      return;
    }


    // ===================================================
    // PALETTE
    //
    // PAL:35
    // ===================================================

    if (type == "PAL") {

      setPalette(value);

      return;
    }


    // ===================================================
    // EFFECT CYCLE / RANDOMIZE / STICK RESETS
    // ===================================================

    if (type == "FX") {
      cycleEffect(value);
      return;
    }

    if (type == "RND") {
      randomizeLiveParameters();
      return;
    }

    if (type == "RSTL") {
      resetLeftStickParameters();
      return;
    }

    if (type == "RSTR") {
      resetRightStickParameters();
      return;
    }


    // ===================================================
    // RELATIVE CONTROLS
    // ===================================================

    if (
      type == "C1D" ||
      type == "C2D" ||
      type == "C3D" ||
      type == "IXD" ||
      type == "SXD" ||
      type == "LYD"
    ) {

      if (type == "LYD") {
        applyLyDelta(value);
      } else {
        applyDelta(
          type,
          value
        );
      }

      return;
    }


    // ===================================================
    // ABSOLUTE CONTROLS
    //
    // Kept for backwards compatibility.
    // ===================================================

    if (
      type == "C1" ||
      type == "C2" ||
      type == "C3" ||
      type == "IX" ||
      type == "SX"
    ) {

      setAbsolute(
        type,
        value
      );

      return;
    }
  }


public:

  // =====================================================
  // SETUP
  // =====================================================

  void setup() override {

    rxBuffer.reserve(32);


    gameSerial.begin(
      BAUD,
      SERIAL_8N1,
      RX_PIN,
      -1
    );
  }


  // =====================================================
  // LOOP
  // =====================================================

  void loop() override {

    // ---------------------------------------------------
    // UART INPUT
    // ---------------------------------------------------

    while (
      gameSerial.available()
    ) {

      char c =
        (char)gameSerial.read();


      if (c == '\n') {

        if (
          rxBuffer.length() > 0
        ) {

          processCommand(
            rxBuffer
          );

          rxBuffer = "";
        }
      }


      else if (c != '\r') {

        if (
          rxBuffer.length() < 31
        ) {

          rxBuffer += c;
        }

        else {

          // Dump malformed/oversized command.
          rxBuffer = "";
        }
      }
    }


    // ---------------------------------------------------
    // STROBE OUTPUT
    // ---------------------------------------------------

    handleStrobe();
  }


  // =====================================================
  // USERMOD ID
  // =====================================================

  uint16_t getId() override {

    return USERMOD_ID_UNSPECIFIED;
  }
};