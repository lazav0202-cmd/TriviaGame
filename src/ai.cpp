// ai.cpp - сетевая часть: запрос к Claude API через libcurl.
//
// Этот файл НЕ подключает raylib.h (см. пояснение в ai.hpp).
// Сборка: его нужно добавить в команду g++ рядом с main.cpp.

#include <curl/curl.h>

#include <string>
#include <vector>

#include "ai.hpp"

void AiGlobalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }
void AiGlobalCleanup() { curl_global_cleanup(); }

// ---------------------------------------------------------------------------
// Сетевой запрос
// ---------------------------------------------------------------------------

// libcurl вызывает эту функцию, когда приходит очередная порция ответа.
static std::size_t CurlWrite(char* data, std::size_t size, std::size_t count, void* userdata) {
    static_cast<std::string*>(userdata)->append(data, size * count);
    return size * count;
}

// libcurl вызывает её регулярно во время запроса; ненулевой результат прерывает запрос.
static int CurlProgress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return g_aiCancel.load() ? 1 : 0;
}

// Описание функции - в ai.hpp.
AiResult RequestQuestions(std::string apiKey, int count, std::vector<std::string> topics,
                                 std::string difficulty, std::vector<std::string> avoid) {
    AiResult result;

    const std::string prompt = BuildPrompt(count, topics, difficulty, avoid);
    const std::string body = std::string("{\"model\":\"") + AI_MODEL +
                             "\",\"max_tokens\":4096,\"messages\":[{\"role\":\"user\",\"content\":\"" +
                             JsonEscape(prompt) + "\"}]}";

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "Не удалось запустить сетевую библиотеку.";
        return result;
    }

    std::string response;
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("x-api-key: " + apiKey).c_str());
    headers = curl_slist_append(headers, (std::string("anthropic-version: ") + AI_VERSION).c_str());
    headers = curl_slist_append(headers, "content-type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, AI_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgress);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
#ifdef CURLSSLOPT_NATIVE_CA
    // Использовать сертификаты Windows, чтобы HTTPS работал и вне окна MSYS2.
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_NATIVE_CA));
#endif

    const CURLcode code = curl_easy_perform(curl);
    long httpStatus = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        result.error = std::string("Нет связи с сервером ИИ: ") + curl_easy_strerror(code);
        return result;
    }

    if (httpStatus != 200) {
        const std::string details = ExtractApiError(response);
        switch (httpStatus) {
            case 401: result.error = "Неверный API-ключ (ошибка 401). Проверь ключ."; break;
            case 403: result.error = "У ключа нет доступа к API (ошибка 403)."; break;
            case 404: result.error = "Модель не найдена (ошибка 404). Измени AI_MODEL в ai.hpp."; break;
            case 429: result.error = "Слишком много запросов или закончился лимит (ошибка 429)."; break;
            default:
                result.error = "Ошибка сервера ИИ (код " + std::to_string(httpStatus) + ").";
        }
        if (!details.empty()) result.error += " " + details;
        return result;
    }

    // Ответ API: {"content":[{"type":"text","text":"..."}], ...}. Склеиваем все текстовые блоки.
    Json root;
    if (!ParseJson(response, root)) {
        result.error = "ИИ прислал ответ в неожиданном формате.";
        return result;
    }
    std::string text;
    if (const Json* content = root.Find("content")) {
        for (const Json& block : content->items) {
            const Json* type = block.Find("type");
            const Json* part = block.Find("text");
            if (type && part && type->text == "text") text += part->text;
        }
    }

    result.questions = ParseQuestionsFromText(text);
    if (result.questions.empty()) {
        result.error = "ИИ вернул вопросы в неподходящем виде. Попробуй ещё раз.";
        return result;
    }
    result.ok = true;
    return result;
}
