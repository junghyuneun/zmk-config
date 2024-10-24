#include <zmk/commands.h>
#include <zmk/event.h>
#include <zmk/display.h>

const char *username = "LSJ"; // 사용자명

void display_input_on_second_line(const char *input) {
    // 입력값을 OLED에 출력
    zmk_display_set_text(ZMK_DISPLAY_OLED_RIGHT, input);
}

void display_username() {
    zmk_display_set_text(ZMK_DISPLAY_OLED_RIGHT, username);
}

// 입력을 처리하는 함수
static void on_key_event(struct zmk_event *event) {
    if (event->type == ZMK_EVENT_KEY_PRESS) {
        const char *key_label = event->key.label; // 키 라벨 가져오기
        display_input_on_second_line(key_label); // OLED에 입력값 표시
        display_username(); // 사용자명 표시
    }
}

// 이벤트 리스너 등록
ZMK_LISTEN(on_key_event);

void display_input(const char *input) {
    char display_text[32]; // 적절한 크기로 배열 선언
    snprintf(display_text, sizeof(display_text), "%s: %s", username, input);
    zmk_display_set_text(ZMK_DISPLAY_OLED_RIGHT, display_text);
}
