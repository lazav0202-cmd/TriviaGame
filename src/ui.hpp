// ui.hpp - интерфейс: шрифт, текст, кнопки (4 дизайна), ползунок, фон и частицы.

#pragma once

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

constexpr int SCREEN_WIDTH = 900;
constexpr int SCREEN_HEIGHT = 680;

// Цвет второстепенного текста.
constexpr Color MUTED_COLOR = {170, 175, 200, 255};

// ---------------------------------------------------------------------------
// Шрифт и текст
// ---------------------------------------------------------------------------

// Стандартный шрифт raylib не знает русских букв, поэтому берём системный
// TTF-шрифт и просим загрузить латиницу, кириллицу и несколько знаков.
inline Font LoadGameFont(int size) {
    std::vector<int> codepoints;
    for (int c = 32; c <= 126; ++c) codepoints.push_back(c);         // латиница, цифры
    for (int c = 0x0400; c <= 0x04FF; ++c) codepoints.push_back(c);  // кириллица
    for (int c : {0x00AB, 0x00BB, 0x00B2, 0x2013, 0x2014}) {         // « » ² – —
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

// Текст с переносом по словам, чтобы длинный вопрос помещался в окно.
// Возвращает высоту, которую занял текст.
inline float DrawWrappedText(const Font& font, const std::string& text, float x, float y,
                             float maxWidth, float fontSize, Color color) {
    const float spacing = 1.0f;
    const float lineHeight = fontSize * 1.3f;

    std::istringstream words(text);  // позволяет читать текст по словам
    std::string word;
    std::string line;
    float cursorY = y;

    while (words >> word) {
        const std::string candidate = line.empty() ? word : line + " " + word;
        // Если строка с новым словом уже не помещается - рисуем накопленное.
        if (!line.empty() &&
            MeasureTextEx(font, candidate.c_str(), fontSize, spacing).x > maxWidth) {
            DrawTextEx(font, line.c_str(), Vector2{x, cursorY}, fontSize, spacing, color);
            cursorY += lineHeight;
            line = word;
        } else {
            line = candidate;
        }
    }
    if (!line.empty()) {
        DrawTextEx(font, line.c_str(), Vector2{x, cursorY}, fontSize, spacing, color);
        cursorY += lineHeight;
    }
    return cursorY - y;
}

// ---------------------------------------------------------------------------
// Фон
// ---------------------------------------------------------------------------

// Тёмный градиент и медленно плавающие полупрозрачные круги.
inline void DrawBackground(float time) {
    DrawRectangleGradientV(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Color{34, 38, 72, 255},
                           Color{12, 14, 30, 255});
    for (int i = 0; i < 10; ++i) {
        const float fi = static_cast<float>(i);
        const float x = SCREEN_WIDTH * (0.5f + 0.45f * std::sin(time * 0.15f + fi * 1.7f));
        const float y = SCREEN_HEIGHT * (0.5f + 0.45f * std::cos(time * 0.12f + fi * 2.3f));
        const float radius = 30.0f + 12.0f * static_cast<float>(i % 4);
        DrawCircleV(Vector2{x, y}, radius, Fade(WHITE, 0.03f));
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

// Кнопка "в один вызов": рисует себя и возвращает true в тот кадр, когда по ней кликнули.
// clickable = false делает кнопку только картинкой (клики и наведение игнорируются).
inline bool UiButton(const Font& font, int style, Rectangle rect, const std::string& label,
                     ButtonState state = ButtonState::Normal, bool centered = true,
                     float fontSize = 26.0f, bool clickable = true) {
    const bool hovered = clickable && CheckCollisionPointRec(GetMousePosition(), rect);
    const bool held = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const bool clicked = hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const bool lit = hovered && state == ButtonState::Normal;
    const bool neon = (style == STYLE_NEON);
    const Color color = PickButtonColor(neon, state, lit);

    Rectangle body = rect;
    if (held) body.y += 2.0f;  // эффект "нажатия"

    switch (style) {
        case STYLE_ROUNDED: {
            // Тень, тёмная рамка и закруглённая заливка.
            DrawRectangleRounded(Rectangle{body.x, body.y + 4.0f, body.width, body.height}, 0.35f,
                                 12, Fade(BLACK, 0.35f));
            DrawRectangleRounded(body, 0.35f, 12, DARKGRAY);
            const Rectangle inner{body.x + 3.0f, body.y + 3.0f, body.width - 6.0f, body.height - 6.0f};
            DrawRectangleRounded(inner, 0.35f, 12, color);
            break;
        }
        case STYLE_NEON: {
            // Тёмная заливка, свечение из нескольких рамок и яркий контур.
            DrawRectangleRec(body, Color{16, 18, 40, 255});
            for (int i = 1; i <= 4; ++i) {
                const float grow = static_cast<float>(i) * 2.0f;
                const Rectangle glow{body.x - grow, body.y - grow, body.width + grow * 2.0f,
                                     body.height + grow * 2.0f};
                DrawRectangleLinesEx(glow, 2.0f, Fade(color, (lit ? 0.07f : 0.04f) * static_cast<float>(5 - i)));
            }
            DrawRectangleLinesEx(body, 3.0f, color);
            break;
        }
        case STYLE_RETRO: {
            // Пиксельная кнопка: жёсткая тень, светлые и тёмные грани.
            DrawRectangleRec(Rectangle{body.x + 4.0f, body.y + 4.0f, body.width, body.height}, BLACK);
            DrawRectangleRec(body, color);
            DrawRectangleRec(Rectangle{body.x, body.y, body.width, 4.0f}, Fade(WHITE, 0.6f));
            DrawRectangleRec(Rectangle{body.x, body.y, 4.0f, body.height}, Fade(WHITE, 0.6f));
            DrawRectangleRec(Rectangle{body.x, body.y + body.height - 4.0f, body.width, 4.0f},
                             Fade(BLACK, 0.3f));
            DrawRectangleRec(Rectangle{body.x + body.width - 4.0f, body.y, 4.0f, body.height},
                             Fade(BLACK, 0.3f));
            DrawRectangleLinesEx(body, 2.0f, BLACK);
            break;
        }
        default: {  // STYLE_CLASSIC
            DrawRectangleRec(body, color);
            DrawRectangleLinesEx(body, 2.0f, DARKGRAY);
            break;
        }
    }

    // Цвет текста зависит от дизайна и состояния.
    Color textColor = neon ? WHITE : BLACK;
    if (state == ButtonState::Dim) textColor = neon ? Color{170, 170, 195, 255} : DARKGRAY;

    const Vector2 measured = MeasureTextEx(font, label.c_str(), fontSize, 1.0f);
    const float textX = centered ? body.x + (body.width - measured.x) / 2.0f : body.x + 20.0f;
    const float textY = body.y + (body.height - measured.y) / 2.0f;
    DrawTextEx(font, label.c_str(), Vector2{textX, textY}, fontSize, 1.0f, textColor);

    return clicked;
}

// Ползунок: двигается мышью, значение value меняется от 0 до 1.
// Возвращает true, пока игрок его тянет.
inline bool UiSlider(Rectangle track, float& value) {
    const Rectangle hitArea{track.x - 12.0f, track.y - 16.0f, track.width + 24.0f, track.height + 32.0f};
    bool changed = false;
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), hitArea)) {
        value = std::clamp((GetMousePosition().x - track.x) / track.width, 0.0f, 1.0f);
        changed = true;
    }

    DrawRectangleRec(track, Color{60, 65, 100, 255});                                     // дорожка
    DrawRectangleRec(Rectangle{track.x, track.y, track.width * value, track.height}, GOLD);  // заполнение
    DrawCircleV(Vector2{track.x + track.width * value, track.y + track.height / 2.0f}, 14.0f, WHITE);
    return changed;
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
    SpawnBurst(particles, origin, 60, {GOLD, GREEN, SKYBLUE, PINK, ORANGE, VIOLET}, -PI, 0.0f,
               150.0f, 450.0f, false);
}

// Красные искры во все стороны - за неправильный ответ.
inline void SpawnSparks(std::vector<Particle>& particles, Vector2 origin) {
    SpawnBurst(particles, origin, 35, {RED, MAROON, Color{255, 120, 120, 255}}, -PI, PI, 100.0f,
               320.0f, true);
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
