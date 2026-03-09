---
name: local-image-generation
description: Create and edit images locally with no AI (programmatic operations). Use when the user wants to create a new image (blank, gradient, solid color), resize an image, draw rectangles or shapes on an image, add a watermark, paste a logo, overlay one image on another, or do any Pillow/ImageMagick-style image operations. Do not use for text-to-image generation — use generate_image (AI) instead.
---

# Local Image Operations

Use the `image_ops` tool to create, resize, draw on, and composite images **without AI**. All operations are programmatic: you specify dimensions, colors, positions, and paths. The tool does not interpret prompts or generate content from text descriptions.

## Supported Formats

**Input (read):** PNG, JPEG, BMP, GIF, TGA, PSD, HDR, PIC, PNM

**Output (write):** PNG only. All results are saved as PNG regardless of the output file extension.

## Tool: `image_ops`

Supported operations: **create**, **resize**, **draw_rect**, **overlay**. Choose one per call via the `operation` parameter.

### Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | string | Yes | One of: `create`, `resize`, `draw_rect`, `overlay`. Determines which operation runs and which other parameters are required. |
| `output_path` | string | Yes | Path where the result image is written. Must be under workspace or `public/` (e.g. `public/media/result.png`). |
| `input_path` | string | No | Source image path. Required for `resize`, `draw_rect`, and `overlay`. Must be under workspace or `public/`. |
| `overlay_path` | string | No | Image to paste on top (logo, watermark). Required only for `overlay`. |
| `width` | integer | No | For **create**: image width (1–8192). For **resize**: target width. For **draw_rect**: rectangle width. |
| `height` | integer | No | For **create**: image height (1–8192). For **resize**: target height. For **draw_rect**: rectangle height. |
| `x` | integer | No | X position. For **draw_rect**: top-left X of the rectangle. For **overlay**: X where overlay top-left is placed. |
| `y` | integer | No | Y position. For **draw_rect**: top-left Y of the rectangle. For **overlay**: Y where overlay top-left is placed. |
| `overlay_width` | integer | No | For **overlay** only. Width to resize the overlay to before pasting (e.g. `100`). Use with `overlay_height` for a specific size. |
| `overlay_height` | integer | No | For **overlay** only. Height to resize the overlay to before pasting (e.g. `20`). Use with `overlay_width` for a specific size. |
| `background` | string | No | For **create** only. `gradient` (default) or `solid`. |
| `color` | string | No | Hex color RRGGBB (6 digits, no `#`). For **create**: used when `background` is `solid`. For **draw_rect**: fill color of the rectangle. |

### Paths

All paths (`output_path`, `input_path`, `overlay_path`) must resolve to files under the **workspace** or under **public** (e.g. `public/media/`). The tool validates paths and rejects anything outside these directories. Use relative paths such as `public/media/canvas.png` or paths relative to the workspace root.

---

## Operation: create

Create a new image from scratch. No input image is used.

**Required:** `operation`, `output_path`, `width`, `height`  
**Optional:** `background` (default `gradient`), `color` (for solid background; default `ffffff`)

**Behavior:**

- **gradient** — Fills the image with a procedural gradient (horizontal and vertical color variation). Useful for placeholders and backgrounds.
- **solid** — Fills the image with a single color. Set `color` to hex RRGGBB (e.g. `ff0000` for red).

**Create a gradient placeholder (e.g. for a hero section):**

```
image_ops(operation="create", output_path="public/media/hero.png", width=1920, height=1080)
```

**Create a solid-color image:**

```
image_ops(operation="create", output_path="public/media/red.png", width=400, height=300, background="solid", color="ff0000")
```

**Create a white canvas:**

```
image_ops(operation="create", output_path="public/media/canvas.png", width=800, height=600, background="solid", color="ffffff")
```

**Create a black banner background:**

```
image_ops(operation="create", output_path="public/media/banner_bg.png", width=1200, height=400, background="solid", color="000000")
```

---

## Operation: resize

Resize an existing image to new dimensions. Aspect ratio can change.

**Required:** `operation`, `input_path`, `output_path`, `width`, `height`

**Behavior:** Loads the image at `input_path`, resizes it to the given width and height, and writes the result to `output_path`. Supports common formats (PNG, JPEG, etc.) for input; output is PNG.

**Resize to thumbnail:**

```
image_ops(operation="resize", input_path="public/media/photo.png", output_path="public/media/photo_thumb.png", width=200, height=200)
```

**Resize to specific dimensions (e.g. social card):**

```
image_ops(operation="resize", input_path="public/media/image.jpg", output_path="public/media/card.png", width=1200, height=630)
```

**Downscale keeping aspect ratio (example 50%):**

Compute target dimensions as needed, then call with the desired `width` and `height`.

---

## Operation: draw_rect

Draw a filled rectangle on an existing image. Use for panels, bars, masks, or blocking out regions (e.g. where text or a logo will go).

**Required:** `operation`, `input_path`, `output_path`, `x`, `y`, `width`, `height`, `color`

**Behavior:** Loads the image at `input_path`, fills the rectangle with top-left at (`x`, `y`) and size (`width`, `height`) with the given hex color, and writes to `output_path`. You can set `output_path` equal to `input_path` to overwrite the original. Coordinates and dimensions are clamped to the image bounds.

**Black bar at top of image:**

```
image_ops(operation="draw_rect", input_path="public/media/banner.png", output_path="public/media/banner.png", x=0, y=0, width=800, height=60, color="000000")
```

**White rectangle (e.g. text area) at (100, 100) with size 400×80:**

```
image_ops(operation="draw_rect", input_path="public/media/slide.png", output_path="public/media/slide.png", x=100, y=100, width=400, height=80, color="ffffff")
```

**Red accent block at bottom-right:**

```
image_ops(operation="draw_rect", input_path="public/media/card.png", output_path="public/media/card.png", x=700, y=450, width=100, height=50, color="ff0000")
```

---

## Operation: overlay

Paste one image on top of another at a given position. Use for watermarks, logos, badges, or any compositing where one image sits on top of another.

**Required:** `operation`, `input_path`, `overlay_path`, `output_path`, `x`, `y`  
**Optional:** `overlay_width`, `overlay_height` — when both are set, the overlay is resized to this size (in pixels) before pasting. Use for a proportional watermark or logo size (e.g. 100×20).

**Behavior:** Loads the base image at `input_path` and the overlay at `overlay_path`. If `overlay_width` and `overlay_height` are both positive, the overlay is resized to that size first. Pastes the overlay with its top-left corner at (`x`, `y`) on the base. Writes the result to `output_path`. Overlay is opaque (pixels replace base pixels in that region). If the overlay extends beyond the base image, the excess is clipped.

**Watermark at bottom-right (e.g. base 800×600, position 690, 550):**

```
image_ops(operation="overlay", input_path="public/media/photo.png", overlay_path="public/media/watermark.png", output_path="public/media/photo_watermarked.png", x=690, y=550)
```

**Watermark at a specific size (e.g. 100×20) at bottom-right:**

```
image_ops(operation="overlay", input_path="public/media/photo.png", overlay_path="public/media/watermark.png", output_path="public/media/photo_watermarked.png", x=700, y=580, overlay_width=100, overlay_height=20)
```

**Logo at top-left:**

```
image_ops(operation="overlay", input_path="public/media/banner.png", overlay_path="public/media/logo.png", output_path="public/media/banner_with_logo.png", x=10, y=10)
```

**Logo centered horizontally at top (example: base width 800, logo width 200 → x=300):**

```
image_ops(operation="overlay", input_path="public/media/hero.png", overlay_path="public/media/logo.png", output_path="public/media/hero_logo.png", x=300, y=20)
```

---

## Color format

All colors use **hex RRGGBB**: 6 hexadecimal digits, no `#`. Uppercase or lowercase is fine.

| Color | Hex |
|-------|-----|
| Black | `000000` |
| White | `ffffff` |
| Red | `ff0000` |
| Green | `00ff00` |
| Blue | `0000ff` |
| Gray | `808080` |
| Yellow | `ffff00` |
| Cyan | `00ffff` |
| Magenta | `ff00ff` |

Use `color` in **create** (with `background="solid"`) and in **draw_rect** for the rectangle fill.

---

## Workflows

**Placeholder then thumbnail**

1. Create a gradient image.
2. Resize it to thumbnail dimensions.

```
image_ops(operation="create", output_path="public/media/hero.png", width=1920, height=1080)
image_ops(operation="resize", input_path="public/media/hero.png", output_path="public/media/hero_thumb.png", width=400, height=225)
```

**Banner with top bar and logo**

1. Create gradient banner.
2. Draw a black bar at the top.
3. Overlay the logo at (10, 10).

```
image_ops(operation="create", output_path="public/media/banner.png", width=800, height=200)
image_ops(operation="draw_rect", input_path="public/media/banner.png", output_path="public/media/banner.png", x=0, y=0, width=800, height=40, color="000000")
image_ops(operation="overlay", input_path="public/media/banner.png", overlay_path="public/media/logo.png", output_path="public/media/banner_final.png", x=10, y=10)
```

**Watermark a photo**

1. Overlay the watermark image at the desired position (e.g. bottom-right). Optionally set `overlay_width` and `overlay_height` to paste the watermark at a specific size (e.g. 100×20).

```
image_ops(operation="overlay", input_path="public/media/photo.jpg", overlay_path="public/media/watermark.png", output_path="public/media/photo_watermarked.png", x=700, y=500)
```

With fixed size 100×20:

```
image_ops(operation="overlay", input_path="public/media/photo.jpg", overlay_path="public/media/watermark.png", output_path="public/media/photo_watermarked.png", x=700, y=580, overlay_width=100, overlay_height=20)
```

---

## What NOT to do

- Do not use `image_ops` when the user wants an image **generated from a text description** (e.g. "draw a sunset", "create an illustration of a cat"). Use the `generate_image` tool (AI) for that.
- Do not use a `#` in color values; use only 6 hex digits (e.g. `ff0000`, not `#ff0000`).
- Do not use paths outside the workspace or `public/`; the tool will reject them.
- Do not assume aspect ratio is preserved in **resize**; you set both `width` and `height` explicitly.

---

## After operations

Return the resulting path to the user so they can open or link to the image (e.g. `/public/media/result.png` or the path you used in `output_path`).
