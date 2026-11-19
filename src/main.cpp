// main.cpp - экраны игры и главный цикл.
//
// Сборка:
//   g++ -std=c++20 main.cpp -o trivia $(pkg-config --cflags --libs raylib)
// (остальные файлы - .hpp, они подключаются автоматически через #include)

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include "audio.hpp"
#include "questions.hpp"
#include "save.hpp"
#include "ui.hpp"

// Замени на своё имя - оно покажется на экране "Об авторе".
constexpr const char* AUTHOR_NAME = "Твоё имя";

// Сколько вопросов в одной партии.
constexpr std::size_t QUESTIONS_PER_GAME = 8;

// ---------------------------------------------------------------------------
// Типы данных
// ---------------------------------------------------------------------------

// Какой экран сейчас открыт.
enum class Screen { Menu, Playing, Settings, Shop, About };

// Что происходит внутри партии.
enum class State {
    Asking,    // ждём, пока игрок выберет ответ
    Feedback,  // ответ выбран: показываем верно или неверно
    Finished   // вопросы закончились: показываем результат
};

// Данные одной партии.
struct Game {
    std::vector<Question> questions;
    int current = 0;    // номер текущего вопроса
    int score = 0;      // верных ответов в этой партии
    int selected = -1;  // какой ответ выбрал игрок (-1 = пока никакой)
    State state = State::Asking;
};

// Всё состояние приложения в одном месте. Любой экран получает ссылку на App
// и может прочитать или изменить всё, что ему нужно.
struct App {
    Font font{};
    SaveData save;
    SoundBank sounds;
    std::mt19937 rng{std::random_device{}()};
    std::vector<Question> bank;
    Game game;
    Screen screen = Screen::Menu;
    bool quit = false;

    // Эффекты.
    std::vector<Particle> particles;
    float flashAlpha = 0.0f;   // яркость цветной вспышки экрана
    Color flashColor = WHITE;
    float shakeTimer = 0.0f;   // сколько ещё трясти экран
    float popupTimer = 0.0f;   // сколько ещё показывать "+10 монет!"
    std::string toast;         // короткое сообщение внизу экрана
    float toastTimer = 0.0f;
    bool sfxDragged = false;   // тянет ли игрок ползунок громкости звуков
};

// ---------------------------------------------------------------------------
// Небольшие помощники
// ---------------------------------------------------------------------------

void PlaySfx(App& app, const Sound& sound) {
    PlayEffect(app.sounds, sound, app.save.sfxVolume);
}

void PlayClick(App& app) {
    PlaySfx(app, app.sounds.click);
}

void ShowToast(App& app, const std::string& text) {
    app.toast = text;
    app.toastTimer = 2.0f;
}

// Начинает новую партию.
void StartGame(App& app) {
    app.game = Game{};
    app.game.questions = PickQuestions(app.bank, app.rng, QUESTIONS_PER_GAME);
    app.screen = Screen::Playing;
}

// Прямоугольник кнопки с ответом номер i (0..3).
Rectangle AnswerRect(int i) {
    return Rectangle{40.0f, 290.0f + static_cast<float>(i) * 85.0f, 820.0f, 70.0f};
}

// Игрок выбрал ответ номер index. Проверяем, начисляем награды, запускаем эффекты.
void AnswerQuestion(App& app, int index, Rectangle buttonRect) {
    Game& game = app.game;
    const Question& q = game.questions[static_cast<std::size_t>(game.current)];
    const Vector2 center{buttonRect.x + buttonRect.width / 2.0f, buttonRect.y + buttonRect.height / 2.0f};

    game.selected = index;
    game.state = State::Feedback;

    if (index == q.correct) {
        ++game.score;
        PlaySfx(app, app.sounds.correct);
        if (app.save.effects) {
            SpawnConfetti(app.particles, center);
            app.flashColor = Color{0, 255, 120, 255};
            app.flashAlpha = 0.25f;
        }

        // Каждые CORRECT_PER_REWARD верных ответов - монеты.
        ++app.save.correctProgress;
        if (app.save.correctProgress >= CORRECT_PER_REWARD) {
            app.save.correctProgress = 0;
            app.save.coins += COINS_PER_REWARD;
            app.popupTimer = 2.5f;
            PlaySfx(app, app.sounds.coin);
            if (app.save.effects) {
                SpawnCoinBurst(app.particles, Vector2{SCREEN_WIDTH / 2.0f, 50.0f});
            }
        }
    } else {
        PlaySfx(app, app.sounds.wrong);
        if (app.save.effects) {
            SpawnSparks(app.particles, center);
            app.flashColor = Color{255, 50, 70, 255};
            app.flashAlpha = 0.25f;
            app.shakeTimer = 0.4f;
        }
    }

    SaveGame(app.save);  // сохраняем прогресс после каждого ответа
}

// Пытается купить предмет. owned - флаг "куплено" этого предмета.
// Возвращает true, если покупка состоялась.
bool TryBuy(App& app, bool& owned, int price, const char* name) {
    if (app.save.coins < price) {
        PlaySfx(app, app.sounds.wrong);
        ShowToast(app, "Не хватает монет!");
        return false;
    }
    app.save.coins -= price;
    owned = true;
    PlaySfx(app, app.sounds.buy);
    if (app.save.effects) {
        SpawnCoinBurst(app.particles, Vector2{SCREEN_WIDTH / 2.0f, 330.0f});
    }
    ShowToast(app, std::string("Куплено: ") + name);
    return true;
}

// Подпись строки в магазине.
std::string ItemLabel(const char* name, bool owned, bool selected, int price) {
    if (selected) return std::string(name) + " (выбрано)";
    if (owned) return std::string(name) + " (выбрать)";
    return std::string(name) + " — " + std::to_string(price) + " монет";
}

// Кнопка "Назад" внизу экрана. Также срабатывает по клавише Esc.
bool BackPressed(App& app) {
    const Rectangle rect{40.0f, 600.0f, 200.0f, 52.0f};
    const bool clicked = UiButton(app.font, app.save.selectedStyle, rect, "Назад");
    return clicked || IsKeyPressed(KEY_ESCAPE);
}

void GoToMenu(App& app) {
    PlayClick(app);
    SaveGame(app.save);
    app.screen = Screen::Menu;
}

// Монеты справа вверху и 5 точек прогресса до следующей награды.
void DrawCoinHud(const App& app) {
    const std::string text = "Монеты: " + std::to_string(app.save.coins);
    const float width = MeasureTextEx(app.font, text.c_str(), 26.0f, 1.0f).x;
    DrawTextEx(app.font, text.c_str(), Vector2{SCREEN_WIDTH - 40.0f - width, 22.0f}, 26.0f, 1.0f, GOLD);

    for (int i = 0; i < CORRECT_PER_REWARD; ++i) {
        const float cx = SCREEN_WIDTH - 47.0f - static_cast<float>(CORRECT_PER_REWARD - 1 - i) * 22.0f;
        const Color dot = (i < app.save.correctProgress) ? GOLD : Fade(WHITE, 0.2f);
        DrawCircleV(Vector2{cx, 62.0f}, 7.0f, dot);
    }

    const char* label = "до награды";
    const float labelWidth = MeasureTextEx(app.font, label, 18.0f, 1.0f).x;
    const float firstDotX = SCREEN_WIDTH - 47.0f - static_cast<float>(CORRECT_PER_REWARD - 1) * 22.0f;
    DrawTextEx(app.font, label, Vector2{firstDotX - 17.0f - labelWidth, 52.0f}, 18.0f, 1.0f, MUTED_COLOR);
}

// ---------------------------------------------------------------------------
// Экраны. Каждая функция и обрабатывает ввод, и рисует (так делают
// "immediate mode"-интерфейсы: кнопка сама рисуется и сообщает о клике).
// ---------------------------------------------------------------------------

void ScreenMenu(App& app) {
    const float t = static_cast<float>(GetTime());
    DrawCoinHud(app);

    // Заголовок слегка "дышит" вверх-вниз.
    DrawTextCenteredX(app.font, "Programming Trivia", SCREEN_WIDTH / 2.0f,
                      56.0f + 6.0f * std::sin(t * 2.0f), 56.0f, WHITE);
    DrawTextCenteredX(app.font, "Викторина для программистов", SCREEN_WIDTH / 2.0f, 130.0f, 26.0f,
                      MUTED_COLOR);

    const char* labels[] = {"Играть", "Магазин", "Настройки", "Об авторе", "Выйти"};
    for (int i = 0; i < 5; ++i) {
        const Rectangle rect{270.0f, 195.0f + static_cast<float>(i) * 76.0f, 360.0f, 58.0f};
        if (UiButton(app.font, app.save.selectedStyle, rect, labels[i])) {
            PlayClick(app);
            switch (i) {
                case 0: StartGame(app); break;
                case 1: app.screen = Screen::Shop; break;
                case 2: app.screen = Screen::Settings; break;
                case 3: app.screen = Screen::About; break;
                default: app.quit = true; break;
            }
        }
    }
}

void ScreenSettings(App& app) {
    SaveData& s = app.save;
    const Font& font = app.font;

    DrawTextCenteredX(font, "Настройки", SCREEN_WIDTH / 2.0f, 28.0f, 44.0f, WHITE);

    // Громкость музыки.
    DrawTextEx(font, TextFormat("Громкость музыки: %d%%", static_cast<int>(s.musicVolume * 100.0f + 0.5f)),
               Vector2{150.0f, 150.0f}, 28.0f, 1.0f, WHITE);
    UiSlider(Rectangle{150.0f, 205.0f, 600.0f, 10.0f}, s.musicVolume);

    // Громкость звуков. После отпускания ползунка играем пример звука.
    DrawTextEx(font, TextFormat("Громкость звуков: %d%%", static_cast<int>(s.sfxVolume * 100.0f + 0.5f)),
               Vector2{150.0f, 275.0f}, 28.0f, 1.0f, WHITE);
    if (UiSlider(Rectangle{150.0f, 330.0f, 600.0f, 10.0f}, s.sfxVolume)) {
        app.sfxDragged = true;
    }
    if (app.sfxDragged && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        app.sfxDragged = false;
        PlaySfx(app, app.sounds.correct);
    }

    // Переключатель эффектов.
    const std::string effectsLabel =
        std::string("Эффекты (частицы, тряска): ") + (s.effects ? "вкл" : "выкл");
    if (UiButton(font, s.selectedStyle, Rectangle{150.0f, 410.0f, 600.0f, 60.0f}, effectsLabel)) {
        s.effects = !s.effects;
        PlayClick(app);
    }

    if (BackPressed(app)) GoToMenu(app);
}

void ScreenShop(App& app) {
    SaveData& s = app.save;
    const Font& font = app.font;

    DrawTextCenteredX(font, "Магазин", SCREEN_WIDTH / 2.0f, 22.0f, 44.0f, WHITE);
    DrawCoinHud(app);
    DrawTextEx(font, "Дизайн кнопок", Vector2{40.0f, 105.0f}, 28.0f, 1.0f, GOLD);
    DrawTextEx(font, "Музыка", Vector2{470.0f, 105.0f}, 28.0f, 1.0f, GOLD);

    // Левая колонка: дизайны кнопок. Каждая строка нарисована в том дизайне,
    // который продаёт, - так видно, что покупаешь.
    for (int i = 0; i < STYLE_COUNT; ++i) {
        const Rectangle rect{40.0f, 155.0f + static_cast<float>(i) * 80.0f, 400.0f, 64.0f};
        const bool owned = s.ownedStyles[static_cast<std::size_t>(i)];
        const bool selected = (s.selectedStyle == i);
        const ButtonState state =
            selected ? ButtonState::Correct : (owned ? ButtonState::Normal : ButtonState::Dim);
        const std::string label = ItemLabel(STYLE_NAMES[static_cast<std::size_t>(i)], owned, selected,
                                            STYLE_PRICES[static_cast<std::size_t>(i)]);

        if (UiButton(font, i, rect, label, state, true, 22.0f)) {
            if (owned) {
                s.selectedStyle = i;
                PlayClick(app);
                SaveGame(s);
            } else if (TryBuy(app, s.ownedStyles[static_cast<std::size_t>(i)],
                              STYLE_PRICES[static_cast<std::size_t>(i)],
                              STYLE_NAMES[static_cast<std::size_t>(i)])) {
                s.selectedStyle = i;  // купили - сразу выбираем
                SaveGame(s);
            }
        }
    }

    // Правая колонка: музыка.
    for (int i = 0; i < MUSIC_COUNT; ++i) {
        const Rectangle rect{470.0f, 155.0f + static_cast<float>(i) * 80.0f, 390.0f, 64.0f};
        const bool owned = s.ownedMusic[static_cast<std::size_t>(i)];
        const bool selected = (s.selectedMusic == i);
        const ButtonState state =
            selected ? ButtonState::Correct : (owned ? ButtonState::Normal : ButtonState::Dim);
        const std::string label = ItemLabel(MUSIC_NAMES[static_cast<std::size_t>(i)], owned, selected,
                                            MUSIC_PRICES[static_cast<std::size_t>(i)]);

        if (UiButton(font, s.selectedStyle, rect, label, state, true, 22.0f)) {
            if (owned) {
                s.selectedMusic = i;  // музыка переключится сама в главном цикле
                PlayClick(app);
                SaveGame(s);
            } else if (TryBuy(app, s.ownedMusic[static_cast<std::size_t>(i)],
                              MUSIC_PRICES[static_cast<std::size_t>(i)],
                              MUSIC_NAMES[static_cast<std::size_t>(i)])) {
                s.selectedMusic = i;
                SaveGame(s);
            }
        }
    }

    const std::string info = "Каждые " + std::to_string(CORRECT_PER_REWARD) +
                             " верных ответов дают +" + std::to_string(COINS_PER_REWARD) + " монет";
    DrawTextEx(font, info.c_str(), Vector2{270.0f, 616.0f}, 22.0f, 1.0f, MUTED_COLOR);

    if (BackPressed(app)) GoToMenu(app);
}

void ScreenAbout(App& app) {
    const Font& font = app.font;
    const float cx = SCREEN_WIDTH / 2.0f;

    DrawTextCenteredX(font, "Об авторе", cx, 28.0f, 44.0f, WHITE);
    DrawTextCenteredX(font, std::string("Автор: ") + AUTHOR_NAME, cx, 150.0f, 34.0f, GOLD);

    DrawWrappedText(font, "Programming Trivia — викторина по программированию. "
                          "Это моя первая игра на C++.",
                    120.0f, 230.0f, 660.0f, 28.0f, WHITE);
    DrawWrappedText(font, "Написана на C++20 с библиотекой raylib. Вся музыка и все звуки "
                          "создаются прямо в коде, без аудиофайлов.",
                    120.0f, 330.0f, 660.0f, 28.0f, WHITE);
    DrawTextCenteredX(font, "Версия 1.0", cx, 470.0f, 24.0f, MUTED_COLOR);

    if (BackPressed(app)) GoToMenu(app);
}

void ScreenGame(App& app) {
    Game& game = app.game;
    const Font& font = app.font;
    const int style = app.save.selectedStyle;
    const int total = static_cast<int>(game.questions.size());

    if (IsKeyPressed(KEY_ESCAPE)) {  // выход из партии в меню
        app.screen = Screen::Menu;
        return;
    }

    // Переход к следующему вопросу по пробелу, Enter или клику.
    if (game.state == State::Feedback &&
        (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
        ++game.current;
        game.selected = -1;
        game.state = (game.current >= total) ? State::Finished : State::Asking;
    }

    DrawCoinHud(app);

    // ----- Экран результата -----
    if (game.state == State::Finished) {
        const float cx = SCREEN_WIDTH / 2.0f;
        DrawTextCenteredX(font, "Игра окончена!", cx, 110.0f, 52.0f, GOLD);
        DrawTextCenteredX(font, TextFormat("Твой результат: %d из %d", game.score, total), cx, 200.0f,
                          36.0f, WHITE);

        const char* verdict = "Не сдавайся, попробуй ещё раз!";
        if (game.score == total) {
            verdict = "Отлично! Ты настоящий программист!";
        } else if (game.score >= total / 2) {
            verdict = "Неплохо! Есть куда расти.";
        }
        DrawTextCenteredX(font, verdict, cx, 265.0f, 28.0f, Color{120, 230, 160, 255});
        DrawTextCenteredX(font,
                          TextFormat("Верных ответов до награды: %d",
                                     CORRECT_PER_REWARD - app.save.correctProgress),
                          cx, 330.0f, 24.0f, MUTED_COLOR);

        if (UiButton(font, style, Rectangle{270.0f, 430.0f, 360.0f, 58.0f}, "Играть снова")) {
            PlayClick(app);
            StartGame(app);
        }
        if (UiButton(font, style, Rectangle{270.0f, 506.0f, 360.0f, 58.0f}, "В меню")) {
            PlayClick(app);
            app.screen = Screen::Menu;
        }
        return;
    }

    // ----- Экран вопроса -----
    const Question& q = game.questions[static_cast<std::size_t>(game.current)];

    DrawTextEx(font, TextFormat("Вопрос %d из %d", game.current + 1, total), Vector2{40.0f, 25.0f},
               24.0f, 1.0f, MUTED_COLOR);
    DrawWrappedText(font, q.text, 40.0f, 85.0f, 820.0f, 30.0f, WHITE);

    if (game.state == State::Feedback) {
        if (game.selected == q.correct) {
            DrawTextEx(font, "Верно!", Vector2{40.0f, 240.0f}, 28.0f, 1.0f, Color{120, 230, 160, 255});
        } else {
            DrawTextEx(font, "Неверно! Правильный ответ выделен зелёным.", Vector2{40.0f, 240.0f},
                       28.0f, 1.0f, Color{255, 130, 140, 255});
        }
    }

    // Четыре кнопки с ответами.
    for (int i = 0; i < 4; ++i) {
        const Rectangle rect = AnswerRect(i);
        const bool asking = (game.state == State::Asking);

        ButtonState state = ButtonState::Normal;
        if (!asking) {
            if (i == q.correct) {
                state = ButtonState::Correct;
            } else if (i == game.selected) {
                state = ButtonState::Wrong;
            } else {
                state = ButtonState::Dim;
            }
        }

        const std::string label = std::to_string(i + 1) + ". " + q.answers[static_cast<std::size_t>(i)];
        const bool clicked = UiButton(font, style, rect, label, state, false, 26.0f, asking);
        if (clicked || (asking && IsKeyPressed(KEY_ONE + i))) {  // клик или клавиши 1..4
            AnswerQuestion(app, i, rect);
        }
    }

    const char* hint = (game.state == State::Asking) ? "Кликни по ответу или нажми 1-4. Esc - меню"
                                                     : "Пробел, Enter или клик - дальше";
    DrawTextEx(font, hint, Vector2{40.0f, 642.0f}, 20.0f, 1.0f, MUTED_COLOR);
}

// ---------------------------------------------------------------------------
// Эффекты поверх экранов
// ---------------------------------------------------------------------------

void UpdateTimers(App& app, float dt) {
    app.flashAlpha = std::max(0.0f, app.flashAlpha - dt * 0.8f);
    app.shakeTimer = std::max(0.0f, app.shakeTimer - dt);
    app.popupTimer = std::max(0.0f, app.popupTimer - dt);
    app.toastTimer = std::max(0.0f, app.toastTimer - dt);
    UpdateParticles(app.particles, dt);
}

void DrawRewardPopup(const App& app) {
    if (app.popupTimer <= 0.0f) return;
    const float alpha = std::min(1.0f, app.popupTimer / 0.5f);
    const std::string text = "+" + std::to_string(COINS_PER_REWARD) + " монет!";
    DrawTextCenteredX(app.font, text, SCREEN_WIDTH / 2.0f, 18.0f, 36.0f, Fade(GOLD, alpha));
}

void DrawToast(const App& app) {
    if (app.toastTimer <= 0.0f) return;
    const float alpha = std::min(1.0f, app.toastTimer / 0.4f);
    const Vector2 size = MeasureTextEx(app.font, app.toast.c_str(), 26.0f, 1.0f);
    const Rectangle box{SCREEN_WIDTH / 2.0f - size.x / 2.0f - 20.0f, 540.0f, size.x + 40.0f, 50.0f};
    DrawRectangleRounded(box, 0.4f, 10, Fade(BLACK, 0.7f * alpha));
    DrawTextEx(app.font, app.toast.c_str(), Vector2{box.x + 20.0f, box.y + 12.0f}, 26.0f, 1.0f,
               Fade(WHITE, alpha));
}

// ---------------------------------------------------------------------------
// Главная функция
// ---------------------------------------------------------------------------
int main() {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Programming Trivia");
    InitAudioDevice();
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);  // Esc больше не закрывает окно: мы используем его для "Назад"

    App app;
    app.font = LoadGameFont(48);
    app.save = LoadGame();
    app.sounds = CreateSoundBank();
    app.bank = GetQuestionBank();

    // Игровой цикл: один проход = один кадр.
    while (!app.quit && !WindowShouldClose()) {
        UpdateTimers(app, GetFrameTime());
        UpdateMusic(app.sounds, app.save.selectedMusic, app.save.musicVolume);

        BeginDrawing();
        DrawBackground(static_cast<float>(GetTime()));

        // Тряска экрана: камера со случайным смещением.
        Camera2D camera{};
        camera.zoom = 1.0f;
        if (app.shakeTimer > 0.0f) {
            const float strength = 12.0f * (app.shakeTimer / 0.4f);
            camera.offset = Vector2{RandomFloat(-1.0f, 1.0f) * strength, RandomFloat(-1.0f, 1.0f) * strength};
        }

        BeginMode2D(camera);
        switch (app.screen) {
            case Screen::Menu:     ScreenMenu(app);     break;
            case Screen::Playing:  ScreenGame(app);     break;
            case Screen::Settings: ScreenSettings(app); break;
            case Screen::Shop:     ScreenShop(app);     break;
            case Screen::About:    ScreenAbout(app);    break;
        }
        EndMode2D();

        DrawParticles(app.particles);
        if (app.flashAlpha > 0.0f) {
            DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(app.flashColor, app.flashAlpha));
        }
        DrawRewardPopup(app);
        DrawToast(app);
        EndDrawing();
    }

    SaveGame(app.save);
    DestroySoundBank(app.sounds);
    UnloadFont(app.font);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
