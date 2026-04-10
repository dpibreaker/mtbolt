[🇬🇧 English](README.md) | [🇷🇺 Русский](README.ru.md)

# MTBolt

Высокопроизводительный MTProto-прокси для Telegram с нативными GeoIP-метриками, runtime-конфигурацией и усиленной безопасностью.

[![License: GPL v2](https://img.shields.io/badge/License-GPLv2-blue.svg)](LICENSE)

## Возможности

- **TCP_DEFER_ACCEPT** — отбрасывает нулевые соединения на уровне ядра, нулевые накладные расходы
- **Динамическая таблица соединений** — масштабируется до 10M+ одновременных соединений
- **Интеграция jemalloc** — настроенный аллокатор памяти с фоновым GC-потоком
- **Нативные метрики Prometheus** — уникальные IP по странам, разбивка по регионам России, счётчики online/total
- **Мультипроцессный режим** — статистика IP через разделяемую память между воркерами
- **Runtime-конфигурация** — TOML-файл конфигурации + переопределение через CLI, без перекомпиляции
- **Усиление сборки** — stack protector, FORTIFY_SOURCE, PIE, full RELRO

## Быстрый старт

```bash
# Зависимости Debian/Ubuntu
apt install libssl-dev libjemalloc-dev libmaxminddb-dev

# Сборка
make

# Запуск (режим direct)
./objs/bin/mtbolt --direct -S <hex-secret> --http-stats --geoip-db /path/to/GeoLite2-City.mmdb -M 4 -p 443

# Запуск с TOML-конфигом
./objs/bin/mtbolt --config mtbolt.toml --direct
```

## Конфигурация

Все настраиваемые параметры можно задать через TOML-файл конфигурации и/или флаги CLI.
Полный список параметров — в [справочнике по настройкам](docs/SETTINGS.md).

```toml
[workers]
count = 4

[geoip]
database = "/etc/geoip/GeoLite2-City.mmdb"

[stats]
http_enabled = true
```

## Сборка

| Библиотека | Обязательна | Назначение |
|------------|-------------|------------|
| OpenSSL | Да | TLS, AES-криптография |
| jemalloc | Нет | Аллокатор памяти (рекомендуется) |
| libmaxminddb | Нет | GeoIP-метрики по странам и регионам |

```bash
make              # автоматически определяет опциональные библиотеки
make clean        # очистка артефактов сборки
make lint         # статический анализ через cppcheck
```

## Развёртывание

### systemd

```ini
[Unit]
Description=MTBolt MTProto Proxy
After=network.target

[Service]
ExecStart=/opt/mtbolt/mtbolt --config /etc/mtbolt/mtbolt.toml --direct
User=teleproxy
LimitNOFILE=10485760
TasksMax=infinity
Restart=always
RestartSec=5
Environment=MALLOC_CONF=background_thread:true,dirty_decay_ms:5000,muzzy_decay_ms:30000

[Install]
WantedBy=multi-user.target
```

### Рекомендуемые параметры sysctl

```bash
net.core.somaxconn = 524288
net.ipv4.tcp_congestion_control = bbr
net.ipv4.tcp_fin_timeout = 10
net.ipv4.tcp_fastopen = 3
fs.file-max = 10485760
fs.nr_open = 10485760
```

## Метрики

Метрики Prometheus на порту статистики при включённом `--http-stats`:

| Метрика | Описание |
|---------|----------|
| `teleproxy_country_unique_ips` | Уникальные IP по странам (с координатами) |
| `teleproxy_ru_region_unique_ips` | Уникальные IP по регионам России (ISO 3166-2:RU) |
| `teleproxy_online_ips` | Уникальные IP, подключённые в данный момент |
| `teleproxy_transport_total_ips{transport="dd\|ee"}` | Уникальные IP за всё время по transport DD/EE |
| `teleproxy_unique_ips` | Всего уникальных IP за всё время работы |

## Безопасность

- Флаги компиляции: `-fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -Wl,-z,relro,-z,now`
- Атомарные счётчики ссылок, исключающие гонки при работе с соединениями
- Очистка ключевого материала через OpenSSL (`OPENSSL_cleanse`)
- Константное по времени сравнение секрета (`CRYPTO_memcmp`)
- Настраиваемый таймаут рукопожатия (по умолчанию 5 с)
- `TCP_DEFER_ACCEPT` отбрасывает простаивающие TCP-соединения на уровне ядра

## Лицензия

[GNU General Public License v2.0](LICENSE)

## Благодарности

Форк [teleproxy](https://github.com/teleproxy/teleproxy) с улучшениями производительности, безопасности и наблюдаемости.
