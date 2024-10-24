#define OLED_DRIVER_TYPE 2 // SSD1306
#define OLED_DISPLAY_SIZE 128, 32
#define OLED_ADDRESS 0x3C
#include <zmk/keys.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_event.h>
#include <zmk/display.h>

static void display_keycode_on_oled(uint8_t keycode) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Key: %s", keycode_to_string(keycode));

    // OLED 화면을 업데이트할 때 두 번째 줄에만 텍스트 표시
    zmk_oled_set_cursor(0, 1); // (x, y) 위치 설정 (0, 1은 두 번째 줄)
    zmk_oled_print(buf); // 두 번째 줄에 텍스트 표시
}

static void keycode_event_handler(struct zmk_event_header *hdr) {
    struct zmk_keycode_event *event = cast_zmk_keycode_event(hdr);
    display_keycode_on_oled(event->keycode);
}

// 이벤트 리스너 등록
ZMK_LISTENER(keycode_event_handler, keycode_event);
