// main.cpp - экраны игры и главный цикл.
//
// Сборка (в окне MSYS2 UCRT64):
//   g++ -std=c++20 main.cpp ai.cpp -o trivia -pthread $(pkg-config --cflags --libs raylib libcurl)
// (остальные файлы - .hpp, они подключаются автоматически через #include)

#include "raylib.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <ctime>
#include <deque>
#include <future>
#include <random>
#include <string>
#include <vector>

#include "ai.hpp"
#include "audio.hpp"
#include "questions.hpp"
#include "save.hpp"
#include "ui.hpp"

// Замени на своё имя - оно покажется на экране "Об авторе".
constexpr const char* AUTHOR_NAME = "Твоё имя";

// Версия игры. Меняется при каждом большом обновлении (см. CHANGELOG.md).
constexpr const char* GAME_VERSION = "2.0.0";

// Длительности стадий вопроса (в секундах).
constexpr float INTRO_TIME = 0.75f;           // вопрос и ответы "влетают"
constexpr float REVEAL_CORRECT_TIME = 1.15f;  // показ верного ответа
constexpr float REVEAL_WRONG_TIME = 2.0f;     // показ ошибки перед концом игры
constexpr float LEAVE_TIME = 0.5f;            // вопрос "улетает"

// ---------------------------------------------------------------------------
// Типы данных
// ---------------------------------------------------------------------------

// Какой экран сейчас открыт.
enum class Screen { Menu, Playing, Settings, Shop, About, ApiKey };

// Стадии игры внутри экрана Playing.
enum class State {
    Loading,   // ждём вопросы от ИИ
    Error,     // не удалось получить вопросы
    Intro,     // вопрос "влетает" на экран (клики игнорируются)
    Asking,    // ждём ответа игрока
    Reveal,    // показываем, верно ли ответил игрок
    Leaving,   // вопрос "улетает" после верного ответа (клики игнорируются)
    Finished   // игра окончена
};

// Данные одной игры. Игра идёт, пока игрок отвечает верно; первая ошибка = конец.
struct Game {
    Question current;                  // текущий вопрос
    int streak = 0;                    // сколько верных ответов подряд
    int selected = -1;                 // какой ответ выбрал игрок (-1 = пока никакой)
    State state = State::Loading;
    float timer = 0.0f;                // сколько секунд длится текущая стадия
    bool offline = false;              // true - играем запасными вопросами без ИИ
    std::deque<Question> offlineQueue; // очередь запасных вопросов
    bool lost = false;                 // true - игрок ошибся; false - вопросы кончились
    bool newRecord = false;            // побит ли рекорд серии
};

// Всё, что связано с ИИ: фоновый запрос и очередь готовых вопросов.
struct AiState {
    std::future<AiResult> pending;     // результат фонового запроса (когда будет готов)
    bool busy = false;                 // идёт ли запрос прямо сейчас
    bool failed = false;               // последний запрос закончился ошибкой
    std::string error;                 // текст ошибки
    float busyTime = 0.0f;             // сколько секунд идёт запрос
    std::deque<Question> ready;        // готовые вопросы от ИИ
    std::vector<std::string> recent;   // недавние вопросы - просим ИИ их не повторять
};

// Всё состояние приложения в одном месте. Любой экран получает ссылку на App.
struct App {
    Font font{};
    SaveData save;
    SoundBank sounds;
    std::mt19937 rng{std::random_device{}()};
    std::vector<Question> bank;        // запасные вопросы (только для офлайн-режима)
    Game game;
    AiState ai;
    std::string apiKey;                // ключ API
    std::string keyInput;              // текст в поле ввода ключа
    float backHold = 0.0f;             // как долго зажат Backspace
    Screen screen = Screen::Menu;
    float screenTime = 0.0f;           // сколько секунд открыт текущий экран
    bool quit = false;

    // Эффекты.
    std::vector<Particle> particles;
    float flashAlpha = 0.0f;           // яркость цветной вспышки экрана
    Color flashColor = WHITE;
    float shakeTimer = 0.0f;           // сколько ещё трясти экран
    float popupTimer = 0.0f;           // сколько ещё показывать "+10 монет!"
    float streakPop = 0.0f;            // "подпрыгивание" счётчика серии
    std::string toast;                 // короткое сообщение внизу экрана
    float toastTimer = 0.0f;
    bool sfxDragged = false;           // тянет ли игрок ползунок громкости звуков
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

// Переключает экран и запускает анимацию его появления.
void SetScreen(App& app, Screen screen) {
    app.screen = screen;
    app.screenTime = 0.0f;
    UiResetAnimations();  // кнопки нового экрана выедут заново
    while (GetCharPressed() > 0) {}  // выбрасываем "залежавшиеся" нажатия клавиш
}

void SetState(Game& game, State state) {
    game.state = state;
    game.timer = 0.0f;
}

// Включает или выключает полноэкранный режим (без смены разрешения монитора).
void ApplyDisplayMode(bool fullscreen) {
    const bool isFullscreen = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    if (fullscreen != isFullscreen) {
        ToggleBorderlessWindowed();
    }
}

// Чем длиннее серия, тем сложнее просим вопросы.
std::string DifficultyFor(int streak) {
    if (streak < 4) return "лёгкие, для новичков";
    if (streak < 10) return "средней сложности";
    if (streak < 18) return "сложные";
    return "очень сложные, для опытных разработчиков";
}

// ---------------------------------------------------------------------------
// Вопросы от ИИ
// ---------------------------------------------------------------------------

// Запускает фоновый запрос к ИИ. Игра при этом не зависает.
void StartFetch(App& app) {
    AiState& ai = app.ai;
    if (ai.busy || app.apiKey.empty()) return;

    // Берём 4 случайные темы.
    std::vector<std::string> topics = AllTopics();
    std::shuffle(topics.begin(), topics.end(), app.rng);
    topics.resize(4);

    // Последние 25 вопросов отправляем, чтобы ИИ не повторялся.
    std::vector<std::string> avoid;
    const std::size_t from = ai.recent.size() > 25 ? ai.recent.size() - 25 : 0;
    for (std::size_t i = from; i < ai.recent.size(); ++i) {
        avoid.push_back(TruncateUtf8(ai.recent[i], 90));
    }

    // std::async запускает функцию в отдельном потоке и возвращает "обещание" результата.
    ai.pending = std::async(std::launch::async, RequestQuestions, app.apiKey, AI_BATCH_SIZE,
                            topics, DifficultyFor(app.game.streak), avoid);
    ai.busy = true;
    ai.busyTime = 0.0f;
}

// Проверяет, не пришёл ли ответ от ИИ. Вызывается каждый кадр.
void PollFetch(App& app) {
    AiState& ai = app.ai;
    if (!ai.busy) return;
    if (ai.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

    AiResult result = ai.pending.get();
    ai.busy = false;
    if (!result.ok) {
        ai.failed = true;
        ai.error = result.error;
        return;
    }

    for (Question& q : result.questions) {
        // Пропускаем вопросы, которые уже были.
        if (std::find(ai.recent.begin(), ai.recent.end(), q.text) != ai.recent.end()) continue;
        ShuffleAnswers(q, app.rng);  // ИИ любит ставить верный ответ на одно место - перемешиваем
        ai.recent.push_back(q.text);
        ai.ready.push_back(q);
    }
    if (ai.recent.size() > 60) {
        ai.recent.erase(ai.recent.begin(), ai.recent.begin() + 30);
    }
    if (ai.ready.empty()) {
        ai.failed = true;
        ai.error = "ИИ прислал только повторяющиеся вопросы. Попробуй ещё раз.";
    } else {
        ai.failed = false;
    }
}

// ---------------------------------------------------------------------------
// Ход игры
// ---------------------------------------------------------------------------

// Если серия побила рекорд - запоминаем его.
void CommitRun(App& app) {
    if (app.game.streak > app.save.bestStreak) {
        app.save.bestStreak = app.game.streak;
        app.game.newRecord = true;
        SaveGame(app.save);
    }
}

// Конец игры: lost = true, если игрок ошибся.
void FinishGame(App& app, bool lost) {
    Game& g = app.game;
    g.lost = lost;
    CommitRun(app);
    SetState(g, State::Finished);
    PlaySfx(app, lost ? app.sounds.lose : app.sounds.correct);
    SaveGame(app.save);
}

// Готовит следующий вопрос: берёт готовый или ждёт ответа ИИ.
void PrepareNext(App& app) {
    Game& g = app.game;
    AiState& ai = app.ai;
    g.selected = -1;

    if (g.offline) {
        if (g.offlineQueue.empty()) {
            FinishGame(app, false);  // запасные вопросы кончились - победа
            return;
        }
        g.current = g.offlineQueue.front();
        g.offlineQueue.pop_front();
        SetState(g, State::Intro);
        PlaySfx(app, app.sounds.whoosh);
        return;
    }

    if (!ai.ready.empty()) {
        g.current = ai.ready.front();
        ai.ready.pop_front();
        SetState(g, State::Intro);
        PlaySfx(app, app.sounds.whoosh);
        return;
    }

    if (app.apiKey.empty()) {
        ai.error = "Не задан API-ключ. Без него ИИ не сможет придумывать вопросы.";
        SetState(g, State::Error);
        return;
    }
    if (ai.failed && !ai.busy) {
        SetState(g, State::Error);
        return;
    }
    StartFetch(app);
    SetState(g, State::Loading);
}

// Начинает новую игру. offline = true - играть запасными вопросами без ИИ.
void StartGame(App& app, bool offline) {
    app.game = Game{};
    app.game.offline = offline;
    app.ai.failed = false;  // даём ИИ новую попытку
    if (offline) {
        const std::vector<Question> all = PickQuestions(app.bank, app.rng, app.bank.size());
        app.game.offlineQueue.assign(all.begin(), all.end());
    }
    SetScreen(app, Screen::Playing);
    PrepareNext(app);
}

// Игрок выбрал ответ номер index. Проверяем, начисляем награды, запускаем эффекты.
void AnswerQuestion(App& app, int index, Rectangle buttonRect) {
    Game& g = app.game;
    const Vector2 center{buttonRect.x + buttonRect.width / 2.0f, buttonRect.y + buttonRect.height / 2.0f};

    g.selected = index;
    SetState(g, State::Reveal);

    if (index == g.current.correct) {
        ++g.streak;
        app.streakPop = 1.0f;
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
        g.lost = true;  // первая ошибка = проигрыш
        PlaySfx(app, app.sounds.wrong);
        if (app.save.effects) {
            SpawnSparks(app.particles, center);
            app.flashColor = Color{255, 50, 70, 255};
            app.flashAlpha = 0.3f;
            app.shakeTimer = 0.45f;
        }
    }

    SaveGame(app.save);  // сохраняем прогресс после каждого ответа
}

// Пытается купить предмет. owned - флаг "куплено" этого предмета.
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
    const Rectangle rect{40.0f, 640.0f, 200.0f, 52.0f};
    const bool clicked = UiButton(app.font, app.save.selectedStyle, rect, "Назад");
    return clicked || IsKeyPressed(KEY_ESCAPE);
}

void GoToMenu(App& app) {
    PlayClick(app);
    SaveGame(app.save);
    SetScreen(app, Screen::Menu);
}

// Монеты справа вверху и 5 точек прогресса до следующей награды.
void DrawCoinHud(const App& app) {
    const std::string text = "Монеты: " + std::to_string(app.save.coins);
    const float width = MeasureTextEx(app.font, text.c_str(), 28.0f, 1.0f).x;
    DrawTextEx(app.font, text.c_str(), Vector2{SCREEN_WIDTH - 40.0f - width, 20.0f}, 28.0f, 1.0f, GOLD);

    for (int i = 0; i < CORRECT_PER_REWARD; ++i) {
        const float cx = SCREEN_WIDTH - 47.0f - static_cast<float>(CORRECT_PER_REWARD - 1 - i) * 22.0f;
        const Color dot = (i < app.save.correctProgress) ? GOLD : Fade(WHITE, 0.2f);
        DrawCircleV(Vector2{cx, 66.0f}, 7.0f, dot);
    }

    const char* label = "до награды";
    const float labelWidth = MeasureTextEx(app.font, label, 18.0f, 1.0f).x;
    const float firstDotX = SCREEN_WIDTH - 47.0f - static_cast<float>(CORRECT_PER_REWARD - 1) * 22.0f;
    DrawTextEx(app.font, label, Vector2{firstDotX - 17.0f - labelWidth, 56.0f}, 18.0f, 1.0f, MUTED_COLOR);
}

// Серия и рекорд слева вверху. При верном ответе число "подпрыгивает".
void DrawStreakHud(const App& app) {
    const float size = 32.0f * (1.0f + 0.45f * app.streakPop);
    DrawTextEx(app.font, TextFormat("Серия: %d", app.game.streak),
               Vector2{40.0f, 20.0f - (size - 32.0f) / 2.0f}, size, 1.0f, GOLD);
    DrawTextEx(app.font, TextFormat("Рекорд: %d", app.save.bestStreak), Vector2{40.0f, 62.0f}, 20.0f, 1.0f,
               MUTED_COLOR);
}

// Скрывает ключ: показывает только начало и конец.
std::string MaskKey(const std::string& key) {
    if (key.size() <= 14) return std::string(key.size(), '*');
    return key.substr(0, 7) + "..." + key.substr(key.size() - 4);
}

// ---------------------------------------------------------------------------
// Экраны. Каждая функция и обрабатывает ввод, и рисует (так делают
// "immediate mode"-интерфейсы: кнопка сама рисуется и сообщает о клике).
// ---------------------------------------------------------------------------

void ScreenMenu(App& app) {
    const Font& font = app.font;
    const float t = static_cast<float>(GetTime());
    const float cx = SCREEN_WIDTH / 2.0f;
    DrawCoinHud(app);

    // Заголовок "влетает" сверху и потом слегка "дышит".
    const float titleIn = EaseOutBack(Clamp01(app.screenTime / 0.6f));
    DrawTextCenteredX(font, "Programming Trivia", cx,
                      70.0f + 6.0f * std::sin(t * 2.0f) - (1.0f - titleIn) * 90.0f, 78.0f,
                      Fade(WHITE, Clamp01(app.screenTime / 0.4f)));
    DrawTextCenteredX(font, "Викторина для программистов — вопросы придумывает ИИ", cx, 168.0f, 26.0f,
                      Fade(MUTED_COLOR, Clamp01((app.screenTime - 0.2f) / 0.4f)));
    if (app.save.bestStreak > 0) {
        DrawTextCenteredX(font, TextFormat("Рекорд серии: %d", app.save.bestStreak), cx, 204.0f, 24.0f,
                          Fade(GOLD, Clamp01((app.screenTime - 0.3f) / 0.4f)));
    }

    const char* labels[] = {"Играть", "Магазин", "Настройки", "Об авторе", "Выйти"};
    for (int i = 0; i < 5; ++i) {
        const Rectangle rect{430.0f, 250.0f + static_cast<float>(i) * 76.0f, 420.0f, 60.0f};
        if (UiButton(font, app.save.selectedStyle, rect, labels[i])) {
            PlayClick(app);
            switch (i) {
                case 0: StartGame(app, false); break;
                case 1: SetScreen(app, Screen::Shop); break;
                case 2: SetScreen(app, Screen::Settings); break;
                case 3: SetScreen(app, Screen::About); break;
                default: app.quit = true; break;
            }
        }
    }
}

void ScreenSettings(App& app) {
    SaveData& s = app.save;
    const Font& font = app.font;
    const float cx = SCREEN_WIDTH / 2.0f;

    DrawTextCenteredX(font, "Настройки", cx, 28.0f, 52.0f, WHITE);

    // Громкость музыки.
    DrawTextEx(font, TextFormat("Громкость музыки: %d%%", static_cast<int>(s.musicVolume * 100.0f + 0.5f)),
               Vector2{290.0f, 120.0f}, 28.0f, 1.0f, WHITE);
    UiSlider(Rectangle{290.0f, 178.0f, 700.0f, 10.0f}, s.musicVolume);

    // Громкость звуков. После отпускания ползунка играем пример звука.
    DrawTextEx(font, TextFormat("Громкость звуков: %d%%", static_cast<int>(s.sfxVolume * 100.0f + 0.5f)),
               Vector2{290.0f, 225.0f}, 28.0f, 1.0f, WHITE);
    if (UiSlider(Rectangle{290.0f, 283.0f, 700.0f, 10.0f}, s.sfxVolume)) {
        app.sfxDragged = true;
    }
    if (app.sfxDragged && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        app.sfxDragged = false;
        PlaySfx(app, app.sounds.correct);
    }

    // Переключатель эффектов.
    const std::string effectsLabel =
        std::string("Эффекты (частицы, тряска): ") + (s.effects ? "вкл" : "выкл");
    if (UiButton(font, s.selectedStyle, Rectangle{290.0f, 345.0f, 700.0f, 60.0f}, effectsLabel)) {
        s.effects = !s.effects;
        PlayClick(app);
    }

    // Переключатель: полный экран или окно.
    const std::string displayLabel =
        std::string("Режим экрана: ") + (s.fullscreen ? "полный экран" : "в окне") + "  (F11)";
    if (UiButton(font, s.selectedStyle, Rectangle{290.0f, 425.0f, 700.0f, 60.0f}, displayLabel)) {
        s.fullscreen = !s.fullscreen;
        ApplyDisplayMode(s.fullscreen);
        PlayClick(app);
        SaveGame(s);
    }

    // Ключ для ИИ.
    const std::string keyLabel = std::string("API-ключ для ИИ: ") + (app.apiKey.empty() ? "не задан" : "задан");
    if (UiButton(font, s.selectedStyle, Rectangle{290.0f, 505.0f, 700.0f, 60.0f}, keyLabel)) {
        PlayClick(app);
        app.keyInput = app.apiKey;
        SetScreen(app, Screen::ApiKey);
    }

    if (BackPressed(app)) GoToMenu(app);
}

void ScreenApiKey(App& app) {
    const Font& font = app.font;
    const float cx = SCREEN_WIDTH / 2.0f;
    const float dt = UiDelta();

    // --- Ввод с клавиатуры ---
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    int ch = 0;
    while ((ch = GetCharPressed()) > 0) {  // GetCharPressed отдаёт набранные символы по одному
        if (!ctrl && ch >= 33 && ch <= 126 && app.keyInput.size() < 200) {
            app.keyInput.push_back(static_cast<char>(ch));
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE)) {
        if (!app.keyInput.empty()) app.keyInput.pop_back();
        app.backHold = 0.0f;
    } else if (IsKeyDown(KEY_BACKSPACE)) {  // зажали - стираем непрерывно
        app.backHold += dt;
        if (app.backHold > 0.45f) {
            if (!app.keyInput.empty()) app.keyInput.pop_back();
            app.backHold = 0.40f;
        }
    }

    // Вставка из буфера обмена (Ctrl+V или кнопка). Оставляем только видимые символы.
    auto pasteFromClipboard = [&app]() {
        const char* clip = GetClipboardText();
        if (!clip) return;
        std::string pasted;
        for (const char* p = clip; *p != '\0'; ++p) {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c >= 33 && c <= 126) pasted.push_back(static_cast<char>(c));
        }
        app.keyInput = pasted.substr(0, 200);
    };
    if (ctrl && IsKeyPressed(KEY_V)) pasteFromClipboard();

    // --- Рисование ---
    DrawTextCenteredX(font, "API-ключ для ИИ", cx, 28.0f, 52.0f, WHITE);
    DrawParagraph(font,
                  "Вопросы придумывает ИИ Claude, поэтому нужен API-ключ. Это платный сервис, "
                  "отдельный от подписки Claude: ключ создаётся в Claude Console "
                  "(console.anthropic.com), раздел «API keys», а на счёте должны быть деньги.",
                  190.0f, 110.0f, 900.0f, 24.0f, MUTED_COLOR, true);
    DrawParagraph(font,
                  "Ключ хранится только в файле api_key.txt рядом с игрой и отправляется только "
                  "на api.anthropic.com. Никому не показывай этот файл.",
                  190.0f, 225.0f, 900.0f, 22.0f, Fade(MUTED_COLOR, 0.8f), true);

    DrawTextField(font, Rectangle{240.0f, 320.0f, 800.0f, 64.0f}, MaskKey(app.keyInput), "sk-ant-...",
                  static_cast<float>(GetTime()));
    DrawTextCenteredX(font, TextFormat("Введено символов: %d", static_cast<int>(app.keyInput.size())), cx,
                      394.0f, 20.0f, MUTED_COLOR);

    const int style = app.save.selectedStyle;
    if (UiButton(font, style, Rectangle{240.0f, 440.0f, 250.0f, 58.0f}, "Вставить", {.fontSize = 26.0f})) {
        PlayClick(app);
        pasteFromClipboard();
    }
    if (UiButton(font, style, Rectangle{515.0f, 440.0f, 250.0f, 58.0f}, "Очистить", {.fontSize = 26.0f})) {
        PlayClick(app);
        app.keyInput.clear();
    }
    if (UiButton(font, style, Rectangle{790.0f, 440.0f, 250.0f, 58.0f}, "Сохранить", {.fontSize = 26.0f})) {
        PlayClick(app);
        app.apiKey = TrimSpaces(app.keyInput);
        SaveApiKey(app.apiKey);
        app.ai.failed = false;  // со свежим ключом можно пробовать снова
        ShowToast(app, app.apiKey.empty() ? "Ключ удалён" : "Ключ сохранён");
    }

    DrawTextCenteredX(font, app.apiKey.empty() ? "Сейчас: ключ не задан" : "Сейчас: ключ задан", cx, 530.0f,
                      24.0f, app.apiKey.empty() ? BAD_COLOR : GOOD_COLOR);

    if (BackPressed(app)) {
        PlayClick(app);
        SetScreen(app, Screen::Settings);
    }
}

void ScreenShop(App& app) {
    SaveData& s = app.save;
    const Font& font = app.font;

    DrawTextCenteredX(font, "Магазин", SCREEN_WIDTH / 2.0f, 24.0f, 52.0f, WHITE);
    DrawCoinHud(app);
    DrawTextEx(font, "Дизайн кнопок", Vector2{80.0f, 120.0f}, 30.0f, 1.0f, GOLD);
    DrawTextEx(font, "Музыка", Vector2{680.0f, 120.0f}, 30.0f, 1.0f, GOLD);

    // Левая колонка: дизайны кнопок. Каждая строка нарисована в том дизайне,
    // который продаёт, - так видно, что покупаешь.
    for (int i = 0; i < STYLE_COUNT; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        const Rectangle rect{80.0f, 175.0f + static_cast<float>(i) * 84.0f, 520.0f, 66.0f};
        const bool owned = s.ownedStyles[idx];
        const bool selected = (s.selectedStyle == i);
        ButtonOpts opts;
        opts.state = selected ? ButtonState::Correct : (owned ? ButtonState::Normal : ButtonState::Dim);
        opts.fontSize = 24.0f;

        if (UiButton(font, i, rect, ItemLabel(STYLE_NAMES[idx], owned, selected, STYLE_PRICES[idx]), opts)) {
            if (owned) {
                s.selectedStyle = i;
                PlayClick(app);
                SaveGame(s);
            } else if (TryBuy(app, s.ownedStyles[idx], STYLE_PRICES[idx], STYLE_NAMES[idx])) {
                s.selectedStyle = i;  // купили - сразу выбираем
                SaveGame(s);
            }
        }
    }

    // Правая колонка: музыка.
    for (int i = 0; i < MUSIC_COUNT; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        const Rectangle rect{680.0f, 175.0f + static_cast<float>(i) * 84.0f, 520.0f, 66.0f};
        const bool owned = s.ownedMusic[idx];
        const bool selected = (s.selectedMusic == i);
        ButtonOpts opts;
        opts.state = selected ? ButtonState::Correct : (owned ? ButtonState::Normal : ButtonState::Dim);
        opts.fontSize = 24.0f;

        if (UiButton(font, s.selectedStyle, rect,
                     ItemLabel(MUSIC_NAMES[idx], owned, selected, MUSIC_PRICES[idx]), opts)) {
            if (owned) {
                s.selectedMusic = i;  // музыка переключится сама в главном цикле
                PlayClick(app);
                SaveGame(s);
            } else if (TryBuy(app, s.ownedMusic[idx], MUSIC_PRICES[idx], MUSIC_NAMES[idx])) {
                s.selectedMusic = i;
                SaveGame(s);
            }
        }
    }

    const std::string info = "Каждые " + std::to_string(CORRECT_PER_REWARD) +
                             " верных ответов дают +" + std::to_string(COINS_PER_REWARD) + " монет";
    DrawTextCenteredX(font, info, SCREEN_WIDTH / 2.0f, 590.0f, 24.0f, MUTED_COLOR);

    if (BackPressed(app)) GoToMenu(app);
}

void ScreenAbout(App& app) {
    const Font& font = app.font;
    const float cx = SCREEN_WIDTH / 2.0f;

    DrawTextCenteredX(font, "Об авторе", cx, 28.0f, 52.0f, WHITE);
    DrawTextCenteredX(font, std::string("Автор: ") + AUTHOR_NAME, cx, 150.0f, 38.0f, GOLD);

    DrawParagraph(font, "Programming Trivia — викторина по программированию. Это моя первая игра на C++.",
                  190.0f, 240.0f, 900.0f, 30.0f, WHITE, true);
    DrawParagraph(font,
                  "Написана на C++20 с библиотекой raylib. Вопросы придумывает ИИ Claude прямо во время "
                  "игры, а вся музыка и все звуки создаются в коде, без аудиофайлов.",
                  190.0f, 350.0f, 900.0f, 30.0f, WHITE, true);
    DrawTextCenteredX(font, std::string("Версия ") + GAME_VERSION, cx, 520.0f, 24.0f, MUTED_COLOR);

    if (BackPressed(app)) GoToMenu(app);
}

// --- Игровой экран: загрузка вопросов ---
void DrawLoading(App& app) {
    const Font& font = app.font;
    const float cx = SCREEN_WIDTH / 2.0f;
    const float t = static_cast<float>(GetTime());

    DrawSpinner(Vector2{cx, 270.0f}, 54.0f, t);
    const int dots = static_cast<int>(t * 2.5f) % 4;
    DrawTextCenteredX(font, "ИИ придумывает вопросы" + std::string(static_cast<std::size_t>(dots), '.'), cx,
                      370.0f, 38.0f, WHITE);
    DrawTextCenteredX(font, TextFormat("Прошло секунд: %d", static_cast<int>(app.ai.busyTime)), cx, 430.0f,
                      24.0f, MUTED_COLOR);

    if (UiButton(font, app.save.selectedStyle, Rectangle{cx - 150.0f, 520.0f, 300.0f, 58.0f}, "Отмена")) {
        GoToMenu(app);
    }
}

// --- Игровой экран: ошибка получения вопросов ---
void DrawError(App& app) {
    const Font& font = app.font;
    const float cx = SCREEN_WIDTH / 2.0f;
    const int style = app.save.selectedStyle;

    DrawTextCenteredX(font, "Не удалось получить вопросы", cx, 90.0f, 52.0f, BAD_COLOR);
    DrawParagraph(font, app.ai.error, 240.0f, 180.0f, 800.0f, 28.0f, WHITE, true, 4);

    const Rectangle r0{cx - 300.0f, 330.0f, 600.0f, 60.0f};
    const Rectangle r1{cx - 300.0f, 406.0f, 600.0f, 60.0f};
    const Rectangle r2{cx - 300.0f, 482.0f, 600.0f, 60.0f};
    const Rectangle r3{cx - 300.0f, 558.0f, 600.0f, 60.0f};

    if (UiButton(font, style, r0, "Повторить")) {
        PlayClick(app);
        StartGame(app, false);
    }
    if (UiButton(font, style, r1, "Сменить API-ключ")) {
        PlayClick(app);
        app.keyInput = app.apiKey;
        SetScreen(app, Screen::ApiKey);
    }
    if (UiButton(font, style, r2, "Играть без ИИ (запасные вопросы)")) {
        PlayClick(app);
        StartGame(app, true);
    }
    if (UiButton(font, style, r3, "В меню")) {
        GoToMenu(app);
    }
}

// --- Игровой экран: конец игры ---
void DrawFinished(App& app) {
    Game& g = app.game;
    const Font& font = app.font;
    const int style = app.save.selectedStyle;
    const float cx = SCREEN_WIDTH / 2.0f;
    const float t = g.timer;

    // Заголовок "падает" сверху и проявляется.
    const char* title = g.lost ? "Игра окончена!" : "Ты ответил на все вопросы!";
    DrawTextCenteredX(font, title, cx, 50.0f - (1.0f - EaseOutCubic(t / 0.6f)) * 100.0f, 62.0f,
                      Fade(GOLD, Clamp01(t / 0.4f)));

    if (g.lost) {
        // Показываем вопрос и правильный ответ, чтобы игрок узнал что-то новое.
        const float a = Clamp01((t - 0.35f) / 0.4f);
        DrawTextCenteredX(font, "Ты ошибся в вопросе:", cx, 150.0f, 26.0f, Fade(MUTED_COLOR, a));
        DrawParagraph(font, g.current.text, 190.0f, 188.0f, 900.0f, 28.0f, Fade(WHITE, a), true, 3);
        DrawParagraph(font, "Правильный ответ: " + g.current.answers[static_cast<std::size_t>(g.current.correct)],
                      190.0f, 306.0f, 900.0f, 28.0f, Fade(GOOD_COLOR, a), true, 2);
    }

    // Число серии "накручивается" от нуля до результата.
    const float streakY = g.lost ? 400.0f : 250.0f;
    const int shown = static_cast<int>(std::round(static_cast<float>(g.streak) * EaseOutCubic((t - 0.6f) / 1.0f)));
    DrawTextCenteredX(font, TextFormat("Серия: %d", shown), cx, streakY, 48.0f, Fade(WHITE, Clamp01((t - 0.5f) / 0.3f)));

    const float recordAlpha = Clamp01((t - 1.4f) / 0.4f);
    if (g.newRecord) {
        DrawTextCenteredX(font, "Новый рекорд!", cx, streakY + 58.0f, 28.0f + 3.0f * std::sin(t * 6.0f),
                          Fade(GOLD, recordAlpha));
    } else {
        DrawTextCenteredX(font, TextFormat("Рекорд: %d", app.save.bestStreak), cx, streakY + 58.0f, 24.0f,
                          Fade(MUTED_COLOR, recordAlpha));
    }

    const bool offline = g.offline;
    if (UiButton(font, style, Rectangle{cx - 210.0f, 520.0f, 420.0f, 56.0f}, "Играть снова")) {
        PlayClick(app);
        StartGame(app, offline);
        return;  // app.game пересоздан - дальше g использовать нельзя
    }
    if (UiButton(font, style, Rectangle{cx - 210.0f, 592.0f, 420.0f, 56.0f}, "В меню")) {
        GoToMenu(app);
    }
}

// --- Игровой экран: вопрос с вариантами ответа ---
void DrawQuestion(App& app) {
    Game& g = app.game;
    const Font& font = app.font;
    const int style = app.save.selectedStyle;
    const Question& q = g.current;

    // Сдвиги и прозрачность вопроса и каждого ответа зависят от стадии.
    float qOff = 0.0f;
    float qAlpha = 1.0f;
    std::array<float, 4> off = {0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> alp = {1.0f, 1.0f, 1.0f, 1.0f};

    if (g.state == State::Intro) {  // всё "влетает" справа, ответы по очереди
        const float tq = Clamp01(g.timer / 0.45f);
        qOff = (1.0f - EaseOutCubic(tq)) * 900.0f;
        qAlpha = tq;
        for (int i = 0; i < 4; ++i) {
            const float ti = Clamp01((g.timer - 0.08f * static_cast<float>(i)) / 0.45f);
            off[static_cast<std::size_t>(i)] = (1.0f - EaseOutCubic(ti)) * 1000.0f;
            alp[static_cast<std::size_t>(i)] = Clamp01(ti * 1.5f);
        }
    } else if (g.state == State::Leaving) {  // всё "улетает" влево
        const float tq = Clamp01(g.timer / 0.35f);
        qOff = -EaseInCubic(tq) * 900.0f;
        qAlpha = 1.0f - tq;
        for (int i = 0; i < 4; ++i) {
            const float ti = Clamp01((g.timer - 0.04f * static_cast<float>(i)) / 0.35f);
            off[static_cast<std::size_t>(i)] = -EaseInCubic(ti) * 1000.0f;
            alp[static_cast<std::size_t>(i)] = 1.0f - ti;
        }
    }

    // Вопрос на тёмной подложке.
    DrawTextEx(font, TextFormat("Вопрос %d", g.streak + 1), Vector2{140.0f + qOff, 80.0f}, 24.0f, 1.0f,
               Fade(MUTED_COLOR, qAlpha));
    DrawPanel(Rectangle{120.0f + qOff, 112.0f, 1040.0f, 196.0f}, qAlpha);
    DrawParagraph(font, q.text, 150.0f + qOff, 130.0f, 980.0f, 38.0f, Fade(WHITE, qAlpha), false, 3);

    // Четыре кнопки с ответами в сетке 2x2.
    const bool asking = (g.state == State::Asking);
    bool answered = false;
    for (int i = 0; i < 4; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        const Rectangle rect{(i % 2 == 0) ? 140.0f : 650.0f, 340.0f + static_cast<float>(i / 2) * 120.0f,
                             490.0f, 100.0f};

        ButtonOpts opts;
        opts.centered = false;
        opts.fontSize = 30.0f;
        opts.entrance = false;  // выезжают своей анимацией, а не стандартной
        opts.id = 100 + i;
        opts.clickable = asking;
        opts.alpha = alp[idx];
        opts.offsetX = off[idx];

        if (g.state == State::Reveal) {
            if (i == q.correct) {
                opts.state = ButtonState::Correct;
                // Верный ответ "пульсирует".
                opts.scale = 1.0f + 0.07f * std::sin(g.timer * 14.0f) * std::exp(-g.timer * 2.5f);
            } else if (i == g.selected) {
                opts.state = ButtonState::Wrong;
                // Неверный ответ "трясётся".
                opts.offsetX += std::sin(g.timer * 60.0f) * 14.0f * std::exp(-g.timer * 4.0f);
            } else {
                opts.state = ButtonState::Dim;
            }
        } else if (g.state == State::Leaving && i == q.correct) {
            opts.state = ButtonState::Correct;
        } else if (g.state == State::Leaving && i != q.correct) {
            opts.state = ButtonState::Dim;
        }

        const std::string label = std::to_string(i + 1) + ". " + q.answers[idx];
        const bool clicked = UiButton(font, style, rect, label, opts);
        if (asking && !answered && (clicked || IsKeyPressed(KEY_ONE + i))) {  // клик или клавиши 1..4
            answered = true;
            AnswerQuestion(app, i, rect);
        }
    }

    // Подпись с результатом "выпрыгивает" с лёгким пружинением.
    if (g.state == State::Reveal) {
        const float pop = EaseOutBack(g.timer / 0.35f);
        const float size = std::max(1.0f, 38.0f * pop);  // в первый кадр pop = 0, а размер 0 не нужен
        const bool correct = (g.selected == q.correct);
        DrawTextCenteredX(font, correct ? "Верно!" : "Неправильно! Игра окончена", SCREEN_WIDTH / 2.0f,
                          596.0f - (size - 38.0f) / 2.0f, size, correct ? GOOD_COLOR : BAD_COLOR);
    }

    const char* hint = asking ? "Кликни по ответу или нажми 1-4. Esc — меню"
                              : (g.state == State::Reveal ? "Пробел или Enter — пропустить паузу" : "");
    DrawTextCenteredX(font, hint, SCREEN_WIDTH / 2.0f, 678.0f, 20.0f, MUTED_COLOR);
}

void ScreenGame(App& app) {
    Game& g = app.game;
    AiState& ai = app.ai;
    g.timer += UiDelta();

    if (IsKeyPressed(KEY_ESCAPE)) {  // выход в меню в любой момент
        CommitRun(app);
        GoToMenu(app);
        return;
    }

    // Пока игра идёт, заранее просим у ИИ следующую порцию, чтобы не ждать между вопросами.
    if (!g.offline && g.state != State::Finished && !ai.busy && !ai.failed && !app.apiKey.empty() &&
        ai.ready.size() <= 3) {
        StartFetch(app);
    }

    // --- Обновление стадий ---
    switch (g.state) {
        case State::Loading:
            if (!ai.ready.empty()) {
                PrepareNext(app);
            } else if (ai.failed && !ai.busy) {
                SetState(g, State::Error);
            } else if (!ai.busy) {
                StartFetch(app);
            }
            break;
        case State::Intro:
            if (g.timer >= INTRO_TIME) SetState(g, State::Asking);
            break;
        case State::Reveal: {
            const float duration = g.lost ? REVEAL_WRONG_TIME : REVEAL_CORRECT_TIME;
            // Пропуск паузы только клавишами: клик мог бы "провалиться" в следующий вопрос.
            if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) g.timer = std::max(g.timer, duration);
            if (g.timer >= duration) {
                if (g.lost) {
                    FinishGame(app, true);  // ошибка = конец игры
                } else {
                    SetState(g, State::Leaving);
                    PlaySfx(app, app.sounds.whoosh);
                }
            }
            break;
        }
        case State::Leaving:
            if (g.timer >= LEAVE_TIME) PrepareNext(app);
            break;
        default:
            break;
    }

    // --- Рисование ---
    DrawCoinHud(app);
    if (g.state != State::Error) DrawStreakHud(app);

    switch (g.state) {
        case State::Loading:  DrawLoading(app);  break;
        case State::Error:    DrawError(app);    break;
        case State::Finished: DrawFinished(app); break;
        default:              DrawQuestion(app); break;
    }
}

// ---------------------------------------------------------------------------
// Эффекты поверх экранов
// ---------------------------------------------------------------------------

void UpdateTimers(App& app, float dt) {
    app.screenTime += dt;
    app.flashAlpha = std::max(0.0f, app.flashAlpha - dt * 0.8f);
    app.shakeTimer = std::max(0.0f, app.shakeTimer - dt);
    app.popupTimer = std::max(0.0f, app.popupTimer - dt);
    app.toastTimer = std::max(0.0f, app.toastTimer - dt);
    app.streakPop = std::max(0.0f, app.streakPop - dt * 2.5f);
    if (app.ai.busy) app.ai.busyTime += dt;
    UpdateParticles(app.particles, dt);
}

void DrawRewardPopup(const App& app) {
    if (app.popupTimer <= 0.0f) return;
    const float alpha = std::min(1.0f, app.popupTimer / 0.5f);
    const float rise = (2.5f - app.popupTimer) * 12.0f;  // плывёт вниз и растворяется
    const std::string text = "+" + std::to_string(COINS_PER_REWARD) + " монет!";
    DrawTextCenteredX(app.font, text, SCREEN_WIDTH / 2.0f, 18.0f + rise, 40.0f, Fade(GOLD, alpha));
}

void DrawToast(const App& app) {
    if (app.toastTimer <= 0.0f) return;
    const float alpha = std::min(1.0f, app.toastTimer / 0.4f);
    const float slide = (1.0f - EaseOutCubic((2.0f - app.toastTimer) / 0.25f)) * 30.0f;  // выезжает снизу
    const Vector2 size = MeasureTextEx(app.font, app.toast.c_str(), 28.0f, 1.0f);
    const Rectangle box{SCREEN_WIDTH / 2.0f - size.x / 2.0f - 24.0f, 625.0f + slide, size.x + 48.0f, 54.0f};
    DrawRectangleRounded(box, 0.4f, 10, Fade(BLACK, 0.75f * alpha));
    DrawTextEx(app.font, app.toast.c_str(), Vector2{box.x + 24.0f, box.y + 12.0f}, 28.0f, 1.0f,
               Fade(WHITE, alpha));
}

// ---------------------------------------------------------------------------
// Главная функция
// ---------------------------------------------------------------------------
int main() {
    AiGlobalInit();  // включаем сетевую библиотеку

    App app;
    app.save = LoadGame();
    app.apiKey = LoadApiKey();
    app.bank = GetQuestionBank();

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Programming Trivia");
    InitAudioDevice();
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);  // Esc больше не закрывает окно: мы используем его для "Назад"
    SetWindowMinSize(640, 360);
    SetRandomSeed(static_cast<unsigned int>(app.rng()));  // чтобы конфетти каждый раз было разным
    ApplyDisplayMode(app.save.fullscreen);

    app.font = LoadGameFont(64);
    app.sounds = CreateSoundBank();

    // Холст фиксированного размера: рисуем в него, потом растягиваем на окно.
    RenderTexture2D canvas = LoadRenderTexture(SCREEN_WIDTH, SCREEN_HEIGHT);
    SetTextureFilter(canvas.texture, TEXTURE_FILTER_BILINEAR);

    // Игровой цикл: один проход = один кадр.
    while (!app.quit && !WindowShouldClose()) {
        const float dt = UiDelta();

        // F11 - переключение полного экрана с клавиатуры.
        if (IsKeyPressed(KEY_F11)) {
            app.save.fullscreen = !app.save.fullscreen;
            ApplyDisplayMode(app.save.fullscreen);
            SaveGame(app.save);
        }

        PollFetch(app);
        UpdateTimers(app, dt);
        UpdateMusic(app.sounds, app.save.selectedMusic, app.save.musicVolume);

        // Считаем, как холст вписывается в окно (с чёрными полосами по краям, если пропорции другие),
        // и учим мышь пересчитывать координаты из экранных в координаты холста.
        const float screenW = static_cast<float>(GetScreenWidth());
        const float screenH = static_cast<float>(GetScreenHeight());
        float scale = std::min(screenW / SCREEN_WIDTH, screenH / SCREEN_HEIGHT);
        if (scale <= 0.0f) scale = 1.0f;
        const float offsetX = (screenW - SCREEN_WIDTH * scale) / 2.0f;
        const float offsetY = (screenH - SCREEN_HEIGHT * scale) / 2.0f;
        SetMouseOffset(static_cast<int>(-offsetX), static_cast<int>(-offsetY));
        SetMouseScale(1.0f / scale, 1.0f / scale);

        // --- Рисуем всё в холст ---
        UiBeginFrame();
        BeginTextureMode(canvas);
        DrawBackground(static_cast<float>(GetTime()));

        // Тряска экрана: камера со случайным смещением.
        Camera2D camera{};
        camera.zoom = 1.0f;
        if (app.shakeTimer > 0.0f) {
            const float strength = 12.0f * (app.shakeTimer / 0.45f);
            camera.offset = Vector2{RandomFloat(-1.0f, 1.0f) * strength, RandomFloat(-1.0f, 1.0f) * strength};
        }

        BeginMode2D(camera);
        switch (app.screen) {
            case Screen::Menu:     ScreenMenu(app);     break;
            case Screen::Playing:  ScreenGame(app);     break;
            case Screen::Settings: ScreenSettings(app); break;
            case Screen::Shop:     ScreenShop(app);     break;
            case Screen::About:    ScreenAbout(app);    break;
            case Screen::ApiKey:   ScreenApiKey(app);   break;
        }
        EndMode2D();

        DrawParticles(app.particles);
        if (app.flashAlpha > 0.0f) {
            DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(app.flashColor, app.flashAlpha));
        }
        DrawRewardPopup(app);
        DrawToast(app);

        // Плавное проявление нового экрана из тёмного.
        const float fade = 1.0f - EaseOutCubic(app.screenTime / 0.3f);
        if (fade > 0.01f) {
            DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(Color{12, 14, 30, 255}, fade));
        }
        EndTextureMode();

        // Звук наведения и курсор-рука над кнопками.
        if (UiConsumeHoverStarted()) PlayEffect(app.sounds, app.sounds.hover, app.save.sfxVolume * 0.6f);
        SetMouseCursor(g_uiAnyHover ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);

        // --- Выводим холст на экран. Высота источника отрицательная: текстуры хранятся "вверх ногами". ---
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(canvas.texture,
                       Rectangle{0.0f, 0.0f, static_cast<float>(canvas.texture.width),
                                 -static_cast<float>(canvas.texture.height)},
                       Rectangle{offsetX, offsetY, SCREEN_WIDTH * scale, SCREEN_HEIGHT * scale},
                       Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        EndDrawing();
    }

    // --- Аккуратное завершение ---
    g_aiCancel = true;                 // просим сетевой запрос прерваться
    if (app.ai.busy) app.ai.pending.wait();  // и дожидаемся фонового потока
    SaveGame(app.save);
    DestroySoundBank(app.sounds);
    UnloadRenderTexture(canvas);
    UnloadFont(app.font);
    CloseAudioDevice();
    CloseWindow();
    AiGlobalCleanup();
    return 0;
}
