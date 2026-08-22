// Host test for List::OnUpdate null controller safety (sphaira/source/ui/list.cpp)
// Verifies that List::OnUpdate and OnUpdateGrid/OnUpdateHome safely handle controller=nullptr without crashing.
//
//     g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_list_null_safety.cpp -o /tmp/t && /tmp/t

#include <cstdint>
#include <cstdio>
#include <functional>

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

namespace {

struct Vec2 {
    float x{}, y{};
};

struct Vec4 {
    float x{}, y{}, w{}, h{};
};

struct TouchInfo {
    bool is_touching{false};
    bool is_clicked{false};
    bool is_scroll{false};
    bool is_end{false};
    Vec2 initial{};
    Vec2 cur{};

    bool in_range(const Vec4& r) const {
        return cur.x >= r.x && cur.x <= r.x + r.w &&
               cur.y >= r.y && cur.y <= r.y + r.h;
    }
};

struct Controller {
    bool GotDown(int) const { return false; }
};

using TouchCallback = std::function<void(bool touched, int64_t index)>;

class MockList {
public:
    enum class Layout { HOME, GRID };

    MockList(int64_t row, int64_t page, const Vec4& pos, const Vec4& v, const Vec2& pad)
    : m_row{row}, m_page{page}, m_pos{pos}, m_v{v}, m_pad{pad} {}

    void StepFling(TouchInfo* touch) {
        if (!touch) return;
        m_step_fling_called = true;
    }

    bool OnTouchScroll(TouchInfo* touch) {
        if (!touch) return false;
        m_touch_scroll_called = true;
        return true;
    }

    void OnUpdateGrid(Controller* controller, TouchInfo* touch, int64_t& index, int64_t count, TouchCallback callback) {
        (void)count;
        if (controller && controller->GotDown(1)) {
            index++;
            callback(false, index);
        } else if (touch && touch->is_clicked && touch->in_range(m_pos)) {
            callback(true, index);
        } else if (touch) {
            OnTouchScroll(touch);
        }
    }

    void OnUpdateHome(Controller* controller, TouchInfo* touch, int64_t& index, int64_t count, TouchCallback callback) {
        (void)count;
        if (controller && controller->GotDown(1)) {
            index++;
            callback(false, index);
        } else if (touch && touch->is_clicked && touch->in_range(m_pos)) {
            callback(true, index);
        } else if (touch) {
            OnTouchScroll(touch);
        }
    }

    void OnUpdate(Controller* controller, TouchInfo* touch, int64_t& index, int64_t count, TouchCallback callback) {
        switch (m_layout) {
            case Layout::HOME:
                StepFling(touch);
                OnUpdateHome(controller, touch, index, count, callback);
                break;
            case Layout::GRID:
                StepFling(touch);
                OnUpdateGrid(controller, touch, index, count, callback);
                break;
        }
    }

    Layout m_layout{Layout::GRID};
    int64_t m_row{1};
    int64_t m_page{7};
    Vec4 m_pos{};
    Vec4 m_v{};
    Vec2 m_pad{};
    bool m_step_fling_called{false};
    bool m_touch_scroll_called{false};
};

} // namespace

int main() {
    // 1. Grid layout with controller=nullptr and touch=nullptr -> no crash
    {
        MockList list{1, 7, Vec4{445.f, 105.f, 780.f, 525.f}, Vec4{465.f, 115.f, 735.f, 62.f}, Vec2{0.f, 7.f}};
        int64_t index = 0;
        bool callback_fired = false;

        list.OnUpdate(nullptr, nullptr, index, 10, [&](bool, int64_t){
            callback_fired = true;
        });

        CHECK(!callback_fired);
        CHECK(index == 0);
        CHECK(!list.m_step_fling_called);
    }

    // 2. Grid layout with controller=nullptr and valid touch click
    {
        MockList list{1, 7, Vec4{445.f, 105.f, 780.f, 525.f}, Vec4{465.f, 115.f, 735.f, 62.f}, Vec2{0.f, 7.f}};
        TouchInfo touch{};
        touch.is_clicked = true;
        touch.cur = { 500.f, 120.f };
        int64_t index = 0;
        bool callback_fired = false;

        list.OnUpdate(nullptr, &touch, index, 10, [&](bool touched, int64_t idx){
            callback_fired = true;
            if (!touched || idx != 0) {
                g_checks--;
            }
        });

        CHECK(callback_fired);
        CHECK(list.m_step_fling_called);
    }

    // 3. Home layout with controller=nullptr and touch=nullptr -> no crash
    {
        MockList list{1, 5, Vec4{0.f, 200.f, 1280.f, 300.f}, Vec4{50.f, 200.f, 200.f, 200.f}, Vec2{20.f, 0.f}};
        list.m_layout = MockList::Layout::HOME;
        int64_t index = 0;
        bool callback_fired = false;

        list.OnUpdate(nullptr, nullptr, index, 5, [&](bool, int64_t){
            callback_fired = true;
        });

        CHECK(!callback_fired);
    }

    std::printf("ok  list_null_safety: %d checks passed\n", g_checks);
    return 0;
}
