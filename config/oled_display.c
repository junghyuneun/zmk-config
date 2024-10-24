#include <zmk/keys.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_event.h>
#include <zmk/display.h>

static void display_keycode_on_oled(uint8_t keycode) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Key: %s", keycode_to_string(keycode));

    // OLED 초기화 및 두 번째 줄에 텍스트 표시
    zmk_oled_clear(); // OLED 초기화
    zmk_oled_set_cursor(0, 1); // 두 번째 줄로 커서 이동
    zmk_oled_print(buf); // 텍스트 출력
}

static void keycode_event_handler(struct zmk_event_header *hdr) {
    struct zmk_keycode_event *event = cast_zmk_keycode_event(hdr);
    display_keycode_on_oled(event->keycode);
}

// 이벤트 리스너 등록
ZMK_LISTENER(keycode_event_handler, keycode_event);
