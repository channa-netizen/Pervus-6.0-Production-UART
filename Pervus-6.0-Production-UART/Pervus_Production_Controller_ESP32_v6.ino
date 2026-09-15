#include <Arduino.h>
#include <Bluepad32.h>

void sendPresetUart(uint8_t preset);

extern "C" {
  #include "btstack.h"
}

// =====================================================
// BLUEPAD32
// =====================================================

ControllerPtr myController = nullptr;

void onConnectedController(ControllerPtr ctl) {
  myController = ctl;

  Serial.println();
  Serial.println("====================================");
  Serial.println("GAMEPAD CONNECTED");
  Serial.println("====================================");
  Serial.print("Model: ");
  Serial.println(ctl->getModelName());
}

void onDisconnectedController(ControllerPtr ctl) {
  if (myController == ctl) {
    myController = nullptr;
  }

  Serial.println("GAMEPAD DISCONNECTED");
}


// =====================================================
// MIDI SETTINGS
// =====================================================

// Velocity must be GREATER than 5.
const uint8_t MIN_VELOCITY = 5;

// BLE MIDI UUIDs
static const uint8_t MIDI_SERVICE_UUID[16] = {
  0x03, 0xB8, 0x0E, 0x5A,
  0xED, 0xE8,
  0x4B, 0x33,
  0xA7, 0x51,
  0x6C, 0xE3, 0x4E, 0xC4, 0xC7, 0x00
};

static const uint8_t MIDI_CHAR_UUID[16] = {
  0x77, 0x72, 0xE5, 0xDB,
  0x38, 0x68,
  0x41, 0x12,
  0xA1, 0xA9,
  0xF2, 0x66, 0x9D, 0x10, 0x6B, 0xF3
};


// =====================================================
// MIDI NOTE MAP
// =====================================================

int noteToPreset(uint8_t note) {

  switch (note) {

    case 41: return 1;
    case 45: return 2;
    case 48: return 3;
    case 52: return 4;

    case 42: return 5;
    case 46: return 6;
    case 44: return 7;
    case 54: return 8;

    case 37: return 9;
    case 38: return 10;
    case 40: return 11;
    case 39: return 12;

    case 49: return 13;
    case 35: return 14;
    case 36: return 15;
    case 51: return 16;

    default:
      return 0;
  }
}


// =====================================================
// MIDI PARSER
// =====================================================

void parseMidiPacket(const uint8_t* data, uint16_t len) {

  if (len < 5) {
    Serial.print("Short BLE MIDI packet: ");
    Serial.println(len);
    return;
  }

  // Same packet layout observed previously:
  //
  // byte 0 = BLE timestamp
  // byte 1 = BLE timestamp
  // byte 2 = MIDI status
  // byte 3 = note
  // byte 4 = velocity

  uint8_t status   = data[2];
  uint8_t type     = status & 0xF0;
  uint8_t channel  = (status & 0x0F) + 1;

  uint8_t note     = data[3];
  uint8_t velocity = data[4];

  // Only Note On
  if (type != 0x90) {
    return;
  }

  // Velocity zero is effectively Note Off
  if (velocity == 0) {
    return;
  }

  Serial.println();
  Serial.println("------------------------------------");
  Serial.println("BLE MIDI NOTE ON");
  Serial.println("------------------------------------");

  Serial.print("Channel:  ");
  Serial.println(channel);

  Serial.print("Note:     ");
  Serial.println(note);

  Serial.print("Velocity: ");
  Serial.println(velocity);

  if (velocity <= MIN_VELOCITY) {
    Serial.println("RESULT: IGNORED - velocity <= 5");
    return;
  }

  int preset = noteToPreset(note);

  if (preset == 0) {
    Serial.println("RESULT: UNMAPPED NOTE");
    return;
  }

  Serial.print("RESULT: PRESET ");
  Serial.println(preset);

  sendPresetUart((uint8_t)preset);
}


// =====================================================
// BTSTACK MIDI STATE
// =====================================================

enum MidiState {
  MIDI_IDLE,
  MIDI_SCANNING,
  MIDI_CONNECTING,
  MIDI_FINDING_SERVICE,
  MIDI_FINDING_CHARACTERISTIC,
  MIDI_SUBSCRIBING,
  MIDI_READY
};

volatile MidiState midiState = MIDI_IDLE;

bd_addr_t midiAddress;
bd_addr_type_t midiAddressType;

hci_con_handle_t midiConnectionHandle = HCI_CON_HANDLE_INVALID;

gatt_client_service_t midiService;
gatt_client_characteristic_t midiCharacteristic;

gatt_client_notification_t midiNotificationListener;

static btstack_packet_callback_registration_t hciEventCallback;


// =====================================================
// ADVERTISEMENT NAME PARSER
// =====================================================

bool advertisementHasName(
  const uint8_t* advData,
  uint8_t advLen,
  const char* target
) {

  ad_context_t context;

  for (
    ad_iterator_init(&context, advLen, advData);
    ad_iterator_has_more(&context);
    ad_iterator_next(&context)
  ) {

    uint8_t type = ad_iterator_get_data_type(&context);

    if (
      type != BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME &&
      type != BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME
    ) {
      continue;
    }

    uint8_t len = ad_iterator_get_data_len(&context);
    const uint8_t* data = ad_iterator_get_data(&context);

    size_t targetLen = strlen(target);

    if (len != targetLen) {
      continue;
    }

    if (memcmp(data, target, len) == 0) {
      return true;
    }
  }

  return false;
}


// =====================================================
// START SCAN
// =====================================================

void startMidiScanBTstack(void* context) {

  (void)context;

  if (midiState == MIDI_READY) {
    return;
  }

  Serial.println();
  Serial.println("Scanning for SMC-PAD Pocket...");

  // Active scan
  gap_set_scan_parameters(
    1,
    0x0060,
    0x0030
  );

  gap_set_scan_duplicate_filter(true);

  midiState = MIDI_SCANNING;

  gap_start_scan();
}


// =====================================================
// SCHEDULE BTSTACK WORK SAFELY
// =====================================================

btstack_context_callback_registration_t scanCallback;

void requestMidiScan() {

  scanCallback.callback = &startMidiScanBTstack;
  scanCallback.context = nullptr;

  btstack_run_loop_execute_on_main_thread(
    &scanCallback
  );
}


// =====================================================
// GATT EVENT HANDLER
// =====================================================

void midiGattPacketHandler(
  uint8_t packetType,
  uint16_t channel,
  uint8_t* packet,
  uint16_t size
) {

  if (packetType != HCI_EVENT_PACKET) {
    return;
  }

  uint8_t event = hci_event_packet_get_type(packet);

  switch (event) {

    // -------------------------------------------------
    // SERVICE FOUND
    // -------------------------------------------------

    case GATT_EVENT_SERVICE_QUERY_RESULT: {

      gatt_event_service_query_result_get_service(
        packet,
        &midiService
      );

      Serial.println("BLE MIDI service found.");
      break;
    }


    // -------------------------------------------------
    // CHARACTERISTIC FOUND
    // -------------------------------------------------

    case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT: {

      gatt_client_characteristic_t characteristic;

      gatt_event_characteristic_query_result_get_characteristic(
        packet,
        &characteristic
      );

      if (
        memcmp(
          characteristic.uuid128,
          MIDI_CHAR_UUID,
          16
        ) == 0
      ) {

        midiCharacteristic = characteristic;

        Serial.println(
          "BLE MIDI characteristic found."
        );
      }

      break;
    }


    // -------------------------------------------------
    // MIDI NOTIFICATION
    // -------------------------------------------------

    case GATT_EVENT_NOTIFICATION: {

      uint16_t valueHandle =
        gatt_event_notification_get_value_handle(
          packet
        );

      if (
        valueHandle !=
        midiCharacteristic.value_handle
      ) {
        return;
      }

      uint16_t valueLength =
        gatt_event_notification_get_value_length(
          packet
        );

      const uint8_t* value =
        gatt_event_notification_get_value(
          packet
        );

      parseMidiPacket(
        value,
        valueLength
      );

      break;
    }


    // -------------------------------------------------
    // QUERY FINISHED
    // -------------------------------------------------

    case GATT_EVENT_QUERY_COMPLETE: {

      uint8_t status =
        gatt_event_query_complete_get_att_status(
          packet
        );

      if (status != ATT_ERROR_SUCCESS) {

        Serial.print(
          "GATT query failed. ATT status: 0x"
        );

        Serial.println(
          status,
          HEX
        );

        return;
      }


      // -----------------------------
      // SERVICE DISCOVERY COMPLETE
      // -----------------------------

      if (midiState == MIDI_FINDING_SERVICE) {

        Serial.println(
          "Searching MIDI characteristic..."
        );

        midiState =
          MIDI_FINDING_CHARACTERISTIC;

        gatt_client_discover_characteristics_for_service(
          midiGattPacketHandler,
          midiConnectionHandle,
          &midiService
        );

        return;
      }


      // -----------------------------
      // CHARACTERISTIC DISCOVERY DONE
      // -----------------------------

      if (
        midiState ==
        MIDI_FINDING_CHARACTERISTIC
      ) {

        if (
          midiCharacteristic.value_handle == 0
        ) {

          Serial.println(
            "ERROR: MIDI characteristic not found."
          );

          return;
        }

        Serial.println(
          "Subscribing to BLE MIDI..."
        );

        midiState = MIDI_SUBSCRIBING;

        gatt_client_listen_for_characteristic_value_updates(
          &midiNotificationListener,
          midiGattPacketHandler,
          midiConnectionHandle,
          &midiCharacteristic
        );

        gatt_client_write_client_characteristic_configuration(
          midiGattPacketHandler,
          midiConnectionHandle,
          &midiCharacteristic,
          GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION
        );

        return;
      }


      // -----------------------------
      // SUBSCRIPTION COMPLETE
      // -----------------------------

      if (midiState == MIDI_SUBSCRIBING) {

        midiState = MIDI_READY;

        Serial.println();
        Serial.println("====================================");
        Serial.println("SMC-PAD BLE MIDI READY");
        Serial.println("====================================");
        Serial.println();
        Serial.println("Press some MIDI pads.");
        Serial.println();

        return;
      }

      break;
    }
  }
}


// =====================================================
// MAIN BTSTACK EVENT HANDLER
// =====================================================

void midiHciPacketHandler(
  uint8_t packetType,
  uint16_t channel,
  uint8_t* packet,
  uint16_t size
) {

  if (packetType != HCI_EVENT_PACKET) {
    return;
  }

  uint8_t event =
    hci_event_packet_get_type(packet);

  switch (event) {

    // -------------------------------------------------
    // BLE ADVERTISEMENT
    // -------------------------------------------------

    case GAP_EVENT_ADVERTISING_REPORT: {

      if (midiState != MIDI_SCANNING) {
        return;
      }

      const uint8_t* advData =
        gap_event_advertising_report_get_data(
          packet
        );

      uint8_t advLen =
        gap_event_advertising_report_get_data_length(
          packet
        );

      if (
        !advertisementHasName(
          advData,
          advLen,
          "SMC-PAD Pocket"
        )
      ) {
        return;
      }

      Serial.println();
      Serial.println("FOUND SMC-PAD POCKET");

      gap_event_advertising_report_get_address(
        packet,
        midiAddress
      );

midiAddressType =
  (bd_addr_type_t)
  gap_event_advertising_report_get_address_type(
    packet
  );

      Serial.print("Address: ");
      Serial.println(
        bd_addr_to_str(midiAddress)
      );

      gap_stop_scan();

      midiState = MIDI_CONNECTING;

      Serial.println("Connecting...");

      gap_connect(
        midiAddress,
        midiAddressType
      );

      break;
    }


    // -------------------------------------------------
    // BLE CONNECTION COMPLETE
    // -------------------------------------------------

    case HCI_EVENT_META_GAP: {

      if (
        hci_event_gap_meta_get_subevent_code(
          packet
        ) !=
        GAP_SUBEVENT_LE_CONNECTION_COMPLETE
      ) {
        return;
      }

      uint8_t status =
        gap_subevent_le_connection_complete_get_status(
          packet
        );

      if (status != ERROR_CODE_SUCCESS) {

        Serial.print(
          "BLE connect failed: 0x"
        );

        Serial.println(
          status,
          HEX
        );

        midiState = MIDI_IDLE;
        requestMidiScan();
        return;
      }

      midiConnectionHandle =
        gap_subevent_le_connection_complete_get_connection_handle(
          packet
        );

      Serial.println();
      Serial.println("SMC-PAD CONNECTED");
      Serial.print("Handle: 0x");
      Serial.println(
        midiConnectionHandle,
        HEX
      );

      memset(
        &midiService,
        0,
        sizeof(midiService)
      );

      memset(
        &midiCharacteristic,
        0,
        sizeof(midiCharacteristic)
      );

      midiState = MIDI_FINDING_SERVICE;

      Serial.println(
        "Searching BLE MIDI service..."
      );

      gatt_client_discover_primary_services_by_uuid128(
        midiGattPacketHandler,
        midiConnectionHandle,
        MIDI_SERVICE_UUID
      );

      break;
    }


    // -------------------------------------------------
    // DISCONNECT
    // -------------------------------------------------

    case HCI_EVENT_DISCONNECTION_COMPLETE: {

      hci_con_handle_t handle =
        hci_event_disconnection_complete_get_connection_handle(
          packet
        );

      if (handle != midiConnectionHandle) {
        return;
      }

      Serial.println();
      Serial.println("SMC-PAD DISCONNECTED");

      midiConnectionHandle =
        HCI_CON_HANDLE_INVALID;

      midiCharacteristic.value_handle = 0;

      midiState = MIDI_IDLE;

      requestMidiScan();

      break;
    }
  }
}


// =====================================================
// START MIDI SUPPORT
// =====================================================

void initMidiBTstack(void* context) {

  (void)context;

  Serial.println(
    "Registering BLE MIDI BTstack handler..."
  );

  hciEventCallback.callback =
    &midiHciPacketHandler;

  hci_add_event_handler(
    &hciEventCallback
  );

  startMidiScanBTstack(nullptr);
}


btstack_context_callback_registration_t initMidiCallback;

// =====================================================
// REAL GAMEPAD ROUTER
// =====================================================

// Confirmed S / Switch-mode raw map
static const uint16_t BTN_B  = 0x0001;
static const uint16_t BTN_A  = 0x0002;
static const uint16_t BTN_X  = 0x0004;
static const uint16_t BTN_Y  = 0x0008;
static const uint16_t BTN_L  = 0x0010;
static const uint16_t BTN_R  = 0x0020;
static const uint16_t BTN_ZL = 0x0040;
static const uint16_t BTN_ZR = 0x0080;
static const uint16_t BTN_L3 = 0x0100;
static const uint16_t BTN_R3 = 0x0200;

static const uint8_t GP_DPAD_UP    = 0x01;
static const uint8_t GP_DPAD_DOWN  = 0x02;
static const uint8_t GP_DPAD_RIGHT = 0x04;
static const uint8_t GP_DPAD_LEFT  = 0x08;

static const uint8_t MISC_HOME   = 0x01;
static const uint8_t MISC_MINUS  = 0x02;
static const uint8_t MISC_PLUS   = 0x04;
static const uint8_t MISC_CIRCLE = 0x08;

// Sender UART -> WLED receiver usermod.
// Change only this pin if your S3 TX wire is on another GPIO.
static const int UART_TX_PIN = 17;
static const uint32_t UART_BAUD = 115200;

static const int STICK_DEADZONE = 25;
static const uint32_t STICK_SEND_INTERVAL_MS = 40;
static const int STICK_CHANGE_THRESHOLD = 3;

uint16_t gpLastButtons = 0;
uint8_t gpLastDpad = 0;
uint8_t gpLastMisc = 0;

uint8_t lastSX = 128;
uint8_t lastC3 = 128;
uint8_t lastC1 = 128;
uint8_t lastC2 = 128;
uint32_t lastStickSendMs = 0;

int axisToDelta(int value, bool invert, int maxStep) {
  if (abs(value) <= STICK_DEADZONE) return 0;
  value = constrain(value, -512, 512);
  if (invert) value = -value;

  int magnitude = map(abs(value), STICK_DEADZONE + 1, 512, 1, maxStep);
  magnitude = constrain(magnitude, 1, maxStep);
  return value < 0 ? -magnitude : magnitude;
}

void uartCommand(const char* command) {
  Serial1.println(command);
  Serial.print("[UART] ");
  Serial.println(command);
}

void uartDelta(const char* name, int delta) {
  if (delta == 0) return;
  Serial1.print(name);
  Serial1.print(':');
  Serial1.println(delta);

  Serial.print("[UART] ");
  Serial.print(name);
  Serial.print(':');
  Serial.println(delta);
}

void sendRandomPalette() {
  char command[16];
  int palette = random(0, 72);
  snprintf(command, sizeof(command), "PAL:%d", palette);
  uartCommand(command);
}

void processGamepad() {
  if (!myController) return;
  if (!myController->isConnected()) return;
  if (!myController->hasData()) return;

  uint16_t buttons = myController->buttons();
  uint8_t dpad = myController->dpad();
  uint8_t misc = myController->miscButtons();

  uint16_t pressedButtons = buttons & ~gpLastButtons;
  uint8_t pressedDpad = dpad & ~gpLastDpad;
  uint8_t pressedMisc = misc & ~gpLastMisc;
  uint8_t releasedMisc = gpLastMisc & ~misc;

  // Discrete presets -> UART. Receiver applies the WLED transition policy.
  if (pressedButtons & BTN_A) sendPresetUart(1);
  if (pressedButtons & BTN_B) sendPresetUart(2);
  if (pressedButtons & BTN_X) sendPresetUart(3);
  if (pressedButtons & BTN_Y) sendPresetUart(4);

  if (pressedDpad & GP_DPAD_UP)    sendPresetUart(5);
  if (pressedDpad & GP_DPAD_DOWN)  sendPresetUart(6);
  if (pressedDpad & GP_DPAD_LEFT)  sendPresetUart(7);
  if (pressedDpad & GP_DPAD_RIGHT) sendPresetUart(8);

  if (pressedButtons & BTN_L) sendPresetUart(9);
  if (pressedButtons & BTN_R) sendPresetUart(10);

  // Live/special controls -> UART using the actual receiver protocol.
  if (pressedButtons & BTN_ZL) sendRandomPalette();
  if (pressedButtons & BTN_ZR) sendRandomPalette();

  // These receiver features are not implemented yet. Do not send fake commands.
  if (pressedMisc & MISC_MINUS) uartCommand("MAP:-1");
  if (pressedMisc & MISC_PLUS)  uartCommand("MAP:1");
  if (pressedMisc & MISC_HOME)  uartCommand("RND:1");

  // Momentary strobe: active only while the circle button is held.
  if (pressedMisc & MISC_CIRCLE) uartCommand("STB:1");
  if (releasedMisc & MISC_CIRCLE) uartCommand("STB:0");

  if (pressedButtons & BTN_L3) uartCommand("RSTL:1");
  if (pressedButtons & BTN_R3) uartCommand("RSTR:1");

  // Relative joystick control: center = stop changing; farther = faster change.
  uint32_t now = millis();
  if (now - lastStickSendMs >= STICK_SEND_INTERVAL_MS) {
    int sxDelta = axisToDelta(myController->axisX(), false, 6);
    int c3Delta = axisToDelta(myController->axisY(), true, 2);
    int c1Delta = axisToDelta(myController->axisRX(), false, 6);
    int c2Delta = axisToDelta(myController->axisRY(), true, 6);

    uartDelta("SXD", sxDelta);
    uartDelta("LYD", c3Delta);
    uartDelta("C1D", c1Delta);
    uartDelta("C2D", c2Delta);
    lastStickSendMs = now;
  }

  gpLastButtons = buttons;
  gpLastDpad = dpad;
  gpLastMisc = misc;
}


// =====================================================
// UART PRESET TRANSPORT
// Both gamepad and BLE MIDI use the same hardwired link.
// =====================================================

void sendPresetUart(uint8_t preset) {
  if (preset < 1 || preset > 250) return;
  Serial1.print("P:");
  Serial1.println(preset);
  Serial.print("[PRESET UART] ");
  Serial.println(preset);
}


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("====================================");
  Serial.println("PERVUS PRODUCTION UART v6 - GAMEPAD + BLE MIDI");
  Serial.println("====================================");

  // ---------------------------------------------
  // Bluepad32
  // ---------------------------------------------

  BP32.setup(
    &onConnectedController,
    &onDisconnectedController
  );

  BP32.enableVirtualDevice(false);

  Serial.println("Bluepad32 initialized.");


  // ---------------------------------------------
  // Schedule BLE MIDI initialization
  // on the BTstack thread.
  // ---------------------------------------------

  initMidiCallback.callback =
    &initMidiBTstack;

  initMidiCallback.context =
    nullptr;

  btstack_run_loop_execute_on_main_thread(
    &initMidiCallback
  );

  Serial.println(
    "BLE MIDI initialization requested."
  );

  // UART sender. RX is unused; TX goes to the WLED receiver GPIO18.
  Serial1.begin(UART_BAUD, SERIAL_8N1, -1, UART_TX_PIN);
  Serial.print("UART TX pin: GPIO");
  Serial.println(UART_TX_PIN);
  Serial.println("UART baud: 115200");
}


// =====================================================
// LOOP
// =====================================================

void loop() {
  BP32.update();
  processGamepad();
  delay(5);
}