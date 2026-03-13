---
name: vision
description: Analyze and describe images using AI vision. Use when the user asks to look at, describe, read, analyze, or extract information from an image — from a local file, URL, or base64 data.
---

# Vision

Analyze images using the `vision` tool. The image is sent directly to the AI model for visual analysis.

## Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `path` | string | one of path/url/base64 | Path to a local image file (relative to project root, or absolute for temp files) |
| `url` | string | one of path/url/base64 | URL of a remote image to fetch and analyze |
| `base64` | string | one of path/url/base64 | Base64-encoded image data (with or without data URI prefix) |
| `question` | string | no | Specific question about the image. If omitted, provides a general description |
| `mime_type` | string | no | Override MIME type (auto-detected from file extension or URL) |

## CRITICAL: When NOT to Use This Tool

If the user already sent an image in the chat message, you can **see it directly** — do NOT use this tool. Just describe it from context.

Use this tool ONLY when you need to load an image from:
- A file path on disk (e.g. from a previous `browser action="screenshot"`)
- A remote URL
- External base64 data

## How It Works

The `vision` tool loads an image from any source, resizes it to an LLM-friendly preview (max 1024px wide, JPEG), and passes it to the model as a visual content block. The model then "sees" the image and can describe, analyze, or answer questions about it — no external vision API required.

## Common Workflows

### Describe a local image
```
vision path="public/media/photo.jpg"
```

### Analyze a screenshot
```
vision path="public/screenshots/page.png" question="What error message is shown?"
```

### Analyze an image from a URL
```
vision url="https://example.com/chart.png" question="What trends does this chart show?"
```

### Read text from an image (OCR)
```
vision path="public/documents/scan.png" question="Extract all text from this image"
```

### Analyze a browser screenshot (with output_path)
```
browser action="screenshot" output_path="public/screenshots/page.png"
vision path="public/screenshots/page.png" question="What is displayed on this page?"
```

### Use with base64 data
```
vision base64="data:image/png;base64,iVBOR..." question="What is this?"
```

## Supported Formats

JPEG, PNG, GIF, WebP, SVG, BMP, ICO, TIFF, AVIF.

Max image size: 20MB.

## Notes

- Provide **exactly one** image source: `path`, `url`, or `base64`
- MIME type is auto-detected from file extension or Content-Type header
- Use `question` to focus the analysis on specific aspects
- The model's vision capabilities depend on the underlying LLM (Claude, GPT-4V, Gemini all support vision)
