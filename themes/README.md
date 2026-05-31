# LightSticker theme packs

Drop a `.json` file in this folder and LightSticker will list it in
**Right-click -> Theme** the next time it starts. Each pack overrides
the built-in theme presets for the active sticker.

## Where LightSticker looks

On startup the loader scans, in this order:

1. `<EXE_DIR>/themes/*.json` (this folder, when running portable)
2. `%LOCALAPPDATA%/LightSticker/themes/*.json` (per-user)

Packs from (2) override (1) when the `id` field collides, so you can
ship a default set with the EXE and let users replace individual
themes by saving a same-id file under their LocalAppData folder.

## Schema

```json
{
  "schemaVersion": 1,
  "id": "sakura",
  "name": "Sakura",
  "description": "Soft pink, like cherry blossoms.",
  "tokens": {
    "bgColor": "#FFE4ED",
    "textColor": "#3A1F2B",
    "opacityPercent": 96,
    "cornerRadius": 18
  }
}
```

| Field | Type | Notes |
|---|---|---|
| `schemaVersion` | int | Must be `1`. |
| `id` | string | Stable identifier (lowercase, no spaces). Used as menu key. |
| `name` | string | Shown in the menu. |
| `description` | string | Optional, currently unused at runtime. |
| `tokens.bgColor` | string | `#RRGGBB`. Sticker background. |
| `tokens.textColor` | string | `#RRGGBB`. Sticker foreground. |
| `tokens.opacityPercent` | int | `20`..`100`. Whole-sticker alpha. |
| `tokens.cornerRadius` | int | `0`..`48`. DIPs. |

Unknown fields are ignored, so you can extend a file (for your own
notes or a generator pipeline) without breaking the loader.

## AI workflow

Theme packs are designed so an LLM can produce them directly. Two
pipelines worked well during development:

### Gemini (text)

Ask for tokens-as-JSON, validate locally, drop the file in here. A
prompt skeleton:

> You are a UI designer for a sticky-notes app. Output **only** a
> JSON object that matches this schema (no prose, no code fences):
>
> ```json
> { "schemaVersion": 1, "id": "...", "name": "...",
>   "description": "...",
>   "tokens": {
>     "bgColor": "#RRGGBB", "textColor": "#RRGGBB",
>     "opacityPercent": 20-100, "cornerRadius": 0-48 } }
> ```
>
> Theme: a calm late-summer evening on a porch.

The hand-rolled JSON parser at `src/json_min.h` validates structure;
out-of-range tokens are silently clamped.

### gpt-image-2 (image-driven)

Ask the model for a 1024x1024 reference image of the desired theme,
then either:

- pull dominant colours with any tiny image-quantiser and stuff them
  into the schema above, or
- ask Gemini in a follow-up to read the image and emit tokens.

Either way the **file dropped in this folder is plain JSON** -- the
generated image itself is reference material, not a runtime asset.

## Built-in themes

The three built-ins (`Default`, `Miku`, `Transparent`) are still
hard-coded in `src/main.cpp`; they exist before any theme pack loads
and provide a guaranteed-working baseline. Packs cannot remove them.
