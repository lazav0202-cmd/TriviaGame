// save.hpp - прогресс игрока (монеты, покупки, настройки) и его сохранение в файл.

#pragma once

#include <algorithm>  // std::clamp, std::max
#include <array>
#include <cstddef>
#include <fstream>    // чтение и запись файлов
#include <string>

// ---------------------------------------------------------------------------
// Правила игры и каталог магазина
// ---------------------------------------------------------------------------
constexpr int STYLE_COUNT = 4;          // сколько дизайнов кнопок
constexpr int MUSIC_COUNT = 3;          // сколько музыкальных тем
constexpr int CORRECT_PER_REWARD = 5;   // каждые N верных ответов...
constexpr int COINS_PER_REWARD = 10;    // ...игрок получает столько монет

// Названия и цены. Первый элемент (цена 0) выдаётся бесплатно.
constexpr std::array<const char*, STYLE_COUNT> STYLE_NAMES = {"Классика", "Закруглённые",
                                                              "Неон", "Ретро"};
constexpr std::array<int, STYLE_COUNT> STYLE_PRICES = {0, 20, 30, 40};

constexpr std::array<const char*, MUSIC_COUNT> MUSIC_NAMES = {"Спокойная", "Аркада", "Мечта"};
constexpr std::array<int, MUSIC_COUNT> MUSIC_PRICES = {0, 20, 30};

// ---------------------------------------------------------------------------
// Данные, которые запоминаются между запусками игры
// ---------------------------------------------------------------------------
struct SaveData {
    int coins = 0;               // монеты
    int correctProgress = 0;     // сколько верных ответов набрано к следующей награде (0..4)
    std::array<bool, STYLE_COUNT> ownedStyles = {true, false, false, false};  // что куплено
    std::array<bool, MUSIC_COUNT> ownedMusic = {true, false, false};
    int selectedStyle = 0;       // какой дизайн кнопок выбран
    int selectedMusic = 0;       // какая музыка выбрана
    float musicVolume = 0.5f;    // громкость музыки 0..1
    float sfxVolume = 0.8f;      // громкость звуков 0..1
    bool effects = true;         // включены ли частицы и тряска экрана
    bool fullscreen = true;      // true - полный экран, false - обычное окно
    int bestStreak = 0;          // рекорд: сколько вопросов подряд отвечено верно
};

// Файл сохранения лежит рядом с программой.
constexpr const char* SAVE_FILE = "save.txt";

// template - "шаблон": одна функция работает с массивами любого размера N.
// Превращаем массив флагов в строку вида "1010".
template <std::size_t N>
std::string FlagsToString(const std::array<bool, N>& flags) {
    std::string text;
    for (bool flag : flags) {
        text += flag ? '1' : '0';
    }
    return text;
}

// Обратная операция: строка "1010" -> массив флагов.
template <std::size_t N>
void StringToFlags(const std::string& text, std::array<bool, N>& flags) {
    for (std::size_t i = 0; i < N && i < text.size(); ++i) {
        flags[i] = (text[i] == '1');
    }
}

// Приводит данные к допустимым значениям (если файл испортили вручную).
inline void FixSaveData(SaveData& d) {
    d.coins = std::max(d.coins, 0);
    d.bestStreak = std::max(d.bestStreak, 0);
    d.correctProgress = std::clamp(d.correctProgress, 0, CORRECT_PER_REWARD - 1);
    d.musicVolume = std::clamp(d.musicVolume, 0.0f, 1.0f);
    d.sfxVolume = std::clamp(d.sfxVolume, 0.0f, 1.0f);

    d.ownedStyles[0] = true;  // бесплатное всегда доступно
    d.ownedMusic[0] = true;

    d.selectedStyle = std::clamp(d.selectedStyle, 0, STYLE_COUNT - 1);
    if (!d.ownedStyles[d.selectedStyle]) d.selectedStyle = 0;
    d.selectedMusic = std::clamp(d.selectedMusic, 0, MUSIC_COUNT - 1);
    if (!d.ownedMusic[d.selectedMusic]) d.selectedMusic = 0;
}

// Записывает прогресс в файл: по одной строке "ключ значение".
inline void SaveGame(const SaveData& d) {
    std::ofstream out(SAVE_FILE);
    if (!out) return;  // не получилось открыть файл - просто молча выходим
    out << "coins " << d.coins << "\n";
    out << "progress " << d.correctProgress << "\n";
    out << "styles " << FlagsToString(d.ownedStyles) << "\n";
    out << "music " << FlagsToString(d.ownedMusic) << "\n";
    out << "style " << d.selectedStyle << "\n";
    out << "track " << d.selectedMusic << "\n";
    out << "musicVolume " << d.musicVolume << "\n";
    out << "sfxVolume " << d.sfxVolume << "\n";
    out << "effects " << (d.effects ? 1 : 0) << "\n";
    out << "fullscreen " << (d.fullscreen ? 1 : 0) << "\n";
    out << "best " << d.bestStreak << "\n";
}

// Читает прогресс из файла. Если файла нет - возвращает значения по умолчанию.
inline SaveData LoadGame() {
    SaveData d;
    std::ifstream in(SAVE_FILE);
    if (!in) return d;

    std::string key;
    while (in >> key) {  // читаем слово-ключ, затем значение
        if (key == "coins") {
            in >> d.coins;
        } else if (key == "progress") {
            in >> d.correctProgress;
        } else if (key == "styles") {
            std::string flags;
            in >> flags;
            StringToFlags(flags, d.ownedStyles);
        } else if (key == "music") {
            std::string flags;
            in >> flags;
            StringToFlags(flags, d.ownedMusic);
        } else if (key == "style") {
            in >> d.selectedStyle;
        } else if (key == "track") {
            in >> d.selectedMusic;
        } else if (key == "musicVolume") {
            in >> d.musicVolume;
        } else if (key == "sfxVolume") {
            in >> d.sfxVolume;
        } else if (key == "effects") {
            int value = 1;
            in >> value;
            d.effects = (value != 0);
        } else if (key == "fullscreen") {
            int value = 1;
            in >> value;
            d.fullscreen = (value != 0);
        } else if (key == "best") {
            in >> d.bestStreak;
        } else {
            std::string ignored;  // неизвестный ключ - пропускаем строку
            std::getline(in, ignored);
        }
    }
    FixSaveData(d);
    return d;
}
