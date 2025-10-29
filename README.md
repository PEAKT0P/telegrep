# 🧠 Telegrep — Intelligent Log Monitor for Telegram

Telegrep is a lightweight C++ daemon for Gentoo Linux (OpenRC compatible) that watches system logs in real time and sends important events to a Telegram chat with smart formatting and emojis.

It can detect:

* 🧠 Command history (from patched Bash loggers such as **loggerbash**, **sudolog**, or similar)
* 🔐 SSH logins (accepted/failed)
* 🐳 Docker events
* ⚙️ Kernel warnings and errors
* 🚨 System errors, panics, OOMs, and crashes

---

## 🧩 Features

* Written in pure C++11 — no Python or systemd dependencies
* Fully compatible with Gentoo/OpenRC
* Sends logs to Telegram bots with rich HTML formatting
* Detects user commands, SSH activity, and kernel messages
* Supports Bash loggers (like **loggerbash** or Ubuntu’s auditd/bash-logger integration) for command tracking
* Configurable log patterns and exceptions
* Secure configuration file (chmod 600)

---

## 🛠️ Installation

```bash
git clone https://github.com/PEAKT0P/telegrep.git
cd telegrep
make
sudo make install
```

### Optional: static build

```bash
make static
```

---

### Configuration

Edit `/opt/telegrep/settings.conf`:

```
token="YOUR_TELEGRAM_BOT_TOKEN"
chat_id="YOUR_CHAT_ID"
pattern="HISTORY:|kernel:|ssh|sshd|docker|error|fail|warn|panic|oom|segfault"
exceptions="systemd|dbus|avahi|NetworkManager|cups|pulseaudio"
```

Then secure it:

```bash
chmod 600 /opt/telegrep/settings.conf
chown root:root /opt/telegrep/settings.conf
```

---

## 🚀 Usage (Gentoo / OpenRC)

```bash
rc-service telegrep start
rc-service telegrep status
rc-service telegrep reload
rc-service telegrep stop
```

Test:

```bash
logger "HISTORY: PID=9999 UID=0 echo test"
```

---

## ⚡ Common Issues

### `/var/run: Too many levels of symbolic links`

This means `/var/run` is incorrectly symlinked.
It should point to `/run`, which must be a real tmpfs directory.

Check:

```bash
ls -ld /var/run
```

If it loops (`/var/run -> /run -> /var/run`), fix it:

```bash
rm -f /var/run
ln -s /run /var/run
```

Restart service:

```bash
rc-service telegrep restart
```

---

## 🧰 Debug mode

Run in foreground:

```bash
/opt/telegrep/telegrep -f
```

Watch logs:

```bash
tail -f /var/log/messages | grep telegrep
```

---

## 🧠 Author & Credits

👨‍💻 Author: **Denjik © 2025**
🌐 Website: [www.denjik.ru](https://www.denjik.ru)
🤖 Assistant: ChatGPT (GPT-5)
💾 Repository: [github.com/PEAKT0P](https://github.com/PEAKT0P)

---

# 🇷🇺 Telegrep — Умный мониторинг логов в Telegram

Telegrep — лёгкий демон на C++ для Gentoo (OpenRC), который отслеживает системные логи и отправляет важные события в Telegram.

Он определяет:

* 🧠 команды пользователей (только если **bash собран с поддержкой логгирования**, например **loggerbash**, **sudolog** или аналоги в Ubuntu)
* 🔐 SSH-входы
* 🐳 события Docker
* ⚙️ предупреждения ядра
* 🚨 ошибки, OOM и паники

---

## ⚙️ Установка

```bash
git clone https://github.com/PEAKT0P/telegrep.git
cd telegrep
make
sudo make install
```

Настрой конфиг `/opt/telegrep/settings.conf` и установи права:

```bash
chmod 600 /opt/telegrep/settings.conf
chown root:root /opt/telegrep/settings.conf
```

---

## 🚀 Использование (Gentoo / OpenRC)

```bash
rc-service telegrep start
rc-service telegrep status
rc-service telegrep reload
rc-service telegrep stop
```

---

## 💡 Примечание о HISTORY

Если твой Bash **собран с патчем loggerbash** (или аналогичным), все команды пользователей будут писаться в системный лог вида:

```
HISTORY: PID=1234 UID=1000 USER=varenik CMD=emerge -avuDN @world
```

Именно эти события Telegrep перехватывает и отправляет в Telegram.
Если патча нет — можно добавить в `~/.bashrc` строку:

```bash
trap 'logger "HISTORY: PID=$$ UID=$UID CMD=$(history 1 | sed \"s/^[ ]*[0-9]\+[ ]*//\")"' DEBUG
```

---

## 🧩 Советы

Проверь `/var/run`:

```bash
ls -ld /var/run
```

Должно быть:

```
lrwxrwxrwx 1 root root 4 Oct 29 20:00 /var/run -> /run
```

Если петля — исправь:

```bash
rm -f /var/run
ln -s /run /var/run
```

Тест:

```bash
logger "HISTORY: PID=1234 UID=0 emerge -avuDN @world"
```

---

## 🧑‍💻 Автор

**Denjik © 2025**
GitHub: [PEAKT0P](https://github.com/PEAKT0P)
Website: [www.denjik.ru](https://www.denjik.ru)
Powered by ChatGPT (GPT-5)

---

## 📜 License

Licensed under **GPL-2.0** — see `LICENSE` for details.

---
