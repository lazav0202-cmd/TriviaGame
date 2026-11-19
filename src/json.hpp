// json.hpp - крошечный разборщик JSON (парсер).
//
// Ответ ИИ приходит в формате JSON, а в стандартной библиотеке C++ парсера JSON нет.
// Поэтому мы пишем свой маленький: он понимает объекты, массивы, строки, числа,
// true/false/null и escape-последовательности вроде \n и Ж.

#pragma once

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

// Одно JSON-значение. Тип хранится в type, а данные - в соответствующем поле.
struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;               // для строки
    std::vector<Json> items;        // для массива: элементы
    std::vector<std::string> keys;  // для объекта: имена полей...
    std::vector<Json> values;       // ...и их значения (values[i] принадлежит keys[i])

    // Ищет поле объекта по имени. Возвращает nullptr, если такого поля нет.
    const Json* Find(const std::string& key) const {
        if (type != Type::Object) return nullptr;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == key) return &values[i];
        }
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& source) : src_(source) {}

    // Разбирает весь текст. Возвращает true, если это корректный JSON.
    bool Parse(Json& out) {
        pos_ = 0;
        SkipSpaces();
        if (!ParseValue(out, 0)) return false;
        SkipSpaces();
        return pos_ == src_.size();  // после значения не должно быть мусора
    }

private:
    const std::string& src_;
    std::size_t pos_ = 0;

    static constexpr int MAX_DEPTH = 64;  // защита от бесконечной вложенности

    void SkipSpaces() {
        while (pos_ < src_.size() &&
               (src_[pos_] == ' ' || src_[pos_] == '\n' || src_[pos_] == '\r' || src_[pos_] == '\t')) {
            ++pos_;
        }
    }

    bool Match(const char* literal) {
        std::size_t n = 0;
        while (literal[n] != '\0') ++n;
        if (src_.compare(pos_, n, literal) == 0) {
            pos_ += n;
            return true;
        }
        return false;
    }

    bool ParseValue(Json& out, int depth) {
        if (depth > MAX_DEPTH || pos_ >= src_.size()) return false;
        const char c = src_[pos_];

        if (c == '{') return ParseObject(out, depth);
        if (c == '[') return ParseArray(out, depth);
        if (c == '"') {
            out.type = Json::Type::String;
            return ParseString(out.text);
        }
        if (c == 't') {
            out.type = Json::Type::Bool;
            out.boolean = true;
            return Match("true");
        }
        if (c == 'f') {
            out.type = Json::Type::Bool;
            out.boolean = false;
            return Match("false");
        }
        if (c == 'n') {
            out.type = Json::Type::Null;
            return Match("null");
        }
        return ParseNumber(out);
    }

    bool ParseNumber(Json& out) {
        const std::size_t start = pos_;
        while (pos_ < src_.size()) {
            const char c = src_[pos_];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
                ++pos_;
            } else {
                break;
            }
        }
        if (pos_ == start) return false;
        const std::string number = src_.substr(start, pos_ - start);
        char* end = nullptr;
        out.number = std::strtod(number.c_str(), &end);
        out.type = Json::Type::Number;
        return end != nullptr && *end == '\0';
    }

    // Читает 4 шестнадцатеричные цифры после \u.
    bool ParseHex4(unsigned& value) {
        if (pos_ + 4 > src_.size()) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = src_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    // Записывает кодовую точку Unicode в строку в кодировке UTF-8.
    static void AppendUtf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool ParseString(std::string& out) {
        out.clear();
        ++pos_;  // пропускаем открывающую кавычку
        while (pos_ < src_.size()) {
            const char c = src_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= src_.size()) return false;
            const char esc = src_[pos_++];
            switch (esc) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    unsigned code = 0;
                    if (!ParseHex4(code)) return false;
                    // Символы вне основной таблицы записываются парой 😀.
                    if (code >= 0xD800 && code <= 0xDBFF && pos_ + 1 < src_.size() &&
                        src_[pos_] == '\\' && src_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        unsigned low = 0;
                        if (!ParseHex4(low)) return false;
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(out, code);
                    break;
                }
                default:
                    return false;
            }
        }
        return false;  // строка не закрыта
    }

    bool ParseArray(Json& out, int depth) {
        out.type = Json::Type::Array;
        ++pos_;  // пропускаем [
        SkipSpaces();
        if (pos_ < src_.size() && src_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            Json item;
            SkipSpaces();
            if (!ParseValue(item, depth + 1)) return false;
            out.items.push_back(std::move(item));
            SkipSpaces();
            if (pos_ >= src_.size()) return false;
            if (src_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (src_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return false;
        }
    }

    bool ParseObject(Json& out, int depth) {
        out.type = Json::Type::Object;
        ++pos_;  // пропускаем {
        SkipSpaces();
        if (pos_ < src_.size() && src_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            SkipSpaces();
            if (pos_ >= src_.size() || src_[pos_] != '"') return false;
            std::string key;
            if (!ParseString(key)) return false;
            SkipSpaces();
            if (pos_ >= src_.size() || src_[pos_] != ':') return false;
            ++pos_;
            SkipSpaces();
            Json value;
            if (!ParseValue(value, depth + 1)) return false;
            out.keys.push_back(std::move(key));
            out.values.push_back(std::move(value));
            SkipSpaces();
            if (pos_ >= src_.size()) return false;
            if (src_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (src_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return false;
        }
    }
};

// Удобная обёртка: разобрать строку и вернуть true при успехе.
inline bool ParseJson(const std::string& text, Json& out) {
    JsonParser parser(text);
    return parser.Parse(out);
}
