# Image Generation

IonClaw supports AI-powered image generation and editing through provider-specific generators. Each provider has its own dedicated generator that handles API-specific request formats, parameters, and response parsing.

---

## Configuration

```yaml
image:
  model: "gemini/gemini-2.5-flash-image"
  aspect_ratio: ""    # default aspect ratio (optional)
  size: ""            # default size (optional)
```

The provider is auto-detected from the model prefix (`gemini/`, `openai/`, `grok/`). The corresponding credential must be configured under `credentials` and `providers`.

---

## Supported Providers

| Provider | Generator | Models | Generation | Editing |
|----------|-----------|--------|-----------|---------|
| `gemini` / `google` | GeminiImageGenerator | gemini-2.5-flash-image, gemini-3.1-flash-image-preview, imagen-4.0-* | Yes | Yes (Gemini models only) |
| `openai` | OpenAIImageGenerator | gpt-image-1, gpt-image-1-mini, gpt-image-1.5, dall-e-3, dall-e-2 | Yes | Partial |
| `grok` | GrokImageGenerator | grok-imagine-image, grok-imagine-image-pro | Yes | Yes |

---

## Gemini (Google)

Two types of models are supported, each with a different API:

### Gemini models (generateContent)

**Models:** `gemini-2.5-flash-image`, `gemini-3.1-flash-image-preview`

**URL:** `https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent`

**Auth:** `x-goog-api-key` header.

**Generation:** Sends a `generateContent` request with `responseModalities: ["TEXT", "IMAGE"]`. The response contains `inlineData` with base64-encoded image data.

**Editing (reference images):** Reference images are sent as `inlineData` parts (base64) before the text prompt in the same request. No separate edit endpoint — uses the same `generateContent` API.

| Model | Aspect ratios | Sizes | Reference images | Notes |
|-------|--------------|-------|-----------------|-------|
| gemini-2.5-flash-image | 1:1, 3:2, 2:3, 3:4, 4:3, 4:5, 5:4, 9:16, 16:9, 21:9 | 1K, 2K, 4K | Yes | Stable, speed-optimized |
| gemini-3.1-flash-image-preview | 1:1, 1:4, 1:8, 2:3, 3:2, 3:4, 4:1, 4:3, 4:5, 5:4, 8:1, 9:16, 16:9, 21:9 | 512, 1K, 2K, 4K | Yes (up to 10) | Preview, supports thinkingConfig |

**Parameters:**

| Parameter | Mapping | Notes |
|-----------|---------|-------|
| `aspect_ratio` | `generationConfig.imageConfig.aspectRatio` | Passed directly |
| `size` | `generationConfig.imageConfig.imageSize` | e.g. `512`, `1K`, `2K`, `4K` |
| `google_search` | `tools: [{googleSearch: {}}]` | Enables Google Search grounding |
| `reference_images` | `inlineData` parts before prompt | Gemini models only |

### Imagen models (predict)

**Models:** `imagen-4.0-generate-001`, `imagen-4.0-ultra-generate-001`, `imagen-4.0-fast-generate-001`

**URL:** `https://generativelanguage.googleapis.com/v1beta/models/{model}:predict`

**Auth:** `x-goog-api-key` header.

**Generation:** Text-to-image only (no reference images). Uses `instances[].prompt` and `parameters` format.

| Model | Aspect ratios | Sizes | Notes |
|-------|--------------|-------|-------|
| imagen-4.0-generate-001 | 1:1, 3:4, 4:3, 9:16, 16:9 | 1K, 2K | Standard tier |
| imagen-4.0-ultra-generate-001 | 1:1, 3:4, 4:3, 9:16, 16:9 | 1K, 2K | Ultra (highest quality) |
| imagen-4.0-fast-generate-001 | 1:1, 3:4, 4:3, 9:16, 16:9 | N/A | Fast (lowest latency) |

**Base URL resolution:** The default `resolveBaseUrl` for gemini returns the OpenAI-compatible endpoint (`.../v1beta/openai`). The generator strips the path and uses only the scheme + authority to build the native API URL.

**Example config:**

```yaml
image:
  model: "gemini/gemini-2.5-flash-image"

credentials:
  gemini:
    type: simple
    key: ${GOOGLE_API_KEY}

providers:
  gemini:
    credential: gemini
```

---

## OpenAI

**API:** OpenAI Images API with separate generation and edit endpoints.

**URLs:**
- Generation: `https://api.openai.com/v1/images/generations` (JSON)
- Editing: `https://api.openai.com/v1/images/edits` (multipart form)

**Auth:** `Authorization: Bearer` header.

### Model differences

| Model | Generation | Editing | Response field | Generation sizes | Edit sizes | Multipart field |
|-------|-----------|---------|----------------|-----------------|------------|-----------------|
| gpt-image-1 | Yes | Yes (up to 16 images) | `output_format: "png"` | 1024x1024, 1536x1024, 1024x1536, auto | 1024x1024, 1536x1024, 1024x1536 | `image[]` (multiple) |
| gpt-image-1-mini | Yes | Yes (up to 16 images) | `output_format: "png"` | 1024x1024, 1536x1024, 1024x1536, auto | 1024x1024, 1536x1024, 1024x1536 | `image[]` (multiple) |
| gpt-image-1.5 | Yes | Yes (up to 16 images) | `output_format: "png"` | 1024x1024, 1536x1024, 1024x1536, auto | 1024x1024, 1536x1024, 1024x1536 | `image[]` (multiple) |
| dall-e-3 | Yes | No (falls back to generation) | `response_format: "b64_json"` | 1024x1024, 1792x1024, 1024x1792 | N/A | N/A |
| dall-e-2 | Yes | Yes (1 image) | `response_format: "b64_json"` | 256x256, 512x512, 1024x1024 | 256x256, 512x512, 1024x1024 | `image` (single) |

All `gpt-image-*` models share the same API behavior: `output_format` instead of `response_format`, always returns base64, supports multipart `image[]` for edits.

**Parameters:**

| Parameter | Mapping | Notes |
|-----------|---------|-------|
| `aspect_ratio` | Mapped to pixel `size` | `16:9`→`1536x1024` (gpt-image) or `1792x1024` (dall-e-3) |
| `size` | `size` (generation only) | `2K`/`4K`→`1792x1024` for dall-e-3 |
| `reference_images` | Multipart `image[]` or `image` | Triggers edit endpoint |

**Example config:**

```yaml
image:
  model: "openai/gpt-image-1"

credentials:
  openai:
    type: simple
    key: ${OPENAI_API_KEY}

providers:
  openai:
    credential: openai
```

---

## Grok (xAI)

**API:** xAI Images API, JSON-based for both generation and editing (no multipart).

**URLs:**
- Generation: `https://api.x.ai/v1/images/generations`
- Editing: `https://api.x.ai/v1/images/edits`

**Auth:** `Authorization: Bearer` header.

**Generation:** Standard JSON request with `response_format: "b64_json"`.

**Editing:** JSON request with reference images as base64 data URIs.

- Single image: `"image": {"url": "data:...", "type": "image_url"}`
- Multiple images: `"images": [{"url": "data:...", "type": "image_url"}, ...]` (up to 3)

### Model differences

| Model | Notes |
|-------|-------|
| grok-imagine-image | Standard quality, 300 RPM |
| grok-imagine-image-pro | Higher quality, 30 RPM |

Both models support the same API parameters and endpoints.

**Valid aspect ratios:** `1:1`, `16:9`, `9:16`, `4:3`, `3:4`, `3:2`, `2:3`, `2:1`, `1:2`, `19.5:9`, `9:19.5`, `20:9`, `9:20`, `auto`

**Valid resolutions:** `1k`, `2k`

**Parameters:**

| Parameter | Mapping | Notes |
|-----------|---------|-------|
| `aspect_ratio` | `aspect_ratio` | Passed directly to the API |
| `size` | `resolution` | `2K`/`4K`→`"2k"`, others→`"1k"` |
| `reference_images` | `image` or `images` (base64 data URIs) | Up to 3 images |

**Example config:**

```yaml
image:
  model: "grok/grok-imagine-image"

credentials:
  grok:
    type: simple
    key: ${XAI_API_KEY}

providers:
  grok:
    credential: grok
```

---

## Shared Helper (ImageGeneratorHelper)

All generators share common utilities:

| Method | Description |
|--------|-------------|
| `extractModelId(model)` | Strips provider prefix (`openai/gpt-image-1` → `gpt-image-1`) |
| `resolveReferencePaths(params, workspace, public)` | Resolves and validates reference image paths |
| `cleanReferencePath(raw)` | Strips `[media: path (type)]` annotations from LLM-generated paths |
| `decodeAndSave(responseBody, public, filename, url)` | Parses `data[0].b64_json` response and saves to public/media |
| `saveToPublicMedia(data, public, filename, url)` | Writes raw image bytes to `public/media/` and returns URL |

`decodeAndSave` handles the OpenAI/Grok response format (`data[0].b64_json`). Gemini models use different response formats and call `saveToPublicMedia` directly after extracting image data.

---

## Custom Base URL

All generators support custom base URLs via the provider's `base_url` config:

```yaml
providers:
  openai:
    credential: openai
    base_url: "https://my-proxy.example.com"  # overrides https://api.openai.com
```

For Gemini, only the scheme + host are used from the base URL (the native API path `/v1beta/models/...` is always appended).
