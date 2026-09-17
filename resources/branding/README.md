# Turbo Chook branding

<img src="turbochook-logo.png" alt="Turbo Chook logo: a running Tasmanian nativehen with the wordmark" width="520">

## Haiku application icon

![Turbo Chook's application icon on a light background](turbochook-icon-preview.png)

The icon is a Tasmanian nativehen (*Tribonyx mortierii*) at full tilt, the bird
Tasmanians call the turbo chook: a stocky olive-brown body, a short cocked
tail, a stout yellow-green bill, the red eye and the white flank streak, on
strong grey legs, with two motion streaks behind it.

It follows [Haiku's icon guidelines](https://www.haiku-os.org/development/icon-guidelines/):

- 64 x 64 canvas, broad shapes and no detail that does not carry meaning at 32 px.
- Light from the upper left: body, head and tail gradients run from a lighter
  top-left to a darker lower-right; the bill's upper half is lighter than the lower.
- The floor shadow is a soft ellipse extending straight to the right of the feet.
- A dark silhouette one pixel wide at 32 x 32 (a four-unit contour transformer
  on the 64-unit canvas) around the body, head, tail and bill; the legs are dark
  strokes that outline themselves.
- Mixed rather than pure colours: olive `#6f6c3f`, grey-green head `#737048`,
  bill `#dcd65e`/`#aaa33a`, eye `#d43a2a`, legs `#5f6469`, outline `#2a2818`.
- Transparent background; the whole icon is 789 bytes of HVIF.

| 16 × 16 | 32 × 32 | 64 × 64 |
| --- | --- | --- |
| ![16 px](turbochook-icon-16.png) | ![32 px](turbochook-icon-32.png) | ![64 px](turbochook-icon-64.png) |

Files:

- [turbochook-icon.hvif](turbochook-icon.hvif): the native vector icon embedded
  by `resources/TurboChook.rdef` and stamped on the executable and its MIME type.
- [turbochook-icon.svg](turbochook-icon.svg): SVG export of the same paths,
  gradients and strokes for editing in Icon-O-Matic (import) or other tools.
- PNG renders at [16](turbochook-icon-16.png), [32](turbochook-icon-32.png),
  [64](turbochook-icon-64.png) and [256](turbochook-icon-256.png) pixels and a
  [light-background preview](turbochook-icon-preview.png), all produced by
  Haiku's own `BIconUtils` renderer (`tools/render-icon.cpp`).
- [turbochook-logo.png](turbochook-logo.png) and its
  [transparent variant](turbochook-logo-transparent.png): the 256 px native
  render beside the wordmark in DejaVu Sans Bold.

## How the icon is made

`tools/make-icon.py` describes the bird as cubic paths, linear and radial
gradients and stroke/contour transformers, and `tools/hvif.py` encodes that
description straight into Haiku's flat icon format (styles, paths with
command-packed points, shapes with transformers). The encoder was validated by
decoding and re-encoding Kiri's Icon-O-Matic-produced icon, and every revision
of the bird was rendered with `BIconUtils::GetVectorIcon` on a Haiku VM.

```sh
python3 tools/make-icon.py resources/branding/turbochook-icon.hvif preview.png
python3 tools/hvif.py dump resources/branding/turbochook-icon.hvif
```

To regenerate the native renders on Haiku:

```sh
g++ -std=c++17 tools/render-icon.cpp -o build-haiku/render-icon -lbe -ltranslation
for size in 16 32 64 256; do
    build-haiku/render-icon resources/branding/turbochook-icon.hvif resources/branding/turbochook-icon-$size.png $size
done
build-haiku/render-icon resources/branding/turbochook-icon.hvif resources/branding/turbochook-icon-preview.png 256 light
```
