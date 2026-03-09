---
name: weather
description: Get current weather and forecasts for any location (no API key required). Use when the user asks about the weather, temperature, forecast, or climate conditions in a city or region.
always: false
---

# Weather

Two free services, no API keys needed. Use the `http_client` tool for all requests.

## wttr.in (primary)

Quick one-liner:
```
http_client(method="GET", url="https://wttr.in/London?format=3")
```

Compact format:
```
http_client(method="GET", url="https://wttr.in/London?format=%l:+%c+%t+%h+%w")
```

Full forecast:
```
http_client(method="GET", url="https://wttr.in/London?T")
```

Format codes: `%c` condition, `%t` temp, `%h` humidity, `%w` wind, `%l` location, `%m` moon

Tips:
- URL-encode spaces: `wttr.in/New+York`
- Airport codes: `wttr.in/JFK`
- Units: `?m` (metric) `?u` (USCS)
- Today only: `?1` / Current only: `?0`

## Open-Meteo (JSON, for programmatic use)

Free, no key, returns structured JSON:
```
http_client(method="GET", url="https://api.open-meteo.com/v1/forecast?latitude=51.5&longitude=-0.12&current_weather=true")
```

Find coordinates for a city, then query. Returns JSON with temp, windspeed, weathercode.

Docs: https://open-meteo.com/en/docs
