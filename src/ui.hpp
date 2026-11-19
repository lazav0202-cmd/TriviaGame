// ui.hpp - интерфейс: шрифт, текст, анимированные кнопки (4 дизайна), ползунок,
// поле ввода, фон, частицы и функции плавности (easing) для анимаций.

#pragma once

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// Виртуальное разрешение игры. Всё рисуется в "холст" 1280x720, а потом он
// масштабируется под реальный экран (см. main.cpp), поэтому координаты ниже
// одинаковы и в окне, и на полном экране.
constexpr int SCREEN_WIDTH = 1280;
constexpr int SCREEN_HEIGHT = 720;

constexpr Color MUTED_COLOR = {170, 175, 200, 255};  // второстепенный текст
constexpr Color GOOD_COLOR = {120, 230, 160, 255};   // "хорошо"
constexpr Color BAD_COLOR = {255, 130, 140, 255};    // "плохо"

// ---------------------------------------------------------------------------
// Математика анимаций
// ---------------------------------------------------------------------------

// Ограничивает число отрезком [0, 1].
inline float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// Линейная интерполяция: t=0 даёт a, t=1 даёт b.
inline float LerpF(float a, float b, float t) { return a + (b - a) * t; }

// Функции плавности: превращают равномерное время t (0..1) в "живое" движение.
inline float EaseOutCubic(float t) {  // быстро стартует, мягко тормозит
    const float u = 1.0f - Clamp01(t);
    return 1.0f - u * u * u;
}
inline float EaseInCubic(float t) {  // медленно разгоняется
    t = Clamp01(t);
    return t * t * t;
}
inline float EaseOutBack(float t) {  // чуть "перелетает" цель и возвращается
    t = Clamp01(t) - 1.0f;
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * t * t * t + c1 * t * t;
}

// Время кадра, но не больше 0.05 с (чтобы анимации не "прыгали" после подвисаний).
inline float UiDelta() { return std::min(GetFrameTime(), 0.05f); }

// ---------------------------------------------------------------------------
// Шрифт и текст
// ---------------------------------------------------------------------------

// Стандартный шрифт raylib не знает русских букв, поэтому берём системный
// TTF-шрифт и просим загрузить латиницу, кириллицу и несколько знаков.
inline Font LoadGameFont(int size) {
    std::vector<int> codepoints;
    for (int c = 32; c <= 126; ++c) codepoints.push_back(c);         // латиница, цифры
    for (int c = 0x0400; c <= 0x04FF; ++c) codepoints.push_back(c);  // кириллица
    for (int c : {0x00AB, 0x00BB, 0x00B2, 0x00B7, 0x2013, 0x2014, 0x2026}) {  // « » ² · – — …
        codepoints.push_back(c);
    }

    // Ищем шрифт: сначала рядом с программой, потом в системных папках.
    const std::vector<std::string> paths = {
        "font.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
    };

    for (const std::string& path : paths) {
        if (FileExists(path.c_str())) {
            Font font = LoadFontEx(path.c_str(), size, codepoints.data(),
                                   static_cast<int>(codepoints.size()));
            if (font.texture.id != 0) {
                SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);  // гладкие буквы
                return font;
            }
        }
    }

    TraceLog(LOG_WARNING, "Cyrillic font not found. Put a .ttf file named font.ttf next to the game.");
    return GetFontDefault();
}

// Случайное дробное число от lo до hi.
inline float RandomFloat(float lo, float hi) {
    return lo + (hi - lo) * static_cast<float>(GetRandomValue(0, 10000)) / 10000.0f;
}

// Текст, выровненный по центру относительно координаты centerX.
inline void DrawTextCenteredX(const Font& font, const std::string& text, float centerX, float y,
                              float size, Color color) {
    const Vector2 measured = MeasureTextEx(font, text.c_str(), size, 1.0f);
    DrawTextEx(font, text.c_str(), Vector2{centerX - measured.x / 2.0f, y}, size, 1.0f, color);
}

// Разбивает текст на строки по словам так, чтобы каждая строка влезала в maxWidth.
inline std::vector<std::string> WrapLines(const Font& font, const std::string& text, float size,
                                          float maxWidth) {
    std::vector<std::string> lines;
    std::istringstream words(text);  // читает текст по словам
    std::string word;
    std::string line;
    while (words >> word) {
        const std::string candidate = line.empty() ? word : line + " " + word;
        // Если строка с новым словом уже не помещается - сохраняем накопленное.
        if (!line.empty() && MeasureTextEx(font, candidate.c_str(), size, 1.0f).x > maxWidth) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// Как WrapLines, но уменьшает размер шрифта (size меняется!), пока текст не поместится
// в maxLines строк. Так длинные тексты не вылезают за рамки.
inline std::vector<std::string> FitLines(const Font& font, const std::string& text, float& size,
                                         float maxWidth, std::size_t maxLines, float minSize) {
    std::vector<std::string> lines = WrapLines(font, text, size, maxWidth);
    while (size > minSize) {
        float widest = 0.0f;
        for (const std::string& l : lines) {
            widest = std::max(widest, MeasureTextEx(font, l.c_str(), size, 1.0f).x);
        }
        if (lines.size() <= maxLines && widest <= maxWidth) break;
        size *= 0.92f;
        lines = WrapLines(font, text, size, maxWidth);
    }
    return lines;
}

// Абзац с переносом по словам. maxLines > 0 включает автоуменьшение шрифта.
// Возвращает высоту, которую занял текст.
inline float DrawParagraph(const Font& font, const std::string& text, float x, float y,
                           float maxWidth, float size, Color color, bool centered = false,
                           std::size_t maxLines = 0) {
    const std::vector<std::string> lines = (maxLines > 0)
                                               ? FitLines(font, text, size, maxWidth, maxLines, 16.0f)
                                               : WrapLines(font, text, size, maxWidth);
    const float lineHeight = size * 1.3f;
    float cursorY = y;
    for (const std::string& line : lines) {
        float lineX = x;
        if (centered) {
            lineX = x + (maxWidth - MeasureTextEx(font, line.c_str(), size, 1.0f).x) / 2.0f;
        }
        DrawTextEx(font, line.c_str(), Vector2{lineX, cursorY}, size, 1.0f, color);
        cursorY += lineHeight;
    }
    return cursorY - y;
}

// ---------------------------------------------------------------------------
// Фон, панели, индикатор загрузки, поле ввода
// ---------------------------------------------------------------------------

// Тёмный градиент и медленно плавающие полупрозрачные круги.
inline void DrawBackground(float time) {
    DrawRectangleGradientV(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Color{34, 38, 72, 255},
                           Color{12, 14, 30, 255});
    for (int i = 0; i < 14; ++i) {
        const float fi = static_cast<float>(i);
        const float x = SCREEN_WIDTH * (0.5f + 0.47f * std::sin(time * 0.15f + fi * 1.7f));
        const float y = SCREEN_HEIGHT * (0.5f + 0.47f * std::cos(time * 0.12f + fi * 2.3f));
        const float radius = 30.0f + 14.0f * static_cast<float>(i % 4);
        DrawCircleV(Vector2{x, y}, radius, Fade(WHITE, 0.03f));
    }
}

// Тёмная полупрозрачная подложка под текст.
inline void DrawPanel(Rectangle rect, float alpha = 1.0f) {
    DrawRectangleRounded(rect, 0.06f, 12, Fade(BLACK, 0.28f * alpha));
}

// Вращающийся индикатор "идёт загрузка".
inline void DrawSpinner(Vector2 center, float radius, float time) {
    const int dots = 10;
    const float head = std::fmod(time * 1.6f, 1.0f) * static_cast<float>(dots);
    for (int i = 0; i < dots; ++i) {
        const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(dots) - 1.5707963f;
        const float behind = std::fmod(head - static_cast<float>(i) + static_cast<float>(dots),
                                       static_cast<float>(dots)) / static_cast<float>(dots);
        const float strength = 1.0f - behind;  // у "головы" точки ярче и крупнее
        const Vector2 pos{center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
        DrawCircleV(pos, 4.0f + 6.0f * strength, Fade(GOLD, 0.15f + 0.85f * strength));
    }
}

// Поле ввода текста с мигающим курсором (сам ввод обрабатывается в main.cpp).
inline void DrawTextField(const Font& font, Rectangle rect, const std::string& text,
                          const std::string& placeholder, float time) {
    DrawRectangleRounded(rect, 0.2f, 10, Color{16, 18, 40, 255});
    DrawRectangleLinesEx(rect, 2.0f, GOLD);
    const float size = 28.0f;
    const float textY = rect.y + (rect.height - size) / 2.0f;
    if (text.empty()) {
        DrawTextEx(font, placeholder.c_str(), Vector2{rect.x + 18.0f, textY}, size, 1.0f,
                   Fade(MUTED_COLOR, 0.6f));
        if (std::fmod(time, 1.0f) < 0.5f) {
            DrawRectangleRec(Rectangle{rect.x + 16.0f, textY, 2.0f, size}, WHITE);
        }
    } else {
        DrawTextEx(font, text.c_str(), Vector2{rect.x + 18.0f, textY}, size, 1.0f, WHITE);
        if (std::fmod(time, 1.0f) < 0.5f) {
            const float width = MeasureTextEx(font, text.c_str(), size, 1.0f).x;
            DrawRectangleRec(Rectangle{rect.x + 20.0f + width, textY, 2.0f, size}, WHITE);
        }
    }
}

// ---------------------------------------------------------------------------
// Кнопки
// ---------------------------------------------------------------------------

// Дизайны кнопок. Номера совпадают с каталогом магазина (save.hpp).
enum ButtonStyle { STYLE_CLASSIC = 0, STYLE_ROUNDED = 1, STYLE_NEON = 2, STYLE_RETRO = 3 };

// Состояние кнопки определяет её цвет.
enum class ButtonState {
    Normal,   // обычная (подсвечивается при наведении)
    Correct,  // зелёная: правильный ответ или выбранный пункт
    Wrong,    // красная: неправильный ответ
    Dim       // приглушённая: недоступна или неактивна
};

// Необязательные настройки кнопки. Пример: UiButton(font, style, rect, "Текст", {.fontSize = 22});
struct ButtonOpts {
    ButtonState state = ButtonState::Normal;
    bool centered = true;      // текст по центру или слева
    float fontSize = 28.0f;
    bool clickable = true;     // false - кнопка только нарисована, клики игнорируются
    int id = -1;               // -1: номер кнопки берётся из её позиции
    float alpha = 1.0f;        // общая прозрачность (для плавного исчезновения)
    bool entrance = true;      // играть ли анимацию появления
    float offsetX = 0.0f;      // дополнительный сдвиг (тряска, "вылет" вопроса)
    float scale = 1.0f;        // дополнительный масштаб (пульсация)
};

// У каждой кнопки есть "память" - текущая стадия анимаций. Хранится по номеру кнопки.
struct ButtonAnim {
    float hover = 0.0f;    // 0..1: насколько кнопка "наведена" (плавно)
    float press = 0.0f;    // 0..1: насколько кнопка "нажата" (плавно)
    float appear = 0.0f;   // 0..1: стадия появления
    float delay = 0.0f;    // задержка появления в секундах (кнопки выезжают по очереди)
    float pulse = -1.0f;   // 0..1: расходящееся кольцо после клика, -1 = нет
    bool wasHovered = false;
};

// inline-переменные: одна на всю программу, их можно держать прямо в .hpp.
inline std::unordered_map<int, ButtonAnim> g_buttonAnims;
inline bool g_uiHoverStarted = false;  // курсор только что навёлся на кнопку (для звука)
inline bool g_uiAnyHover = false;      // курсор над какой-то кнопкой (для курсора-руки)
inline int g_sliderDrag = -1;          // какой ползунок сейчас тянут

// Сбрасывает память кнопок - вызываем при смене экрана, чтобы кнопки выезжали заново.
inline void UiResetAnimations() { g_buttonAnims.clear(); }

// Вызывать в начале каждого кадра.
inline void UiBeginFrame() { g_uiAnyHover = false; }

// Возвращает true один раз после наведения на кнопку.
inline bool UiConsumeHoverStarted() {
    const bool value = g_uiHoverStarted;
    g_uiHoverStarted = false;
    return value;
}

// Подбирает цвет кнопки по дизайну и состоянию.
inline Color PickButtonColor(bool neon, ButtonState state, bool lit) {
    if (neon) {
        switch (state) {
            case ButtonState::Correct: return Color{0, 255, 130, 255};
            case ButtonState::Wrong:   return Color{255, 70, 100, 255};
            case ButtonState::Dim:     return Color{90, 95, 130, 255};
            default:                   return lit ? Color{140, 235, 255, 255} : Color{0, 180, 255, 255};
        }
    }
    switch (state) {
        case ButtonState::Correct: return GREEN;
        case ButtonState::Wrong:   return RED;
        case ButtonState::Dim:     return Color{190, 192, 205, 255};
        default:                   return lit ? SKYBLUE : LIGHTGRAY;
    }
}

// Кнопка "в один вызов": рисует себя, проигрывает анимации (появление, наведение,
// нажатие, кольцо после клика) и возвращает true в тот кадр, когда по ней кликнули.
inline bool UiButton(const Font& font, int style, Rectangle rect, const std::string& label,
                     const ButtonOpts& o = ButtonOpts{}) {
    const float dt = UiDelta();

    // --- Находим (или создаём) "память" этой кнопки ---
    const int key = (o.id >= 0) ? o.id
                                : 100000 + static_cast<int>(rect.x) * 7919 + static_cast<int>(rect.y);
    auto found = g_buttonAnims.try_emplace(key);
    ButtonAnim& a = found.first->second;
    if (found.second) {  // кнопка создана только что
        a.appear = o.entrance ? 0.0f : 1.0f;
        // Чем ниже кнопка на экране, тем позже она появляется - получается "каскад".
        a.delay = o.entrance ? (rect.y / static_cast<float>(SCREEN_HEIGHT)) * 0.30f : 0.0f;
    }
    if (a.delay > 0.0f) {
        a.delay -= dt;
    } else {
        a.appear = std::min(1.0f, a.appear + dt * 4.0f);
    }

    const float intro = EaseOutBack(a.appear);
    const float alpha = o.alpha * Clamp01(a.appear * 2.0f);

    // --- Мышь: наведение, нажатие, клик ---
    const bool active = o.clickable && a.appear >= 0.6f && o.alpha >= 0.6f;
    const Rectangle hitRect{rect.x + o.offsetX, rect.y, rect.width, rect.height};
    const bool hovered = active && CheckCollisionPointRec(GetMousePosition(), hitRect);
    const bool held = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const bool clicked = hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    if (hovered) {
        g_uiAnyHover = true;
        if (!a.wasHovered) g_uiHoverStarted = true;
    }
    a.wasHovered = hovered;

    // --- Плавное приближение значений к цели (экспоненциальное сглаживание) ---
    a.hover += ((hovered ? 1.0f : 0.0f) - a.hover) * std::min(1.0f, dt * 16.0f);
    a.press += ((held ? 1.0f : 0.0f) - a.press) * std::min(1.0f, dt * 30.0f);
    if (clicked) a.pulse = 0.0f;
    if (a.pulse >= 0.0f) {
        a.pulse += dt * 2.2f;
        if (a.pulse > 1.0f) a.pulse = -1.0f;
    }

    // --- Геометрия: масштаб (появление, наведение, нажатие) и лёгкий подъём ---
    const float scale = (0.85f + 0.15f * intro) * (1.0f + 0.045f * a.hover - 0.06f * a.press) * o.scale;
    const float centerX = rect.x + rect.width / 2.0f + o.offsetX;
    const float centerY = rect.y + rect.height / 2.0f - 3.0f * a.hover + 2.0f * a.press;
    const Rectangle body{centerX - rect.width * scale / 2.0f, centerY - rect.height * scale / 2.0f,
                         rect.width * scale, rect.height * scale};

    const bool neon = (style == STYLE_NEON);
    const bool lit = a.hover > 0.5f && o.state == ButtonState::Normal;
    const Color color = PickButtonColor(neon, o.state, lit);
    const float wash = 0.12f * a.hover * alpha;  // светлая "вуаль" при наведении

    switch (style) {
        case STYLE_ROUNDED: {
            // Тень, тёмная рамка и закруглённая заливка.
            DrawRectangleRounded(Rectangle{body.x, body.y + 4.0f + 4.0f * a.hover, body.width, body.height},
                                 0.35f, 12, Fade(BLACK, 0.35f * alpha));
            DrawRectangleRounded(body, 0.35f, 12, Fade(DARKGRAY, alpha));
            const Rectangle inner{body.x + 3.0f, body.y + 3.0f, body.width - 6.0f, body.height - 6.0f};
            DrawRectangleRounded(inner, 0.35f, 12, Fade(color, alpha));
            DrawRectangleRounded(inner, 0.35f, 12, Fade(WHITE, wash));
            break;
        }
        case STYLE_NEON: {
            // Тёмная заливка, свечение из нескольких рамок и яркий контур.
            DrawRectangleRec(body, Fade(Color{16, 18, 40, 255}, alpha));
            for (int i = 1; i <= 4; ++i) {
                const float grow = static_cast<float>(i) * 2.0f;
                const Rectangle glow{body.x - grow, body.y - grow, body.width + grow * 2.0f,
                                     body.height + grow * 2.0f};
                DrawRectangleLinesEx(glow, 2.0f,
                                     Fade(color, (0.04f + 0.05f * a.hover) * static_cast<float>(5 - i) * alpha));
            }
            DrawRectangleLinesEx(body, 3.0f, Fade(color, alpha));
            DrawRectangleRec(body, Fade(color, 0.10f * a.hover * alpha));
            break;
        }
        case STYLE_RETRO: {
            // Пиксельная кнопка: жёсткая тень, светлые и тёмные грани.
            DrawRectangleRec(Rectangle{body.x + 4.0f, body.y + 4.0f, body.width, body.height},
                             Fade(BLACK, alpha));
            DrawRectangleRec(body, Fade(color, alpha));
            DrawRectangleRec(Rectangle{body.x, body.y, body.width, 4.0f}, Fade(WHITE, 0.6f * alpha));
            DrawRectangleRec(Rectangle{body.x, body.y, 4.0f, body.height}, Fade(WHITE, 0.6f * alpha));
            DrawRectangleRec(Rectangle{body.x, body.y + body.height - 4.0f, body.width, 4.0f},
                             Fade(BLACK, 0.3f * alpha));
            DrawRectangleRec(Rectangle{body.x + body.width - 4.0f, body.y, 4.0f, body.height},
                             Fade(BLACK, 0.3f * alpha));
            DrawRectangleLinesEx(body, 2.0f, Fade(BLACK, alpha));
            DrawRectangleRec(body, Fade(WHITE, wash));
            break;
        }
        default: {  // STYLE_CLASSIC
            DrawRectangleRec(Rectangle{body.x, body.y + 4.0f + 3.0f * a.hover, body.width, body.height},
                             Fade(BLACK, 0.28f * alpha));
            DrawRectangleRec(body, Fade(color, alpha));
            DrawRectangleRec(body, Fade(WHITE, wash));
            DrawRectangleLinesEx(body, 2.0f, Fade(DARKGRAY, alpha));
            break;
        }
    }

    // --- Кольцо после клика: расширяется и тает ---
    if (a.pulse >= 0.0f) {
        const float grow = 6.0f + 44.0f * EaseOutCubic(a.pulse);
        const Rectangle ring{body.x - grow, body.y - grow, body.width + grow * 2.0f,
                             body.height + grow * 2.0f};
        DrawRectangleLinesEx(ring, 3.0f, Fade(neon ? color : WHITE, 0.6f * (1.0f - a.pulse) * alpha));
    }

    // --- Текст: переносится на 2 строки, при необходимости уменьшается ---
    Color textColor = neon ? WHITE : BLACK;
    if (o.state == ButtonState::Dim) textColor = neon ? Color{170, 170, 195, 255} : DARKGRAY;
    textColor = Fade(textColor, alpha);

    float size = o.fontSize;
    const std::vector<std::string> lines = FitLines(font, label, size, rect.width - 48.0f, 2, 14.0f);
    const float lineHeight = size * 1.2f * scale;
    const float totalHeight = lineHeight * static_cast<float>(lines.size());
    float textY = body.y + (body.height - totalHeight) / 2.0f;
    for (const std::string& line : lines) {
        const Vector2 measured = MeasureTextEx(font, line.c_str(), size * scale, 1.0f);
        const float textX = o.centered ? body.x + (body.width - measured.x) / 2.0f
                                       : body.x + 24.0f * scale;
        DrawTextEx(font, line.c_str(), Vector2{textX, textY}, size * scale, 1.0f, textColor);
        textY += lineHeight;
    }

    return clicked;
}

// Ползунок: двигается мышью, значение value меняется от 0 до 1.
// Возвращает true, пока игрок его тянет. Тянуть можно и за пределами дорожки.
inline bool UiSlider(Rectangle track, float& value) {
    const int id = static_cast<int>(track.y);
    const Rectangle hitArea{track.x - 14.0f, track.y - 18.0f, track.width + 28.0f, track.height + 36.0f};
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), hitArea);
    if (hovered) g_uiAnyHover = true;

    if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_sliderDrag = id;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && g_sliderDrag == id) g_sliderDrag = -1;

    const bool dragging = (g_sliderDrag == id);
    if (dragging) value = Clamp01((GetMousePosition().x - track.x) / track.width);

    DrawRectangleRec(track, Color{60, 65, 100, 255});                                       // дорожка
    DrawRectangleRec(Rectangle{track.x, track.y, track.width * value, track.height}, GOLD);  // заполнение
    const float radius = (dragging ? 18.0f : (hovered ? 16.0f : 14.0f));
    DrawCircleV(Vector2{track.x + track.width * value, track.y + track.height / 2.0f}, radius, WHITE);
    return dragging;
}

// ---------------------------------------------------------------------------
// Частицы (конфетти, искры, монетки)
// ---------------------------------------------------------------------------

struct Particle {
    Vector2 pos;
    Vector2 vel;     // скорость в пикселях в секунду
    float life;      // сколько секунд ещё проживёт
    float maxLife;   // сколько жила изначально (для плавного исчезновения)
    float size;
    Color color;
    bool round;      // круг или квадратик
};

// Запускает "взрыв" частиц из точки origin.
// Углы в радианах: 0 - вправо, -PI/2 - вверх (ось Y в окне идёт вниз).
inline void SpawnBurst(std::vector<Particle>& particles, Vector2 origin, int count,
                       const std::vector<Color>& palette, float angleMin, float angleMax,
                       float speedMin, float speedMax, bool round) {
    for (int i = 0; i < count; ++i) {
        const float angle = RandomFloat(angleMin, angleMax);
        const float speed = RandomFloat(speedMin, speedMax);
        Particle p;
        p.pos = origin;
        p.vel = Vector2{std::cos(angle) * speed, std::sin(angle) * speed};
        p.maxLife = RandomFloat(0.8f, 1.6f);
        p.life = p.maxLife;
        p.size = RandomFloat(6.0f, 12.0f);
        p.color = palette[static_cast<std::size_t>(GetRandomValue(0, static_cast<int>(palette.size()) - 1))];
        p.round = round;
        particles.push_back(p);
    }
}

// Разноцветное конфетти вверх - за правильный ответ.
inline void SpawnConfetti(std::vector<Particle>& particles, Vector2 origin) {
    SpawnBurst(particles, origin, 70, {GOLD, GREEN, SKYBLUE, PINK, ORANGE, VIOLET}, -PI, 0.0f,
               150.0f, 520.0f, false);
}

// Красные искры во все стороны - за неправильный ответ.
inline void SpawnSparks(std::vector<Particle>& particles, Vector2 origin) {
    SpawnBurst(particles, origin, 45, {RED, MAROON, Color{255, 120, 120, 255}}, -PI, PI, 100.0f,
               360.0f, true);
}

// Золотой фонтан - за монеты и покупки.
inline void SpawnCoinBurst(std::vector<Particle>& particles, Vector2 origin) {
    SpawnBurst(particles, origin, 40, {GOLD, YELLOW, ORANGE}, -PI, 0.0f, 120.0f, 380.0f, true);
}

// Двигает частицы, добавляет гравитацию и удаляет "умершие".
inline void UpdateParticles(std::vector<Particle>& particles, float dt) {
    for (Particle& p : particles) {
        p.vel.y += 700.0f * dt;  // гравитация
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        p.life -= dt;
    }
    // Идиома "стереть-удалить": убираем все частицы, у которых life <= 0.
    particles.erase(std::remove_if(particles.begin(), particles.end(),
                                   [](const Particle& p) { return p.life <= 0.0f; }),
                    particles.end());
}

inline void DrawParticles(const std::vector<Particle>& particles) {
    for (const Particle& p : particles) {
        const float alpha = std::clamp(p.life / p.maxLife, 0.0f, 1.0f);  // исчезают плавно
        if (p.round) {
            DrawCircleV(p.pos, p.size * 0.5f, Fade(p.color, alpha));
        } else {
            DrawRectangleV(p.pos, Vector2{p.size, p.size}, Fade(p.color, alpha));
        }
    }
}
