---
name: browser
description: Full browser automation via Chrome DevTools Protocol. Navigate pages, interact with UI elements, take screenshots, extract content, manage tabs, emulate devices, handle cookies/storage, monitor network, and control browser state.
platform: [linux, macos, windows]
requires:
  bins:
    - google-chrome
---

# Browser

Control Chrome/Brave/Edge/Chromium browser via Chrome DevTools Protocol (CDP).

## CRITICAL: How to Use This Tool

**Use `navigate` to go to a URL.** It auto-starts the browser and uses the current tab.

- Do NOT use `open` to visit a URL — it creates a NEW tab every time, leaving old tabs open.
- Do NOT call `start` before `navigate` — the browser starts automatically.
- After navigating, use `snapshot` to read page content (NOT screenshot).
- Use `screenshot` only when you need a visual image, not to read text.
- Use `inspect` or `snapshot format=accessibility` to find CSS selectors before clicking/typing.

## Actions Reference

### Navigation (use these to move between pages)

| Action | Description |
|--------|-------------|
| `navigate` | **Primary action.** Go to a URL. Auto-starts browser. Uses current tab. Params: `url` (required) |
| `back` | Go back in browser history (like browser back button) |
| `forward` | Go forward in browser history |
| `reload` | Reload the current page |
| `scroll` | Scroll the page. Params: `direction` (up/down/left/right, default: down), `amount` (pixels, default: viewport height) |
| `wait` | Wait for conditions. Params: `seconds` (1-60), `text`, `wait_selector`, `wait_url`, `wait_fn` |

### Reading Page Content

| Action | Description |
|--------|-------------|
| `snapshot` | **Use this to read page content.** Params: `format` (text/accessibility), `max_chars`, `interactive` |
| `screenshot` | Capture visual image. Returns resized JPEG preview for visual analysis + saves full-res file. Params: `full_page`, `image_type`, `quality`, `selector`, `output_path` |
| `inspect` | List interactive elements with CSS selectors (up to 100 visible elements) |
| `pdf` | Render current page to PDF. Saves file and returns path. Params: `output_path` |

### Interacting with Elements

| Action | Description |
|--------|-------------|
| `click` | Click element. Params: `selector` (required), `double_click`, `button` (left/right/middle) |
| `type` | Type text. Params: `text` (required), `selector` (optional, clicks first), `submit` (Enter after), `slowly` |
| `press` | Press key. Params: `key` (Enter, Tab, Escape, Backspace, Delete, ArrowUp/Down/Left/Right, Home, End, PageUp/Down, Space, F1-F12) |
| `hover` | Hover over element. Params: `selector` |
| `select` | Select dropdown options. Params: `selector`, `values` (array) |
| `fill` | Batch form fill. Params: `fields` (array of {selector, value, type?}) |
| `drag` | Drag and drop. Params: `selector` (source), `end_selector` (target) |
| `scroll_into_view` | Scroll element into view. Params: `selector` |
| `resize` | Set viewport. Params: `width`, `height` |

### JavaScript

| Action | Description |
|--------|-------------|
| `evaluate` | Execute JavaScript. Params: `script` (required). Returns result |

### Network & Console Monitoring

| Action | Description |
|--------|-------------|
| `console` | Browser console messages. Params: `level` (log/warn/error), `clear` |
| `errors` | JavaScript errors. Params: `clear` |
| `requests` | Network requests. Params: `filter` (URL substring), `clear` |
| `response_body` | Get response body. Params: `filter` (URL substring, required), `max_chars` |

### Cookies & Storage

| Action | Description |
|--------|-------------|
| `cookies` | Get cookies. Params: `url` (optional filter) |
| `set_cookie` | Set cookie. Params: `name`, `value`, `domain`, `path`, `url` |
| `clear_cookies` | Clear all cookies |
| `get_storage` | Get localStorage/sessionStorage. Params: `kind` (local/session) |
| `set_storage` | Set storage entry. Params: `kind`, `name`, `value` |
| `clear_storage` | Clear storage. Params: `kind` |

### Environment Emulation

| Action | Description |
|--------|-------------|
| `set_offline` | Toggle offline mode. Params: `enabled` |
| `set_headers` | Set custom HTTP headers. Params: `headers` (object) |
| `set_credentials` | Set HTTP basic auth. Params: `username`, `password`, `clear` |
| `set_geolocation` | Override GPS. Params: `latitude`, `longitude`, `clear` |
| `set_media` | Color scheme. Params: `media` (dark/light/no-preference/none) |
| `set_timezone` | Override timezone. Params: `timezone` (IANA ID). Omit to reset to UTC |
| `set_locale` | Override locale. Params: `locale` (e.g. pt-BR) |
| `set_device` | Device emulation. Params: `device` (preset) or `width`+`height`, `clear` |

Device presets: iPhone 14, iPhone 14 Pro Max, iPhone SE, iPad, iPad Pro, Pixel 7, Samsung Galaxy S23, Desktop 1080p, Desktop 1440p.

### Tab Management (only for multi-tab scenarios)

| Action | Description |
|--------|-------------|
| `tabs` | List all open tabs |
| `open` | Open a NEW tab (only use when you need multiple tabs). Params: `url` |
| `focus` | Switch to a tab. Params: `target_id` |
| `close` | Close a tab. Params: `target_id` (optional, closes current) |

### Lifecycle

| Action | Description |
|--------|-------------|
| `status` | Check if browser is running |
| `start` | Launch browser (not needed before navigate) |
| `stop` | Shut down browser |

### Dialog & File

| Action | Description |
|--------|-------------|
| `dialog` | Handle JS dialog. Params: `accept`, `prompt_text` |
| `upload` | Set file input. Params: `selector`, `path` |

## Common Workflows

### Visit a page and read its content
```
navigate url="https://example.com"   → auto-starts browser, loads page
snapshot                              → read the page text content
```

### Fill a form and submit
```
navigate url="https://example.com/login"
inspect                               → find form field selectors
type selector="#email" text="user@example.com"
type selector="#password" text="pass123" submit=true
wait text="Welcome"                   → wait for success
snapshot                              → verify result
```

### Scroll through a long page
```
navigate url="https://example.com/article"
snapshot                              → read visible content
scroll direction=down                 → scroll one viewport down
snapshot                              → read more content
scroll direction=down amount=500      → scroll 500px down
```

### Go back/forward
```
navigate url="https://page1.com"
navigate url="https://page2.com"
back                                  → returns to page1.com
forward                               → returns to page2.com
reload                                → reloads page2.com
```

### Save screenshot to a specific path
```
navigate url="https://example.com"
screenshot output_path="/project/public/home.png"  → saves to specified path
screenshot                                          → saves to temp directory (default)
```

### Save PDF to a specific path
```
navigate url="https://example.com/report"
pdf output_path="/project/docs/report.pdf"  → saves to specified path
```

### Mobile testing
```
set_device device="iPhone 14"
navigate url="https://example.com"
screenshot                            → capture mobile view
```

## Understanding Responses

Every navigation action returns the actual URL, page title, and tab context:
```
Navigated to: https://example.com | Title: "Example Domain" (tab 1/1)
```

If a redirect occurred:
```
Navigated to: https://example.com | Title: "Example" (redirected from http://example.com) (tab 1/1)
```

If navigation failed:
```
Error: navigation to https://bad.example failed: net::ERR_NAME_NOT_RESOLVED (tab 1/1)
```

Screenshot responses include a resized preview image (sent as a visual block you can see) and file info:
```
[image preview is displayed inline for visual analysis]
Screenshot captured (1920x1080, 245KB). Preview: 1024x576 (32KB). Full resolution: /tmp/ionclaw/browser/screenshot_1710043200000.png
```

PDF responses return only the file path:
```
PDF saved: /tmp/ionclaw/browser/page_1710043200000.pdf (156KB)
```

Wait timeout responses report which conditions failed:
```
Error: wait timed out after 10s. Conditions not met: text='Welcome' selector='#dashboard'
```

## Error Recovery

- If actions fail with CDP errors: use `stop` then `navigate` to restart.
- "element not found": use `inspect` to find correct selectors.
- "page load timed out": use `snapshot` to check what loaded.
- If the browser is unresponsive: `stop` then try again with `navigate`.
