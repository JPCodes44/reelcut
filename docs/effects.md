# How Reelcut's effects work

A teaching companion to the effects code. Each section explains one
technique, then points at the lines in Reelcut that implement it. All of the
looks, shaders and parameters here are written for Reelcut; read them, change
them, and re-run `bin/test` to see what breaks.

Two renderers draw every effect, and they have to agree:

| | Preview (while editing) | Export (the MP4) |
| --- | --- | --- |
| Runs in | Qt Quick, on the GPU | ffmpeg, a filter graph |
| Written in | GLSL fragment shaders (`src/shaders/`) | ffmpeg filters (`src/exportgraph.cpp`) |
| Speed | Must hit 60 fps | Can take its time |
| Accuracy | Close | Exact |

Keeping two renderers in step is the main engineering problem in any editor.
Reelcut solves it by computing the effect once in C++ and handing the *same
data* to both, wherever it can.

## 1. Filters: 3D lookup tables

**Code:** `src/filters.cpp`, `src/shaders/lut.frag`, `prepareExport()` in
`src/jobs.cpp`, `videoFinish()` in `src/exportgraph.cpp`.

### The idea

A color filter is a function from one color to another: `out = f(r, g, b)`.
Running real grading maths (curves, split toning, saturation) for every pixel
of every frame is slow, so editors **precompute** the function on a grid of
colors and store the answers. That table is a *3D lookup table*, or LUT. At
render time each pixel finds its place in the grid and blends the nearest
stored answers. The cost no longer depends on how complicated the look is.

### Defining a look

Each look in `buildLooks()` is a `Grade`: a handful of controls applied in a
fixed order by `grade()`:

1. **Contrast** blends toward a smoothstep S-curve, which steepens the
   mid-tones and leaves pure black and white where they are.
2. **Lift, gamma, gain** per channel: lift raises the darks, gamma bends
   the mid-tones, gain scales the brights. Pushing blue's lift up and red's
   gain up gives cool shadows and warm highlights.
3. **Split toning** adds one tint weighted toward the darks
   (`(1 - luma)^2`) and another toward the brights (`luma^2`).
4. **Saturation** blends between the color and its grey (Rec. 709 luma).
5. **Mono** blends toward a tinted grey, for black and white and sepia.
6. **Fade** lifts black toward grey, like a faded print.

Try it: in `buildLooks()`, add a look with `g.mono = 1.0;
g.monoTint = {0.8, 1.0, 0.85};` and you have a green-tinted night-vision
look in both the preview and the export.

### Baking the table, two ways

`grade()` is sampled into two tables:

- **`.cube` for export.** `cubeText()` walks a 33×33×33 grid (red changes
  fastest, then green, then blue), writes one `r g b` line per point, and
  ffmpeg's `lut3d` filter reads it. The strength slider is baked in: each
  entry is `mix(input, graded, amount)`. Mixing the table's outputs is the
  same as mixing the pictures, because the mix is linear.
- **A strip image for the preview.** GPUs read 2D textures most easily, so
  `stripImage()` flattens the cube into a 512×512 PNG: 64 blue levels laid
  out as an 8×8 grid of tiles, each tile a 64×64 square of red (left to
  right) by green (top to bottom).

### The lookup shader

`src/shaders/lut.frag` reverses that layout for each pixel:

```glsl
float blue = rgb.b * 63.0;                    // which of 64 tiles
vec2 inTile = rgb.rg * (63.0 / 512.0) + 0.5 / 512.0;  // which cell in a tile
vec3 low  = texture(lut, tileOrigin(floor(blue)) + inTile).rgb;
vec3 high = texture(lut, tileOrigin(ceil(blue))  + inTile).rgb;
vec3 graded = mix(low, high, fract(blue));    // blend between tiles
```

The texture sampler interpolates red and green for free (bilinear filtering
inside a tile). Blue jumps from tile to tile, so the shader samples the two
tiles either side and blends them itself. That's trilinear interpolation
built out of one hardware bilinear lookup and one manual mix.

The `0.5 / 512.0` offset matters: texture coordinates point at pixel
*edges*, and without it every lookup lands between two cells.

### Things to notice

- **Order matters.** Export applies the sliders, then the filter, then the
  user's own `.cube`. Swap them in `videoFinish()` and a mono filter followed
  by a warm LUT gives a different picture than the reverse.
- **Resolution.** 33 points per axis is the usual export size. Try
  `cubeText(look, 1.0, 5)`: with 5 points, banding appears in smooth
  gradients, because the interpolation can't follow the curves.
- **Why `interp=tetrahedral`.** ffmpeg's default trilinear interpolation
  blends 8 grid points; tetrahedral uses 4 and follows the grey axis
  exactly, which keeps neutrals neutral.

### Tests

`filterLooksGradeWithinRange`, `filterCubeIsWellFormed`,
`filterSavesAndExports`, `filterRendersThroughFfmpeg` (renders a real frame
and checks Silver leaves no color) and `mcpSetsAFilter`.

## 2. Transitions: two pictures and a progress value

**Code:** `src/transitions.cpp`, `Project::overlap()` and `clipStarts()` in
`src/project.cpp`, `joinClips()` in `src/exportgraph.cpp`,
`src/shaders/transition.frag`, `src/ClipSlot.qml`, `src/Preview.qml`.

### The idea

Every transition, however fancy, is the same function:

```
pixel = T(from_pixel, to_pixel, uv, progress)
```

`from` is the outgoing clip, `to` the incoming one, `uv` the pixel's
position, and `progress` runs 0 → 1 across the transition. A dissolve is
`mix(from, to, progress)`. A wipe picks one or the other depending on which
side of a moving line `uv` falls. A slide moves *where* each picture is
sampled. That's the whole design space: change the colour, change the
coordinates, or choose between the two.

### Overlap changes time

For both pictures to exist at once, the clips must overlap. A 0.5 s
transition starts the next clip 0.5 s early, so the reel gets 0.5 s shorter.
Everything that places things in time (`clipStarts()`, `locate()`, the
timeline, captions) goes through one function, so the change is made once.

`overlap()` limits a transition to half of either clip. Otherwise a long
transition into a short clip would have the clip after it start before it
does.

During the overlap, `locate()` says the **outgoing** clip owns the time.
The incoming clip is already playing underneath, which is what lets the
preview keep a steady clock.

### Export: chaining xfade

ffmpeg's `xfade` takes two inputs, an offset (when the transition starts)
and a duration. `joinClips()` chains them two at a time:

```
[v0][v1]xfade=transition=circleopen:duration=0.5:offset=3.5[vj1];
[vj1][v2]concat=n=2:v=1:a=0[vcat]
```

The offset is simply where the next clip starts on the timeline, which
`clipStarts()` already knows. Sound gets the matching `acrossfade`, so the
audio stays exactly as long as the picture. Cuts without a transition use a
two-input `concat` in the same chain.

### Preview: two slots

A single video player can't show two clips at once, so the preview keeps
two `ClipSlot`s and alternates: even clips play in one, odd clips in the
other. While one clip shows, the next is already loaded, paused on its
first frame, in the other slot. That makes cuts instant, and at a
transition both slots simply play.

`transition.frag` receives both slots as textures (`from`, `to`) and draws
the chosen mode. Each branch is a few lines; compare them:

```glsl
// wipe: choose by position
c = uv.x > 1.0 - p ? B(uv) : A(uv);
// slide: move where each picture is sampled
c = uv.x < 1.0 - p ? A(uv + vec2(p, 0.0)) : B(uv - vec2(1.0 - p, 0.0));
// iris: choose by distance from the centre, corrected for the 9:16 frame
c = length((uv - 0.5) * vec2(aspect, 1.0)) < p * reach ? B(uv) : A(uv);
```

The `aspect` correction is easy to forget. Without it the iris is an oval,
because `uv` runs 0..1 on both axes of a frame that is taller than wide.

The sound crossfades too: each slot's volume follows `progress`.

### Things to notice

- **Preview and export are separate implementations** of the same motion.
  They can drift apart. Compare a paused preview frame with `reelcut export`
  at the same time to see how close they are, and treat any mismatch as a
  bug in one or the other.
- **Easing.** Every mode here is linear in `progress`. Try
  `p = smoothstep(0.0, 1.0, progress)` at the top of `main()` and the
  slides start and stop gently.
- **Write your own.** Add a branch (mode 15) in `transition.frag`, run
  `bin/shaders`, and add it to `kinds()` with the closest `xfade` type for
  export. ffmpeg also takes a custom `expr` for `xfade` if you want export
  to match exactly.

### Tests

`transitionsOverlapClips` (timing), `transitionGraphChainsXfade` (filter
graph), `transitionsRenderThroughFfmpeg` (every transition renders and the
reel is the right length), `previewShowsTransitions` (both slots and the
shader in the real UI) and `mcpSetsATransition`.

## 3. Video effects: a track of timed filters

**Code:** `src/effects.cpp` (`exportChain()`), `effectChains()` in
`src/exportgraph.cpp`, `src/shaders/effect.frag`, the effects lane in
`src/ReelTimeline.qml`, the Effects tab in `src/Inspector.qml`.

### The idea

An effect is a filter that is only on for part of the reel. Each one stores
a kind, a start and end on the timeline, an intensity, and a speed for the
ones that move. Effects stack in order: each one works on the picture the
previous one produced.

### Export: split, change, lay back over

Some ffmpeg filters can be switched on for a time range with
`enable='between(t,a,b)'`; many can't (`crop`, `scale`, `zoompan`). Rather
than special-casing them, `exportChain()` uses one pattern for every effect:

```
[in]split[base][work];
[work]<the effect>[done];
[base][done]overlay=enable='between(t,1,2.5)'[out]
```

Copy the picture, change the copy, and lay the copy over the original only
while the effect is active. `overlay` supports `enable`, so every effect
gets timing for free. The cost is one extra full-frame copy per effect.

Because the copy and the original are the same size and frame, effects that
*move* the picture (shake, glitch) work too: they scale the copy up a
little, then move a crop window around it so no black edges show.

### Animating with expressions

ffmpeg filters accept expressions evaluated per frame, with `t` as the
time. Each effect builds its animation from that:

| Effect | Expression | What it does |
| --- | --- | --- |
| Shake | `x='(iw-ow)/2+30*sin((t-1)*32)'` | crop window swings on a sine |
| Strobe | `brightness='0.8*exp(-pow(mod((t-1)*4,1)*6,2))'` | a sharp spike that decays, 4 times a second |
| Zoom pulse | `z='1+0.2*abs(sin(PI*2*(in_time-1)))'` | zoom swells and falls |
| Glitch | `x='...+80*(random(1)-0.5)*gt(random(2),0.6)'` | random jumps on about 40% of frames |
| Mono flicker | `enable='between(t,1,3)*lt(mod((t-1)*5,1),0.5)'` | the *enable* itself flickers |

`(t-1)` is local time: seconds since the effect began, so the animation
always starts from the same phase however late in the reel the effect sits.

### Preview: one shader, one branch per effect

`effect.frag` gets the same four numbers (mode, local time, intensity,
speed) and redraws each effect on the GPU. Most are one or two lines:
change *where* to sample (shake, zoom, mirror, glitch), change *what* you
got (strobe, invert, mono, grain), or sample several times and combine
(blur, RGB split, VHS).

### Where the two renderers differ

These differences are real, deliberate, and good discussion material:

- **Light trails** needs earlier frames; ffmpeg's `lagfun` keeps them, but
  a fragment shader only sees the current frame. The preview fakes it with
  a glow.
- **Randomness.** ffmpeg's `random()` and the shader's `hash()` are
  different generators, so a glitch jumps at different moments in the
  preview and the export. Make it deterministic by hashing the frame number
  in both.
- **Overlapping effects.** Export applies all of them; the preview draws
  only the most recent. Chaining several shader layers would fix that, at
  a cost per frame.

### Things to try

- Add an effect: a new branch in `effect.frag`, a new entry in `kinds()`,
  a new `else if` in `exportChain()`. `effectsRenderThroughFfmpeg` will
  fail until the export actually changes the picture.
- Make the shake ease in and out by multiplying its amplitude by
  `sin(PI * local / duration)`.

### Tests

`effectsSaveAndClamp`, `effectGraphUsesEnable`, `effectsRenderThroughFfmpeg`
(every effect renders and changes the frame), `effectsWorkInTheEditor`
(Effects tab, lane, preview shader, Delete and undo) and `mcpManagesEffects`.

## 4. Body effects: a mask from a segmentation model

**Code:** `src/segment.cpp`, `src/bodyeffects.cpp`, `clipVideoChain()` and
`maskInputs()` in `src/exportgraph.cpp`, `src/shaders/body.frag`, the mask
player in `src/ClipSlot.qml`, `findPeople()` in `src/backend.cpp`.

### The idea

Blur only the background, drain its color, draw a glowing line around the
person: all of these need one extra piece of information per pixel, *is
this the person?* That answer is a **mask**, a grey image where white is
person and black is background. Once you have it, every body effect is the
same operation:

```
out = mix(changed_background, picture, mask)
```

### Getting the mask

`Segment::loadModel()` loads **PP-HumanSeg** from OpenCV Zoo (Apache-2.0,
about 6 MB, downloaded once into `~/.local/share/reelcut/models/`) with
OpenCV's DNN module. For each frame it:

1. scales the RGB frame to the model's 192×192 input and maps 0..255 to
   -1..1;
2. runs the network, which outputs two scores per pixel: *background* and
   *person*;
3. turns them into a probability with a softmax. For two classes that is
   the logistic of their difference, `1 / (1 + exp(bg - person))`, so the
   edge of the mask is soft instead of jagged.

`Segment::makeMask()` streams the clip through that loop. One ffmpeg
process decodes raw frames into a pipe, the model fills in the mask, and a
second ffmpeg process encodes the masks into a small 270×480 grey video.
The decoder runs **the same `clipVideoChain()` the export uses** (trim,
speed, framing, photo motion), so mask frame *n* is exactly picture frame
*n*. If it read the raw file instead, the mask would be offset by the trim
and stretched by the framing.

Masks are cached by a hash of everything that changes the clip's frames
(file, size, modification time, trim, speed, zoom, pan, motion). Trim the
clip and the hash changes, so a new mask is made; set it back and the old
one is reused.

### Using the mask on export

The mask video becomes an extra ffmpeg input, and `maskedmerge` picks each
pixel from its first input where the mask is black and from its second
where it is white:

```
[mask]fps=30,settb=1/30,scale=1080:1920,format=gbrp[m];
[clip]format=gbrp,split[pic][back];
[back]hue=s=0,format=gbrp[bg];          <- Color Pop: grey background
[bg][pic][m]maskedmerge,format=yuv420p,fps=30,settb=1/30[v0]
```

Two lessons from building this, both caught by tests:

- **Format matters.** `maskedmerge` weights each color plane by the same
  plane of the mask. In planar RGB (`gbrp`) the mask's grey is in all three
  planes. In YUV, the mask's color planes are mid-grey, so every pixel
  comes out half and half. Filters like `hue` only work in YUV and quietly
  pull the whole merge into YUV unless every input is forced back to
  `gbrp`.
- **Clocks matter.** The merged clip inherits the mask video's timebase.
  `xfade` refuses to join clips on different timebases, so a body effect
  next to a transition failed until both sides were put on `1/30`.

The neon outline uses the mask differently: `edgedetect` finds where the
mask changes, `gblur` softens that line into a glow, and the glow becomes
the mask for merging a solid neon color over the picture.

### Preview

Each `ClipSlot` plays the clip's mask video in a second, hidden player, in
step with the clip. `body.frag` reads it as a texture and does the same
mix the export does. The neon outline finds the edge by comparing the mask
a few pixels to each side: where it changes fastest is the edge.

### Things to notice

- **The model sees 192×192.** A 9:16 frame is squeezed into a square, and
  fine detail like hair is lost. That's why the mask is soft, and why the
  background blur's edge is forgiving. A larger model is sharper and
  slower.
- **Speed.** One model run per frame on the CPU. Watch the progress bar
  and compare with the clip's length; that's the case for caching.
- **It can be wrong.** A segmentation model guesses. Try a clip with two
  people, a mirror, or a poster of a person on the wall.

### Tests

`bodyEffectGraph` (graph and cache key), `bodyEffectMaskRendersThroughFfmpeg`
(a fake model that calls the left half "the person": the mask has one frame
per picture frame, Color Pop greys only the right half, a body effect sits
next to a transition, and all five effects render), `personModelRuns` (the
real model, when downloaded) and `mcpSetsABodyEffect`.

## 5. Fisheye text: a lens over a text layer

**Where:** Effects tab → *Fisheye Text*, or set *Style: Fisheye* on any text in
the Text tab. Code: `src/fisheyetext.cpp` (export), `src/shaders/fisheye.frag`
(preview), the `ShaderEffectSource` + `ShaderEffect` pair in
`src/CaptionLayer.qml`.

**The idea.** Draw the text flat, then look at it through a lens. The lens is
an ellipse 1.45 times the size of the text block (so the corners of the text
fit inside it). For every output pixel at normalized radius `r` from the
centre, read the flat text at radius

    r * (1 - k (1 - r²))

With `k = 0.36` the centre is magnified `1 / (1 - k) ≈ 1.56×`, and at the rim
(`r = 1`) the factor is exactly 1, so the edge of the lens has no seam. This is
*inverse mapping*: you ask "where does this output pixel come from?", never
"where does this input pixel go?", which is why there are no holes.

An earlier try used `r^1.3` as the curve. It looks similar on paper, but its
slope at `r = 0` is zero, which means *infinite* magnification at the very
centre, and the middle letter blew up. Students can try both in the shader
and see why the derivative at 0 matters.

**Why the export draws an image.** Plain text goes through an `.ass`
subtitle file, which ffmpeg's `ass` filter burns in. ASS can scale and move
text but can't bend it, so for fisheye text the app renders the warped text
into a transparent 1080×1920 PNG with QPainter (same formula), and the export
lays it over the video with `overlay=enable='between(t,start,end)'`, the same
time switch the video effects use.

**Preview vs export.** Same formula, same font (Liberation Serif), same
shadow offset. The preview's shadow blur is a 24-tap ring, the export's is
two box-blur passes, so the shadow edge differs very slightly. Images can't
"pop", so a fisheye text's Pop entrance is a fade, in both places.

**Try it:**
- Set *Bulge* to 0 and to 100% and compare.
- Change `k` into a negative number in `fisheye.frag`: the lens pinches
  instead of bulging.
- Replace the ellipse with a circle (use one radius for both axes) and see
  how long lines stop bending evenly.

## 6. Color wheels and text colors

**Where:** Clip tab → *Color wheels (this clip)*, Look tab → *Color wheels
(whole reel)*, Text tab → Fill / Outline / Shadow / Box on each text. Code:
`src/colorgrade.cpp` (the maths and the .cube), `src/shaders/grade.frag`
(preview), `src/ColorWheel.qml`, `src/GradePanel.qml`, `src/ColorPicker.qml`.

**Primary wheels, the DaVinci layout.** Each wheel is a puck plus a master
ring. The puck's direction picks a hue (red at the top, then clockwise to
green and blue) and its distance how far to push toward it; that becomes an
RGB push that sums to zero, so it tints without brightening. The master moves
R, G and B together. Per channel, in order:

    x = x * wb + offset          temperature/tint, then Offset (everything)
    x = lift + x * (1 - lift)    Lift: blacks move, white stays at 1
    x = x * gain                 Gain: whites move, black stays at 0
    x = x ^ gexp                 Gamma: the middle bends, 0 and 1 stay
    x = (x - pivot) * contrast + pivot
    then hue rotation around the grey axis, then saturation around luma

Each step keeps something fixed (black, white, or the pivot), which is
why colorists can stack them without fighting. A good exercise: grade a
grey ramp and plot the curve each wheel produces.

**One formula, two places.** The preview runs it per pixel in a shader. The
export can't run GLSL, so the same C++ function is sampled at 33×33×33
colors into a `.cube` file and ffmpeg's `lut3d` interpolates between them
(tetrahedral interpolation, as in section 1). The clip's grade goes right
after the clip is framed; the reel's goes after the look sliders, before the
filter library.

**Text colors.** Each part is *Default* (the style's own color),
*Transparent*, or any color with opacity, stored as `#AARRGGBB`. Plain text
becomes ASS override tags: `\1c&HBBGGRR&` for the fill with `\1a&HAA&`,
where ASS alpha counts *up* to transparent (00 is solid). A box switches
the line to a style with `BorderStyle=3`, which draws an opaque box in the
outline color, which is why a box replaces the outline on plain text.
Fisheye text is drawn by QPainter, so it can have both.

**Try it:**
- Push the Gain puck toward blue and the Lift puck toward orange: the classic
  "teal and orange" look, done with two wheels.
- Set Contrast to 1.5 and move the Pivot: see which tones stay put.
- In `colorgrade.cpp` change the LUT size from 33 to 5 and export a sky
  gradient. Where does the banding come from?
