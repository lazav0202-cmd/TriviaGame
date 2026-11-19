// ai.hpp - вопросы от ИИ: запрос к Claude API через libcurl и разбор ответа.
//
// ВАЖНО: сетевая часть (libcurl) лежит в отдельном файле ai.cpp. libcurl на Windows подключает
// windows.h, а в нём есть имена (DrawTextEx, Rectangle, ...), которые конфликтуют с raylib.
// Поэтому raylib.h и curl.h не должны встречаться в одном .cpp файле.
//
// Как это работает:
//   1. Мы формируем текст-просьбу (промпт): "придумай 8 вопросов, верни JSON".
//   2. Отправляем её HTTPS-запросом на api.anthropic.com (библиотека libcurl).
//   3. Получаем ответ, достаём из него JSON с вопросами и превращаем в struct Question.
//
// Запрос занимает несколько секунд, поэтому main.cpp запускает RequestQuestions
// в ОТДЕЛЬНОМ ПОТОКЕ - игра в это время не зависает.

#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "json.hpp"
#include "questions.hpp"

// ---------------------------------------------------------------------------
// Настройки (можно менять)
// ---------------------------------------------------------------------------
constexpr const char* AI_MODEL = "claude-haiku-5-5";  // быстрая и дешёвая модель
constexpr const char* AI_URL = "https://api.anthropic.com/v1/messages";
constexpr const char* AI_VERSION = "2023-06-01";       // версия протокола API
constexpr const char* KEY_FILE = "api_key.txt";        // здесь хранится ключ
constexpr int AI_BATCH_SIZE = 8;                       // сколько вопросов просим за раз

// Флаг "прервать запрос": ставим true при выходе из игры, чтобы окно не ждало ответа.
inline std::atomic<bool> g_aiCancel{false};

// Результат запроса: либо вопросы, либо понятный текст ошибки.
struct AiResult {
    bool ok = false;
    std::string error;
    std::vector<Question> questions;
};

// ---------------------------------------------------------------------------
// Работа с ключом
// ---------------------------------------------------------------------------

// Убирает пробелы, переводы строк и невидимые символы по краям.
inline std::string TrimSpaces(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && static_cast<unsigned char>(text[begin]) <= ' ') ++begin;
    while (end > begin && static_cast<unsigned char>(text[end - 1]) <= ' ') --end;
    return text.substr(begin, end - begin);
}

// Берёт ключ из файла api_key.txt, а если файла нет - из переменной среды ANTHROPIC_API_KEY.
inline std::string LoadApiKey() {
    std::ifstream in(KEY_FILE);
    if (in) {
        std::string line;
        std::getline(in, line);
        line = TrimSpaces(line);
        if (!line.empty()) return line;
    }
    if (const char* env = std::getenv("ANTHROPIC_API_KEY")) {
        return TrimSpaces(env);
    }
    return "";
}

// Записывает ключ в файл api_key.txt (пустая строка = ключ удалён).
inline void SaveApiKey(const std::string& key) {
    std::ofstream out(KEY_FILE);
    if (out) out << key << "\n";
}

// ---------------------------------------------------------------------------
// Подготовка запроса
// ---------------------------------------------------------------------------

// Темы, из которых выбираются случайные для каждой порции вопросов.
inline std::vector<std::string> AllTopics() {
    return {"основы C++", "Python", "JavaScript и веб-разработка",
            "алгоритмы и структуры данных", "Git и инструменты разработчика",
            "базы данных и SQL", "компьютерные сети и интернет", "операционные системы",
            "история программирования и компьютеров", "основы информационной безопасности",
            "логика и математика в программировании", "терминал и командная строка",
            "принципы ООП и чистого кода", "как устроены компьютеры (процессор, память)"};
}

// Экранирует текст для вставки в JSON-строку: кавычки, слэши, переводы строк.
inline std::string JsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 16);
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 0x0F];
                } else {
                    out += ch;  // русские буквы (UTF-8) JSON принимает как есть
                }
        }
    }
    return out;
}

// Обрезает строку до maxBytes байт, не разрывая русские буквы посередине.
inline std::string TruncateUtf8(const std::string& text, std::size_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    std::size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut);
}

// Склеивает строки через разделитель: {"а","б"} -> "а, б".
inline std::string JoinStrings(const std::vector<std::string>& parts, const std::string& sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += sep;
        out += parts[i];
    }
    return out;
}

// Текст просьбы к ИИ.
inline std::string BuildPrompt(int count, const std::vector<std::string>& topics,
                               const std::string& difficulty,
                               const std::vector<std::string>& avoid) {
    std::string prompt =
        "Ты автор вопросов для игры-викторины по программированию.\n"
        "Придумай " + std::to_string(count) + " РАЗНЫХ вопросов.\n"
        "Темы (распредели вопросы между ними): " + JoinStrings(topics, ", ") + ".\n"
        "Сложность: " + difficulty + ".\n"
        "Требования:\n"
        "- вопросы и ответы на русском языке (термины можно оставлять на английском);\n"
        "- ровно 4 варианта ответа: один верный, три правдоподобных, но неверных;\n"
        "- вопрос не длиннее 150 символов, каждый ответ не длиннее 50 символов;\n"
        "- без многострочного кода, только факты, в которых ты абсолютно уверен;\n"
        "- верные ответы должны стоять на разных позициях.\n";
    if (!avoid.empty()) {
        prompt += "- НЕ повторяй и не перефразируй эти вопросы: " + JoinStrings(avoid, " | ") + "\n";
    }
    prompt +=
        "Верни ТОЛЬКО JSON-массив без пояснений и без markdown. Формат:\n"
        "[{\"q\":\"текст вопроса\",\"a\":[\"вариант 1\",\"вариант 2\",\"вариант 3\",\"вариант 4\"],\"c\":0}]\n"
        "где c - номер верного ответа от 0 до 3.";
    return prompt;
}

// ---------------------------------------------------------------------------
// Разбор ответа
// ---------------------------------------------------------------------------

// Достаёт вопросы из текста, который вернул ИИ. Некорректные вопросы пропускает.
inline std::vector<Question> ParseQuestionsFromText(const std::string& text) {
    std::vector<Question> result;

    // ИИ мог добавить пояснение или ```json - берём только то, что между [ и ].
    const std::size_t first = text.find('[');
    const std::size_t last = text.rfind(']');
    if (first == std::string::npos || last == std::string::npos || last < first) return result;

    Json root;
    if (!ParseJson(text.substr(first, last - first + 1), root) || root.type != Json::Type::Array) {
        return result;
    }

    for (const Json& item : root.items) {
        const Json* q = item.Find("q");
        const Json* a = item.Find("a");
        const Json* c = item.Find("c");
        if (!q || !a || !c) continue;
        if (q->type != Json::Type::String || a->type != Json::Type::Array ||
            c->type != Json::Type::Number) {
            continue;
        }
        if (a->items.size() != 4) continue;

        Question question;
        question.text = TrimSpaces(q->text);
        question.correct = static_cast<int>(c->number);
        bool valid = !question.text.empty() && question.text.size() <= 480 &&
                     question.correct >= 0 && question.correct <= 3;

        for (std::size_t i = 0; valid && i < 4; ++i) {
            const Json& answer = a->items[i];
            if (answer.type != Json::Type::String) {
                valid = false;
                break;
            }
            question.answers[i] = TrimSpaces(answer.text);
            if (question.answers[i].empty() || question.answers[i].size() > 200) valid = false;
        }
        // Все четыре ответа должны отличаться друг от друга.
        for (std::size_t i = 0; valid && i < 4; ++i) {
            for (std::size_t j = i + 1; j < 4; ++j) {
                if (question.answers[i] == question.answers[j]) valid = false;
            }
        }
        if (valid) result.push_back(std::move(question));
    }
    return result;
}

// Достаёт сообщение об ошибке из ответа сервера: {"error":{"message":"..."}}.
inline std::string ExtractApiError(const std::string& body) {
    Json root;
    if (ParseJson(body, root)) {
        if (const Json* error = root.Find("error")) {
            if (const Json* message = error->Find("message")) {
                if (message->type == Json::Type::String) return TruncateUtf8(message->text, 160);
            }
        }
    }
    return "";
}

// ---------------------------------------------------------------------------
// Сетевой запрос (реализация - в ai.cpp)
// ---------------------------------------------------------------------------

// Включает и выключает сетевую библиотеку. Вызываются один раз в main().
void AiGlobalInit();
void AiGlobalCleanup();

// Просит ИИ придумать вопросы. БЛОКИРУЕТ поток до ответа, поэтому вызывается
// только из фонового потока. Аргументы передаются по значению (копии),
// чтобы поток не зависел от данных главного потока.
AiResult RequestQuestions(std::string apiKey, int count, std::vector<std::string> topics,
                          std::string difficulty, std::vector<std::string> avoid);
