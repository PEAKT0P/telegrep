# Makefile для telegrep
# Компилятор и флаги оптимизации

CXX = g++
CXXFLAGS = -std=c++11 -O2 -Wall -Wextra -pthread
LDFLAGS = -lcurl -static-libstdc++

# Цели
TARGET = telegrep
SOURCE = telegrep.cpp

# По умолчанию собираем динамически
all: $(TARGET)

# Сборка с динамической линковкой
$(TARGET): $(SOURCE)
        @echo "Компиляция telegrep с динамической линковкой..."
        $(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCE) $(LDFLAGS)
        @echo "Готово! Бинарник: ./$(TARGET)"
        @ls -lh $(TARGET)

# Сборка со статической линковкой libcurl (опционально)
static: $(SOURCE)
        @echo "Компиляция telegrep со статической линковкой..."
        $(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCE) -lcurl -lssl -lcrypto -lz -lpthread -static-libstdc++ -static-libgcc
        @echo "Готово! Статический бинарник: ./$(TARGET)"
        @ls -lh $(TARGET)

# Установка в систему
install: $(TARGET)
        @echo "Установка telegrep..."
        install -D -m 755 $(TARGET) /usr/local/sbin/$(TARGET)
        install -d /opt/telegrep
        @if [ ! -f /opt/telegrep/settings.conf ]; then \
                echo "Создаём пример конфига..."; \
                echo 'token="YOUR_BOT_TOKEN_HERE"' > /opt/telegrep/settings.conf; \
                echo 'chat_id="YOUR_CHAT_ID_HERE"' >> /opt/telegrep/settings.conf; \
                echo 'pattern="HISTORY:|kernel:|docker|error|fail"' >> /opt/telegrep/settings.conf; \
                echo 'exceptions=""' >> /opt/telegrep/settings.conf; \
                chmod 600 /opt/telegrep/settings.conf; \
                echo "Конфиг создан: /opt/telegrep/settings.conf"; \
        fi
        @echo "Установка завершена!"
        @echo "Не забудьте:"
        @echo "1. Отредактировать /opt/telegrep/settings.conf"
        @echo "2. Создать init скрипт /etc/init.d/telegrep"

# Удаление из системы
uninstall:
        @echo "Удаление telegrep..."
        rm -f /usr/local/sbin/$(TARGET)
        rm -f /var/run/telegrep.pid
        @echo "Удалено! Конфиг /opt/telegrep/ сохранён"

# Очистка скомпилированных файлов
clean:
        @echo "Очистка..."
        rm -f $(TARGET)
        @echo "Готово!"

# Проверка зависимостей
check-deps:
        @echo "Проверка зависимостей..."
        @which $(CXX) > /dev/null || (echo "ОШИБКА: g++ не установлен!" && exit 1)
        @pkg-config --exists libcurl || (echo "ОШИБКА: libcurl не установлен!" && exit 1)
        @echo "✓ Все зависимости установлены"

# Тестовая сборка с отладочными символами
debug: CXXFLAGS += -g -DDEBUG
debug: $(TARGET)

# Справка
help:
        @echo "Использование:"
        @echo "  make          - Собрать telegrep (динамическая линковка)"
        @echo "  make static   - Собрать со статической линковкой"
        @echo "  make install  - Установить в систему"
        @echo "  make uninstall- Удалить из системы"
        @echo "  make clean    - Удалить скомпилированные файлы"
        @echo "  make check-deps - Проверить зависимости"
        @echo "  make debug    - Собрать с отладочными символами"

.PHONY: all static install uninstall clean check-deps debug help
