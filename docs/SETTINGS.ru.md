[🇬🇧 English](SETTINGS.md) | [🇷🇺 Русский](SETTINGS.ru.md)

# MTBolt: справочник по конфигурации

## Обзор

MTBolt поддерживает три уровня конфигурации с приоритетом (от высшего к низшему):

1. **Флаги CLI** — переопределяют всё остальное
2. **TOML-файл конфигурации** — указывается через `--config <путь>`
3. **Скомпилированные значения по умолчанию** — применяются при запуске, если ничего другого не задано

TOML-файл загружается один раз при запуске и перечитывается по сигналу `SIGHUP` (только секции с секретами и ACL). Путь к файлу передаётся флагом `--config /path/to/mtbolt.toml`. Аннотированный пример со всеми параметрами и их значениями по умолчанию находится в `mtbolt.toml.example` в корне проекта.

---

## Справочник параметров

### Сеть (network)

| Ключ TOML | Флаг CLI | Тип | По умолчанию | Описание |
|---|---|---|---|---|
| `network.max_connections` | `--max-connections` | int | `10485760` | Максимальное число одновременных клиентских соединений |
| `network.max_targets` | `--max-targets` | int | `10485760` | Максимальное число соединений с upstream-серверами |
| `network.backlog` | `--backlog` | int | `131072` | Размер очереди `listen()` |
| `network.tcp_defer_accept` | `--tcp-defer-accept` | int | `3` | Время ожидания первых данных после TCP-рукопожатия (секунды); `0` отключает `TCP_DEFER_ACCEPT` |
| `network.window_clamp` | `-W` / `--window-clamp` | int | `131072` | Ограничение TCP-окна приёма для клиентских соединений (байты) |

> Примечание: `--max-special-connections` (`-C`) — устаревший псевдоним, который применяется к тому же полю `max_connections`.

### Буферы (buffers)

| Ключ TOML | Флаг CLI | Тип | По умолчанию | Описание |
|---|---|---|---|---|
| `buffers.max_allocated_bytes` | `--max-allocated-bytes` | int | `17179869184` (16 ГБ) | Максимальный объём памяти для пула буферов сообщений |
| `buffers.max_connection_buffer` | `--max-connection-buffer` | int | `33554432` (32 МБ) | Лимит буфера на одно соединение |
| `buffers.std_buffer` | *(только compile-time)* | int | `2048` | Размер стандартного буфера — информационный, нельзя изменить во время выполнения |
| `buffers.small_buffer` | *(только compile-time)* | int | `512` | Размер малого буфера — информационный, нельзя изменить во время выполнения |
| `buffers.tiny_buffer` | *(только compile-time)* | int | `48` | Размер крошечного буфера — информационный, нельзя изменить во время выполнения |
| `buffers.chunk_size` | *(только compile-time)* | int | `2097088` (2 МБ − 64) | Размер чанка при выделении буферов — информационный, нельзя изменить во время выполнения |

> `std_buffer`, `small_buffer`, `tiny_buffer` и `chunk_size` фиксируются на этапе компиляции. TOML-парсер принимает эти ключи и сохраняет значения, однако на поведение во время выполнения они не влияют. Приведены исключительно для справки.

### Тайм-ауты (timeouts)

| Ключ TOML | Флаг CLI | Тип | По умолчанию | Описание |
|---|---|---|---|---|
| `timeouts.handshake` | `--handshake-timeout` | float | `5.0` | Тайм-аут MTProto-рукопожатия (секунды) |
| `timeouts.rpc` | `--rpc-timeout` | float | `5.0` | Тайм-аут ответа на RPC-запрос (секунды) |
| `timeouts.ping_interval` | `-T` / `--ping-interval` | float | `5.0` | Интервал keepalive-пингов для локальных TCP-соединений (секунды) |
| `timeouts.connect` | `--connect-timeout` | float | `3.0` | Тайм-аут подключения к upstream DC (секунды) |
| `timeouts.http_max_wait` | `--http-max-wait` | float | `960.0` | Максимальное время ожидания для HTTP long-poll запросов (секунды) |

### GeoIP

| Ключ TOML | Флаг CLI | Тип | По умолчанию | Описание |
|---|---|---|---|---|
| `geoip.enabled` | *(только TOML)* | bool | `true` | Включить отслеживание страны/региона по IP |
| `geoip.database` | `--geoip-db` | string | `""` | Путь к базе MaxMind `GeoLite2-City.mmdb` или `GeoLite2-Country.mmdb` |
| `geoip.ht_size` | `--geoip-ht-size` | int | `4194304` | Количество слотов хеш-таблицы для отслеживания IP; должно быть степенью двойки и не менее 1024 |
| `geoip.max_regions` | `--geoip-max-regions` | int | `128` | Максимальное число отслеживаемых субрегионов страны (1–1024) |

### Воркеры (workers)

| Ключ TOML | Флаг CLI | Тип | По умолчанию | Описание |
|---|---|---|---|---|
| `workers.count` | `-M` / `--slaves` | int | `0` | Количество дочерних рабочих процессов; `0` — однопроцессный режим |

> TLS-транспорт (`--domain`) не рекомендуется использовать с несколькими воркерами, поскольку защита от replay-атак ослабляется при отсутствии общего состояния между процессами.

### Статистика (stats)

| Ключ TOML | Флаг CLI | Тип | По умолчанию | Описание |
|---|---|---|---|---|
| `stats.http_enabled` | `--http-stats` | bool | `false` | Включить HTTP-эндпоинт статистики |
| `stats.buffer_size` | `--stats-buffer-size` | int | `1048576` (1 МБ) | Размер буфера ответа для запросов статистики (байты) |

---

## Примеры конфигураций

### Минимальная

```toml
# Минимальная конфигурация — для relay-режима обязательны только секреты.
# Все параметры MTBolt используют значения по умолчанию.
```

### Продакшн (высокая нагрузка)

```toml
[network]
max_connections = 10485760
max_targets = 10485760
backlog = 131072
tcp_defer_accept = 3
window_clamp = 131072

[buffers]
max_allocated_bytes = 17179869184  # 16 ГБ
max_connection_buffer = 33554432   # 32 МБ

[timeouts]
handshake = 5.0
rpc = 5.0
ping_interval = 5.0
connect = 3.0
http_max_wait = 960.0

[geoip]
enabled = true
database = "/etc/geoip/GeoLite2-City.mmdb"
ht_size = 4194304
max_regions = 128

[workers]
count = 4

[stats]
http_enabled = true
buffer_size = 4194304  # 4 МБ для крупных инсталляций
```

### Экономичный VPS (2 ГБ ОЗУ)

```toml
[network]
max_connections = 65536
max_targets = 65536
backlog = 4096
tcp_defer_accept = 3

[buffers]
max_allocated_bytes = 1073741824   # 1 ГБ
max_connection_buffer = 4194304    # 4 МБ

[geoip]
enabled = false

[workers]
count = 0

[stats]
http_enabled = false
```

---

## Правила валидации

| Параметр | Правило |
|---|---|
| `max_connections` | Должно быть ≥ 1 |
| `max_targets` | Должно быть ≥ 1 |
| `backlog` | Должно быть ≥ 1 |
| `tcp_defer_accept` | Должно быть ≥ 0 |
| `max_allocated_bytes` | Должно быть ≥ 1 МБ (1048576) |
| `handshake` | Должно быть > 0 |
| `rpc` | Должно быть > 0 |
| `ping_interval` | Должно быть > 0 |
| `connect` | Должно быть > 0 |
| `geoip.ht_size` | Должно быть ≥ 1024 и быть степенью двойки |
| `geoip.max_regions` | Должно быть в диапазоне 1–1024 |
| `stats.buffer_size` | Должно быть ≥ 4096 |
