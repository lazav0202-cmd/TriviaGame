// audio.hpp - звуки и музыка.
//
// Готовых аудиофайлов у нас нет, поэтому звуки СИНТЕЗИРУЮТСЯ прямо в коде:
// мы считаем форму звуковой волны для каждой ноты и отдаём её в raylib.

#pragma once

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <vector>

constexpr int SAMPLE_RATE = 44100;  // 44100 отсчётов звука в секунду - стандарт для CD

// Частоты нот в герцах. REST - пауза (тишина).
namespace Pitch {
constexpr float REST = 0.0f;
constexpr float E3 = 164.81f, G3 = 196.00f, A3 = 220.00f;
constexpr float C4 = 261.63f, E4 = 329.63f, F4 = 349.23f, G4 = 392.00f, A4 = 440.00f, B4 = 493.88f;
constexpr float C5 = 523.25f, D5 = 587.33f, E5 = 659.25f, G5 = 783.99f, A5 = 880.00f, B5 = 987.77f;
constexpr float C6 = 1046.50f, E6 = 1318.51f;
}  // namespace Pitch

// Форма волны определяет "тембр" звука.
enum class Waveform { Sine, Square, Triangle };

// Одна нота: частота и длительность в секундах.
struct Note {
    float freq;
    float seconds;
};

// Значение волны для фазы phase (число от 0 до 1) - результат от -1 до 1.
inline float Oscillator(Waveform shape, float phase) {
    switch (shape) {
        case Waveform::Square:
            return phase < 0.5f ? 1.0f : -1.0f;  // резкий "8-битный" звук
        case Waveform::Triangle:
            return 4.0f * std::fabs(phase - 0.5f) - 1.0f;  // мягкий звук
        default:
            return std::sin(phase * 2.0f * PI);  // чистый тон
    }
}

// Превращает список нот в готовый звук raylib.
inline Sound SynthesizeSound(const std::vector<Note>& notes, Waveform shape, float volume) {
    std::vector<short> samples;  // 16-битные отсчёты звука

    for (const Note& note : notes) {
        const int count = static_cast<int>(note.seconds * static_cast<float>(SAMPLE_RATE));
        float phase = 0.0f;
        for (int i = 0; i < count; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(count);  // прогресс ноты 0..1
            // Огибающая: быстро нарастаем в начале и затухаем к концу.
            // Без неё между нотами были бы неприятные щелчки.
            const float attack = std::min(t / 0.04f, 1.0f);
            const float release = std::min((1.0f - t) / 0.10f, 1.0f);
            const float envelope = attack * release * (1.0f - 0.4f * t);

            float value = 0.0f;
            if (note.freq > 0.0f) {
                value = Oscillator(shape, phase) * envelope * volume;
                phase += note.freq / static_cast<float>(SAMPLE_RATE);
                if (phase >= 1.0f) phase -= 1.0f;
            }
            samples.push_back(static_cast<short>(value * 32767.0f));
        }
    }

    if (samples.empty()) return Sound{};

    Wave wave{};
    wave.frameCount = static_cast<unsigned int>(samples.size());
    wave.sampleRate = SAMPLE_RATE;
    wave.sampleSize = 16;  // бит на отсчёт
    wave.channels = 1;     // моно
    wave.data = samples.data();
    return LoadSoundFromWave(wave);  // raylib копирует данные, поэтому samples можно освободить
}

// Короткая запись мелодии: список частот, все ноты одной длительности.
inline std::vector<Note> Melody(const std::vector<float>& freqs, float noteSeconds) {
    std::vector<Note> notes;
    for (float f : freqs) {
        notes.push_back(Note{f, noteSeconds});
    }
    return notes;
}

// Все звуки игры в одном месте.
struct SoundBank {
    bool ready = false;  // false, если звуковое устройство недоступно
    Sound click{};
    Sound correct{};
    Sound wrong{};
    Sound coin{};
    Sound buy{};
    Sound hover{};   // тихий "тик" при наведении на кнопку
    Sound whoosh{};  // переход между вопросами
    Sound lose{};    // конец игры
    std::vector<Sound> music;  // музыкальные темы, номер совпадает с магазином
};

// Создаёт все звуки. Вызывать после InitAudioDevice().
inline SoundBank CreateSoundBank() {
    using namespace Pitch;  // чтобы писать C5 вместо Pitch::C5

    SoundBank bank;
    bank.ready = IsAudioDeviceReady();
    if (!bank.ready) return bank;

    // Звуковые эффекты.
    bank.click = SynthesizeSound({{E5, 0.05f}}, Waveform::Triangle, 0.5f);
    bank.correct = SynthesizeSound({{C5, 0.09f}, {E5, 0.09f}, {G5, 0.09f}, {C6, 0.25f}},
                                   Waveform::Triangle, 0.6f);
    bank.wrong = SynthesizeSound({{C4, 0.12f}, {G3, 0.12f}, {E3, 0.30f}}, Waveform::Square, 0.28f);
    bank.coin = SynthesizeSound({{E5, 0.07f}, {A5, 0.07f}, {B5, 0.07f}, {E6, 0.30f}},
                                Waveform::Sine, 0.5f);
    bank.buy = SynthesizeSound({{A5, 0.06f}, {REST, 0.03f}, {E6, 0.30f}}, Waveform::Triangle, 0.5f);
    bank.hover = SynthesizeSound({{A5, 0.03f}}, Waveform::Sine, 0.25f);
    bank.whoosh = SynthesizeSound({{C4, 0.03f}, {E4, 0.03f}, {G4, 0.03f}, {C5, 0.06f}},
                                  Waveform::Triangle, 0.3f);
    bank.lose = SynthesizeSound({{G4, 0.18f}, {E4, 0.18f}, {C4, 0.18f}, {G3, 0.55f}},
                                Waveform::Triangle, 0.5f);

    // Музыкальные темы (каждая зацикливается).
    // 0 - "Спокойная"
    bank.music.push_back(SynthesizeSound(
        Melody({C4, E4, G4, E4, A3, C4, E4, C4, F4, A4, C5, A4, G4, B4, D5, B4}, 0.42f),
        Waveform::Triangle, 0.35f));
    // 1 - "Аркада"
    bank.music.push_back(SynthesizeSound(
        Melody({C5, E5, G5, E5, C5, E5, G5, E5, A4, C5, E5, C5, A4, C5, E5, C5,
                F4, A4, C5, A4, F4, A4, C5, A4, G4, B4, D5, B4, G4, B4, D5, G5},
               0.13f),
        Waveform::Square, 0.16f));
    // 2 - "Мечта"
    bank.music.push_back(SynthesizeSound(
        Melody({A4, C5, E5, C5, F4, A4, C5, A4, G4, B4, D5, B4, E4, G4, B4, G4}, 0.6f),
        Waveform::Sine, 0.4f));

    return bank;
}

// Освобождает память звуков. Вызывать до CloseAudioDevice().
inline void DestroySoundBank(SoundBank& bank) {
    if (!bank.ready) return;
    UnloadSound(bank.click);
    UnloadSound(bank.correct);
    UnloadSound(bank.wrong);
    UnloadSound(bank.coin);
    UnloadSound(bank.buy);
    UnloadSound(bank.hover);
    UnloadSound(bank.whoosh);
    UnloadSound(bank.lose);
    for (Sound& track : bank.music) {
        UnloadSound(track);
    }
}

// Проигрывает звуковой эффект с заданной громкостью.
inline void PlayEffect(const SoundBank& bank, const Sound& sound, float volume) {
    if (!bank.ready) return;
    SetSoundVolume(sound, volume);
    PlaySound(sound);
}

// Вызывается каждый кадр: держит играющей выбранную тему и зацикливает её.
inline void UpdateMusic(const SoundBank& bank, int selected, float volume) {
    if (!bank.ready || bank.music.empty()) return;

    const int count = static_cast<int>(bank.music.size());
    for (int i = 0; i < count; ++i) {
        if (i != selected && IsSoundPlaying(bank.music[i])) {
            StopSound(bank.music[i]);  // выключаем все темы, кроме выбранной
        }
    }
    if (selected < 0 || selected >= count) return;

    const Sound& track = bank.music[selected];
    SetSoundVolume(track, volume);
    if (!IsSoundPlaying(track)) {
        PlaySound(track);  // закончилась - запускаем заново
    }
}
