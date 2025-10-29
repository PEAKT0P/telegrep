/*
 * telegrep.cpp - Демон для мониторинга логов и отправки в Telegram
 * Версия: 1.0
 * Для Gentoo Linux с OpenRC
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <regex>
#include <ctime>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <syslog.h>
#include <curl/curl.h>
#include <map>

// Глобальные переменные для обработки сигналов
volatile sig_atomic_t g_running = 1;
volatile sig_atomic_t g_reload_config = 0;

// Структура конфигурации
struct Config {
    std::string token;
    std::string chat_id;
    std::string pattern;
    std::string exceptions;
    std::string log_file = "/var/log/messages";
};

// Структура для буферизации событий
struct EventBuffer {
    std::vector<std::string> events;
    time_t last_send_time;
    int event_count;
    time_t last_mass_warning_time;

    EventBuffer() : last_send_time(0), event_count(0), last_mass_warning_time(0) {}
};

// Callback для curl - сохраняет ответ в string
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// URL-кодирование для безопасной передачи в Telegram API
std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        // Не кодируем буквы, цифры и некоторые спецсимволы
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int((unsigned char)c);
            escaped << std::nouppercase;
        }
    }

    return escaped.str();
}

// Экранирование HTML символов для Telegram
std::string html_escape(const std::string& data) {
    std::string buffer;
    buffer.reserve(data.size() * 1.1); // Резервируем чуть больше места

    for (char c : data) {
        switch(c) {
            case '&':  buffer.append("&amp;");  break;
            case '<':  buffer.append("&lt;");   break;
            case '>':  buffer.append("&gt;");   break;
            default:   buffer.push_back(c);     break;
        }
    }

    return buffer;
}

// Валидация token (только буквы, цифры, длина ~45 символов)
bool validate_token(const std::string& token) {
    if (token.length() < 40 || token.length() > 50) {
        return false;
    }

    std::regex token_regex("^[0-9]+:[A-Za-z0-9_-]+$");
    return std::regex_match(token, token_regex);
}

// Валидация chat_id (только цифры, может начинаться с минуса)
bool validate_chat_id(const std::string& chat_id) {
    if (chat_id.empty() || chat_id.length() > 20) {
        return false;
    }

    std::regex chat_regex("^-?[0-9]+$");
    return std::regex_match(chat_id, chat_regex);
}

// Парсинг конфигурационного файла
bool parse_config(const std::string& config_path, Config& config) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        syslog(LOG_ERR, "Cannot open config file: %s", config_path.c_str());
        return false;
    }

    // Проверка прав доступа (должен быть 0600)
    struct stat st;
    if (stat(config_path.c_str(), &st) == 0) {
        if ((st.st_mode & 0777) != 0600) {
            syslog(LOG_WARNING, "Config file permissions should be 0600, current: %o",
                   st.st_mode & 0777);
        }
    }

    std::string line;
    std::regex config_regex("^\\s*(\\w+)\\s*=\\s*\"([^\"]*)\"\\s*$");

    while (std::getline(file, line)) {
        // Пропускаем комментарии и пустые строки
        if (line.empty() || line[0] == '#') continue;

        std::smatch match;
        if (std::regex_match(line, match, config_regex)) {
            std::string key = match[1].str();
            std::string value = match[2].str();

            if (key == "token") config.token = value;
            else if (key == "chat_id") config.chat_id = value;
            else if (key == "pattern") config.pattern = value;
            else if (key == "exceptions") config.exceptions = value;
            else if (key == "log_file") config.log_file = value;
        }
    }

    // Валидация обязательных параметров
    if (config.token.empty() || config.chat_id.empty() || config.pattern.empty()) {
        syslog(LOG_ERR, "Missing required config parameters");
        return false;
    }

    if (!validate_token(config.token)) {
        syslog(LOG_ERR, "Invalid token format");
        return false;
    }

    if (!validate_chat_id(config.chat_id)) {
        syslog(LOG_ERR, "Invalid chat_id format");
        return false;
    }

    return true;
}

// Отправка сообщения в Telegram
bool send_telegram_message(const Config& config, const std::string& message) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        syslog(LOG_ERR, "Failed to initialize curl");
        return false;
    }

    std::string response;
    std::string url = "https://api.telegram.org/bot" + config.token + "/sendMessage";
    std::string post_fields = "chat_id=" + config.chat_id +
                              "&text=" + url_encode(message) +
                              "&parse_mode=HTML";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        syslog(LOG_ERR, "Curl error: %s", curl_easy_strerror(res));
        return false;
    }

    if (http_code != 200) {
        syslog(LOG_ERR, "Telegram API error: HTTP %ld, Response: %s",
               http_code, response.c_str());
        return false;
    }

    return true;
}

// Извлечение компонентов из строки лога
struct LogComponents {
    std::string timestamp;
    std::string hostname;
    std::string rest;
};

LogComponents parse_log_line(const std::string& line) {
    LogComponents comp;

    // Regex для парсинга syslog формата: "Oct 29 01:09:44 hostname rest..."
    std::regex log_regex(R"(^([A-Za-z]{3}\s+\d{1,2}\s+\d{2}:\d{2}:\d{2})\s+(\S+)\s+(.*)$)");
    std::smatch match;

    if (std::regex_match(line, match, log_regex)) {
        comp.timestamp = match[1].str();
        comp.hostname = match[2].str();
        comp.rest = match[3].str();
    } else {
        // Если не смогли распарсить, возвращаем как есть
        comp.rest = line;
    }

    return comp;
}

// Форматирование строки лога с эмодзи и HTML
std::string format_log_line(const std::string& line) {
    std::string escaped_line = html_escape(line);
    LogComponents comp = parse_log_line(escaped_line);

    std::string emoji = "📋";
    std::string formatted;

    // HISTORY команды - самый частый случай
    if (comp.rest.find("HISTORY:") != std::string::npos) {
        emoji = "🧠";

        // Извлекаем UID, PID и команду
        std::regex history_regex(R"(HISTORY:\s*PID=(\d+)\s+UID=(\d+)\s+(.+))");
        std::smatch match;

        if (std::regex_search(comp.rest, match, history_regex)) {
            std::string pid = match[1].str();
            std::string uid = match[2].str();
            std::string cmd = match[3].str();

            // Специфичные эмодзи для команд
            if (std::regex_search(cmd, std::regex("^(ssh|scp|sftp)"))) emoji = "🔐";
            else if (std::regex_search(cmd, std::regex("^su(\\s|$)"))) emoji = "👤";
            else if (std::regex_search(cmd, std::regex("^docker"))) emoji = "🐳";
            else if (std::regex_search(cmd, std::regex("^(systemctl|rc-service|rc-update)"))) emoji = "🧩";
            else if (std::regex_search(cmd, std::regex("^(rm|rmdir)\\s+-.*r"))) emoji = "🗑️";
            else if (std::regex_search(cmd, std::regex("^(vim|nano|vi|cat|less|tail|head)"))) emoji = "📝";
            else if (std::regex_search(cmd, std::regex("^(cd|ls|pwd|find)"))) emoji = "📁";
            else if (std::regex_search(cmd, std::regex("^(apt|emerge|yum|dnf|pacman|eix)"))) emoji = "📦";
            else if (std::regex_search(cmd, std::regex("^(screen|tmux)"))) emoji = "🖥️";

            if (uid == "0") {
                formatted = emoji + " <b>" + comp.timestamp + "</b> <code>" + comp.hostname + "</code>\n"
                          "├ <b><u>ROOT</u></b> PID:<code>" + pid + "</code> UID:<code>" + uid + "</code>\n"
                          "└ <code>" + cmd + "</code>";
            } else {
                formatted = emoji + " <b>" + comp.timestamp + "</b> <code>" + comp.hostname + "</code>\n"
                          "├ 👤 User PID:<code>" + pid + "</code> UID:<code>" + uid + "</code>\n"
                          "└ <code>" + cmd + "</code>";
            }
        }
    }
    // Kernel события
    else if (comp.rest.find("kernel:") != std::string::npos) {
        emoji = "⚙️";
        std::string kernel_msg = comp.rest.substr(comp.rest.find("kernel:") + 8);

        if (std::regex_search(kernel_msg, std::regex("(error|fail|panic|oops|bug)",
            std::regex::icase))) {
            emoji = "🚨⚙️";
            kernel_msg = "<b>" + kernel_msg + "</b>";
        }
        else if (std::regex_search(kernel_msg, std::regex("(oom|out of memory)",
            std::regex::icase))) emoji = "💥";
        else if (std::regex_search(kernel_msg, std::regex("(eth|wlan|br-|veth|device|link)"))) emoji = "🌐";
        else if (std::regex_search(kernel_msg, std::regex("(disk|mount|filesystem)"))) emoji = "💾";

        formatted = emoji + " <b>" + comp.timestamp + "</b> <code>" + comp.hostname + "</code>\n"
                  "└ <i>" + kernel_msg + "</i>";
    }
    // SSH события
    else if (std::regex_search(comp.rest, std::regex("(sshd|ssh).*(accepted|failed)",
        std::regex::icase))) {
        emoji = std::regex_search(comp.rest, std::regex("accepted", std::regex::icase)) ?
                "✅🔐" : "❌🔐";
        formatted = emoji + " <b>" + comp.timestamp + "</b> <code>" + comp.hostname + "</code>\n"
                  "└ <u>" + comp.rest + "</u>";
    }
    // Docker события
    else if (std::regex_search(comp.rest, std::regex("(docker|container)", std::regex::icase))) {
        emoji = "🐳";
        formatted = emoji + " <b>" + comp.timestamp + "</b> <code>" + comp.hostname + "</code>\n"
                  "└ <i>" + comp.rest + "</i>";
    }
    // Ошибки
    else if (std::regex_search(comp.rest, std::regex("(error|erro|fail|failed|critical|alert|emergency)",
        std::regex::icase))) {
        emoji = "🚨";
        formatted = emoji + " <b>" + comp.timestamp + "</b> <code>" + comp.hostname + "</code>\n"
                  "└ <b><u>" + comp.rest + "</u></b>";
    }
    // Предупреждения
    else if (std::regex_search(comp.rest, std::regex("(warn|warning)", std::regex::icase))) {
        emoji = "⚠️";
        formatted = emoji + " <b>" + comp.timestamp + "</b> <code>" + comp.hostname + "</code>\n"
                  "└ <i>" + comp.rest + "</i>";
    }
    // Cron
    else if (std::regex_search(comp.rest, std::regex("(cron|CRON)"))) {
        emoji = "⏰";
        formatted = emoji + " <b>" + comp.timestamp + "</b> <code>" + comp.hostname + "</code>\n"
                  "└ " + comp.rest;
    }
    // По умолчанию
    else {
        formatted = emoji + " <b>" + comp.timestamp + "</b> <code>" + comp.hostname + "</code>\n"
                  "└ " + comp.rest;
    }

    return formatted;
}

// Обработка буфера и отправка сообщений
void process_buffer(EventBuffer& buffer, const Config& config) {
    time_t current_time = time(nullptr);

    // Проверяем, прошло ли 10 секунд
    if (current_time - buffer.last_send_time < 10) {
        return;
    }

    if (!buffer.events.empty()) {
        // Защита от спама: >50 событий = только предупреждение
        if (buffer.event_count >= 50) {
            // Отправляем предупреждение раз в 5 минут
            if (current_time - buffer.last_mass_warning_time >= 300) {
                std::string message = "🚨 <b>MASS WARNING</b>\n"
                                    "⚠️ Получено <u>" + std::to_string(buffer.event_count) +
                                    " событий</u> за последние 10 секунд\n"
                                    "🔍 Проверьте систему немедленно!";

                if (send_telegram_message(config, message)) {
                    buffer.last_mass_warning_time = current_time;
                    syslog(LOG_WARNING, "Sent mass warning: %d events", buffer.event_count);
                }
            }
        } else {
            // Формируем сообщение с событиями
            std::string message = "📊 <b>События за последние 10 сек:</b> " +
                                std::to_string(buffer.event_count) + "\n"
                                "━━━━━━━━━━━━━━━━━━\n\n";

            for (const auto& event : buffer.events) {
                message += event + "\n\n";
            }

            // Telegram лимит ~4096 символов
            if (message.length() > 3800) {
                message = message.substr(0, 3700) + "\n\n<i>... [сообщение обрезано]</i>";
            }

            if (send_telegram_message(config, message)) {
                syslog(LOG_INFO, "Sent message with %d events", buffer.event_count);
            }
        }
    }

    // Очищаем буфер
    buffer.events.clear();
    buffer.event_count = 0;
    buffer.last_send_time = current_time;
}

// Обработчик сигналов
void signal_handler(int signum) {
    if (signum == SIGTERM || signum == SIGINT) {
        g_running = 0;
        syslog(LOG_INFO, "Received shutdown signal");
    } else if (signum == SIGHUP) {
        g_reload_config = 1;
        syslog(LOG_INFO, "Received config reload signal");
    }
}

// Демонизация процесса
bool daemonize() {
    pid_t pid = fork();

    if (pid < 0) {
        return false;
    }

    if (pid > 0) {
        // Родительский процесс завершается
        exit(EXIT_SUCCESS);
    }

    // Создаём новую сессию
    if (setsid() < 0) {
        return false;
    }

    // Второй fork для отключения от терминала
    pid = fork();

    if (pid < 0) {
        return false;
    }

    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    // Меняем рабочую директорию
    chdir("/");

    // Закрываем стандартные дескрипторы
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Открываем /dev/null
    open("/dev/null", O_RDONLY); // stdin
    open("/dev/null", O_RDWR);   // stdout
    open("/dev/null", O_RDWR);   // stderr

    return true;
}

// Создание PID файла
bool create_pid_file(const std::string& pid_file) {
    std::ofstream file(pid_file);
    if (!file.is_open()) {
        return false;
    }

    file << getpid() << std::endl;
    return true;
}

// Основная функция мониторинга логов
void monitor_logs(const Config& config) {
    std::ifstream log_file(config.log_file);
    if (!log_file.is_open()) {
        syslog(LOG_ERR, "Cannot open log file: %s", config.log_file.c_str());
        return;
    }

    // Переходим в конец файла
    log_file.seekg(0, std::ios::end);

    EventBuffer buffer;
    std::regex pattern_regex(config.pattern);
    std::regex exceptions_regex;
    bool has_exceptions = !config.exceptions.empty();

    if (has_exceptions) {
        exceptions_regex = std::regex(config.exceptions);
    }

    while (g_running) {
        std::string line;

        if (std::getline(log_file, line)) {
            // Проверяем соответствие паттерну
            bool matched = std::regex_search(line, pattern_regex);

            // Проверяем исключения
            if (matched && has_exceptions) {
                if (std::regex_search(line, exceptions_regex)) {
                    matched = false;
                }
            }

            if (matched) {
                std::string formatted = format_log_line(line);
                buffer.events.push_back(formatted);
                buffer.event_count++;
            }
        } else {
            // Конец файла или ошибка
            log_file.clear(); // Сбрасываем флаги ошибок

            // Проверяем буфер
            process_buffer(buffer, config);

            // Перезагрузка конфига если нужно
            if (g_reload_config) {
                Config new_config;
                if (parse_config("/opt/telegrep/settings.conf", new_config)) {
                    // Нельзя изменить config внутри функции, но можно залогировать
                    syslog(LOG_INFO, "Config reload requested (restart needed for changes)");
                }
                g_reload_config = 0;
            }

            // Короткий сон чтобы не грузить CPU
            usleep(100000); // 0.1 секунды
        }
    }

    // Финальная отправка буфера перед завершением
    process_buffer(buffer, config);
}

int main(int argc, char* argv[]) {
    // Открываем syslog
    openlog("telegrep", LOG_PID | LOG_CONS, LOG_DAEMON);

    // Парсим конфигурацию
    Config config;
    if (!parse_config("/opt/telegrep/settings.conf", config)) {
        syslog(LOG_ERR, "Failed to parse config");
        closelog();
        return 1;
    }

    // Инициализируем curl глобально
    curl_global_init(CURL_GLOBAL_ALL);

    // Отправляем стартовое сообщение
    std::string startup_msg = "✅ <b>Telegrep started</b>\n"
                             "📡 Monitoring: <code>" + config.log_file + "</code>\n"
                             "🔍 Pattern: <code>" + config.pattern + "</code>";
    if (!config.exceptions.empty()) {
        startup_msg += "\n🚫 Exceptions: <code>" + config.exceptions + "</code>";
    }

    if (!send_telegram_message(config, startup_msg)) {
        syslog(LOG_ERR, "Cannot connect to Telegram. Check token and chat_id");
        curl_global_cleanup();
        closelog();
        return 1;
    }

    // Демонизация (если не запущено с -f для foreground)
    bool foreground = false;
    if (argc > 1 && std::string(argv[1]) == "-f") {
        foreground = true;
    }

    if (!foreground) {
        if (!daemonize()) {
            syslog(LOG_ERR, "Failed to daemonize");
            curl_global_cleanup();
            closelog();
            return 1;
        }
    }

    // Создаём PID файл
    if (!create_pid_file("/var/run/telegrep.pid")) {
        syslog(LOG_WARNING, "Cannot create PID file");
    }

    // Устанавливаем обработчики сигналов
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGHUP, signal_handler);

    syslog(LOG_INFO, "Telegrep daemon started");

    // Основной цикл мониторинга
    monitor_logs(config);

    // Отправляем сообщение о завершении
    send_telegram_message(config, "🛑 <b>Telegrep stopped</b>");

    // Очистка
    unlink("/var/run/telegrep.pid");
    curl_global_cleanup();
    syslog(LOG_INFO, "Telegrep daemon stopped");
    closelog();

    return 0;
}
