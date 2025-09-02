/*********************************************************************
 Adafruit invests time and resources providing this open source code,
 please support Adafruit and open-source hardware by purchasing
 products from Adafruit!

 MIT license, check LICENSE for more information
 Copyright (c) 2019 Ha Thach for Adafruit Industries
 All text above, and the splash screen below must be included in
 any redistribution
*********************************************************************/


/* This example demonstrates use of both device and host, where
 * - Device run on native usb controller (roothub port0)
 * - Host depending on MCUs run on either:
 *   - rp2040: bit-banging 2 GPIOs with the help of Pico-PIO-USB library (roothub port1)
 *   - samd21/51, nrf52840, esp32: using MAX3421e controller (host shield)
 *
 * Requirements:
 * - For rp2040:
 *   - [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB) library
 *   - 2 consecutive GPIOs: D+ is defined by PIN_USB_HOST_DP, D- = D+ +1
 *   - Provide VBus (5v) and GND for peripheral
 *   - CPU Speed must be either 120 or 240 Mhz. Selected via "Menu -> CPU Speed"
 * - For samd21/51, nrf52840, esp32:
 *   - Additional MAX2341e USB Host shield or featherwing is required
 *   - SPI instance, CS pin, INT pin are correctly configured in usbh_helper.h
 */

#include "tusb.h"
// USBHost is defined in usbh_helper.h
#include "usbh_helper.h"
#include "Adafruit_TinyUSB.h"

// PCへ通知するreport_id
#define RID_KEYBOARD 1
#define RID_MOUSE    2

// HID report descriptor using TinyUSB's template
// Single Report (no ID) descriptor
uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_KEYBOARD( HID_REPORT_ID(RID_KEYBOARD) ),
  TUD_HID_REPORT_DESC_MOUSE( HID_REPORT_ID(RID_MOUSE) )
};

// USB HID object
Adafruit_USBD_HID usb_hid;

// スクロールモード
bool scroll_mode = false;
typedef struct {
  int8_t x;
  int8_t y;
} point_t;
point_t scroll_mode_point = {0, 0};

// 直前のボタンの状態
uint8_t prev_buttons = 0;

// ボタンの割当て
const uint8_t BUTTON_LEFT    = 0b00000001;
const uint8_t BUTTON_RIGHT   = 0b00000010;
const uint8_t BUTTON_MIDDLE  = 0b00000100;
const uint8_t BUTTON_BACK    = 0b00001000;
const uint8_t BUTTON_FORWARD = 0b00010000;
const uint8_t BUTTON_FN1     = 0b00100000;
const uint8_t BUTTON_FN2     = 0b01000000;
const uint8_t BUTTON_FN3     = 0b10000000;

#if defined(CFG_TUH_MAX3421) && CFG_TUH_MAX3421
//--------------------------------------------------------------------+
// Using Host shield MAX3421E controller
//--------------------------------------------------------------------+
void setup() {
  Serial.begin(115200);

  // init host stack on controller (rhport) 1
  USBHost.begin(1);

//  while ( !Serial ) delay(10);   // wait for native usb
  Serial.println("TinyUSB Dual: HID Device Report Example");
}

void loop() {
  USBHost.task();
  Serial.flush();
}

#elif defined(ARDUINO_ARCH_RP2040)
//--------------------------------------------------------------------+
// For RP2040 use both core0 for device stack, core1 for host stack
//--------------------------------------------------------------------+

//------------- Core0 -------------//
void setup() {
  Serial.begin(115200);

  // Manual begin() is required on core without built-in support e.g. mbed rp2040
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }

  // Set up HID
  usb_hid.setBootProtocol(HID_ITF_PROTOCOL_MOUSE);
  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.setStringDescriptor("TinyUSB Mouse");
  usb_hid.begin();

  // If already enumerated, additional class driverr begin() e.g msc, hid, midi won't take effect until re-enumeration
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }

  while ( !Serial ) delay(10);   // wait for native usb
  Serial.println("TinyUSB Dual: HID Device Report Example");
}

void loop() {
  Serial.flush();
}

//------------- Core1 -------------//
void setup1() {
  // configure pio-usb: defined in usbh_helper.h
  rp2040_configure_pio_usb();

  // run host stack on controller (rhport) 1
  // Note: For rp2040 pico-pio-usb, calling USBHost.begin() on core1 will have most of the
  // host bit-banging processing works done in core1 to free up core0 for other works
  USBHost.begin(1);
}

void loop1() {
  USBHost.task();
}

#endif

extern "C" {

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use.
// tuh_hid_parse_report_descriptor() can be used to parse common/simple enough
// descriptor. Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE,
// it will be skipped therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
  (void) desc_report;
  (void) desc_len;
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  Serial.printf("HID device address = %d, instance = %d is mounted\r\n", dev_addr, instance);
  Serial.printf("VID = %04x, PID = %04x\r\n", vid, pid);

  // https://eleccelerator.com/usbdescreqparser/
  Serial.printf("HID desc report : ");
  for (uint16_t i = 0; i < desc_len; i++) {
    Serial.printf("%02X ", desc_report[i]);
  }
  Serial.println();

  int8_t MAX_REPORT = 8;
  tuh_hid_report_info_t report_info[MAX_REPORT] = {0};
  uint8_t report_count = tuh_hid_parse_report_descriptor(report_info, MAX_REPORT, desc_report, desc_len);
  Serial.printf("HID desc report count : %d\r\n", report_count);
  for (int8_t i = 0; i < report_count; i++) {
    Serial.printf("  report id %d : usage_page = %02, usage = %02xx\r\n", report_info[i].report_id, report_info[i].usage_page, report_info[i].usage);
  }

  if (!tuh_hid_receive_report(dev_addr, instance)) {
    Serial.printf("Error: cannot request to receive report\r\n");
  }
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  Serial.printf("HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
  Serial.printf("HID report : ");
  for (uint16_t i = 0; i < len; i++) {
    Serial.printf("0x%02X ", report[i]);
  }
  Serial.println();

  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
  if (itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
    // TODO: チルト(pan)の値が正しく取得できない
    process_hid( (hid_mouse_report_t const*) report );
  }

  // continue to request to receive report
  if (!tuh_hid_receive_report(dev_addr, instance)) {
    Serial.printf("Error: cannot request to receive report\r\n");
  }
}

void process_hid(hid_mouse_report_t const * report) {
  #ifdef TINYUSB_NEED_POLLING_TASK
  // Manual call tud_task since it isn't called by Core's background
  TinyUSBDevice.task();
  #endif

  // not enumerated()/mounted() yet: nothing to do
  if (!TinyUSBDevice.mounted()) {
    return;
  }

  // Remote wakeup
  if (TinyUSBDevice.suspended()) {
    // Wake up host if we are in suspend mode
    // and REMOTE_WAKEUP feature is enabled by host
    TinyUSBDevice.remoteWakeup();
  }

  if (usb_hid.ready()) {
    // Fn1/2はCtrl+PageUp/Down
    uint8_t keycode[6] = {0};
    uint8_t crnt_fn1 = BUTTON_FN1 & report->buttons;
    uint8_t prev_fn1 = BUTTON_FN1 & prev_buttons;
    uint8_t crnt_fn2 = BUTTON_FN2 & report->buttons;
    uint8_t prev_fn2 = BUTTON_FN2 & prev_buttons;
    keycode[0] = (uint8_t)HID_KEY_CONTROL_RIGHT;
    if (prev_fn1 < crnt_fn1) keycode[1] = (uint8_t)HID_KEY_PAGE_UP;
    if (prev_fn2 < crnt_fn2) keycode[1] = (uint8_t)HID_KEY_PAGE_DOWN;
    if (keycode[1]) {
      usb_hid.keyboardReport(RID_KEYBOARD, 0, keycode);
      delay(50);
      usb_hid.keyboardRelease(RID_KEYBOARD);
    }
    // Fn3がクリックされたら、スクロールモードをトグル
    uint8_t crnt_fn3 = BUTTON_FN3 & report->buttons;
    uint8_t prev_fn3 = BUTTON_FN3 & prev_buttons;
    if (prev_fn3 < crnt_fn3) {
      scroll_mode = !scroll_mode;
      Serial.printf("scroll mode: %s\r\n", scroll_mode ? "on" : "off");
      if (!scroll_mode) scroll_mode_point = {0, 0};
    }
    // スクロールモードの場合
    if (scroll_mode) {
      // マウス移動をスクロールとする
      point_t adjusted = adjust_scroll(report->x, -1 * report->y);
      if (adjusted.x || adjusted.y) {
        usb_hid.mouseReport(RID_MOUSE, report->buttons, 0, 0, report->wheel ? report->wheel : adjusted.y, report->pan ? report->pan : adjusted.x);
      }
    } else {
      // マウス移動はそのままPCへ送る
      usb_hid.mouseReport(RID_MOUSE, report->buttons, report->x, report->y, report->wheel, report->pan);
    }
  }
  // 今回のボタンの状態を保存
  prev_buttons = report->buttons;
}

// マウスの移動量をそのままスクロールの値として利用すると
// 感度が良すぎるため、一定量動かしたらスクロールを発生させる
point_t adjust_scroll(int8_t x, int8_t y) {
  int8_t span = 10;
  x += scroll_mode_point.x;
  y += scroll_mode_point.y;
  point_t adjusted = {(int8_t)(x / span), (int8_t)(y / span)};
  scroll_mode_point = {(int8_t)(x % span), (int8_t)(y % span)};
  return adjusted;
}

} // extern C