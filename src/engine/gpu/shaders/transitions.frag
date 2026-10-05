// SPDX-License-Identifier: GPL-3.0-or-later
// GPU path of the transitions (SPEC §5.11bis, 1bis rule 1): the same formulas as the CPU reference kernels of
// src/fx/Transition.cpp, line by line, so that both give the same picture within tolerance (tst_gputransitions).
// GLSL 1.00 / 1.10 only (OpenGL 2.1, OpenGL ES 2.0): no integers beyond small loop counters, no extensions. The code
// prepends "#version …" and "#define K_<kernel name>" (fx::transitionKindName): one small program per transition.
//
// Coordinates are the CPU's: (x, y) the integer pixel, origin at the top left. Textures hold premultiplied RGBA; the
// pseudo-random values of the CPU (integer hash) come in the `noise` texture, computed by the CPU (16 bits in R, G).

#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

uniform sampler2D texA;
uniform sampler2D texB;
uniform sampler2D noise;
uniform vec2 size;
uniform float t;
uniform float soft;
uniform vec4 customColor; // premultiplied

const float pi = 3.14159265358979323846;

// sample(image, x, y) of the CPU: bilinear at pixel coordinates, clamped to the edges.
vec4 sampleA(float x, float y) { return texture2D(texA, vec2(x / size.x, y / size.y)); }
vec4 sampleB(float x, float y) { return texture2D(texB, vec2(x / size.x, y / size.y)); }

float smoothC(float e0, float e1, float v)
{
    if (e1 <= e0)
        return v < e0 ? 0.0 : 1.0;
    float u = clamp((v - e0) / (e1 - e0), 0.0, 1.0);
    return u * u * (3.0 - 2.0 * u);
}

// pseudoRandom(ix, iy) of the CPU, for integer arguments.
float rnd(float ix, float iy)
{
    vec4 n = texture2D(noise, vec2((ix + 0.5) / size.x, (iy + 0.5) / size.y));
    return (floor(n.r * 255.0 + 0.5) * 256.0 + floor(n.g * 255.0 + 0.5)) / 65535.0;
}

// Integer division of a non-negative integer (the 0.5 keeps clear of rounding at exact multiples).
float idiv(float v, float n) { return floor((v + 0.5) / n); }
float truncC(float v) { return v < 0.0 ? -floor(-v) : floor(v); }
float fmodC(float a, float b) { return a - b * truncC(a / b); }
vec4 gray(vec4 p)
{
    float l = 0.2126 * p.r + 0.7152 * p.g + 0.0722 * p.b;
    return vec4(l, l, l, p.a);
}
// Rotation/scale about the centre (cosA, sinA already divided or multiplied by the scale).
vec2 turn(float x, float y, float cx, float cy, float cosA, float sinA)
{
    return vec2((x + 0.5 - cx) * cosA - (y + 0.5 - cy) * sinA + cx, (x + 0.5 - cx) * sinA + (y + 0.5 - cy) * cosA + cy);
}

void main()
{
    float w = size.x;
    float h = size.y;
    float x = floor(gl_FragCoord.x);
    float y = h - 1.0 - floor(gl_FragCoord.y);
    float cx = w / 2.0;
    float cy = h / 2.0;
    float maxRadius = sqrt(cx * cx + cy * cy);
    vec4 pixA = texture2D(texA, vec2((x + 0.5) / w, (y + 0.5) / h));
    vec4 pixB = texture2D(texB, vec2((x + 0.5) / w, (y + 0.5) / h));
    vec4 black = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 white = vec4(1.0);
    float edge = t * (1.0 + 2.0 * soft) - soft;
    float dxc = x + 0.5 - cx;
    float dyc = y + 0.5 - cy;
    float radius = sqrt(dxc * dxc + dyc * dyc);
    vec4 p = pixA;

    // ---- 1. Base ------------------------------------------------------------------------------------------------
#if defined(K_dissolve)
    p = mix(pixA, pixB, t);
#elif defined(K_dipToBlack)
    p = t < 0.5 ? mix(pixA, black, t * 2.0) : mix(black, pixB, (t - 0.5) * 2.0);
#elif defined(K_dipToWhite)
    p = t < 0.5 ? mix(pixA, white, t * 2.0) : mix(white, pixB, (t - 0.5) * 2.0);
#elif defined(K_dipToColor)
    p = t < 0.5 ? mix(pixA, customColor, t * 2.0) : mix(customColor, pixB, (t - 0.5) * 2.0);
#elif defined(K_fadeGrayscale)
    vec4 gA = gray(pixA);
    vec4 gB = gray(pixB);
    p = t < 0.5 ? mix(mix(pixA, gA, t * 2.0), gB, t) : mix(gA, mix(gB, pixB, (t - 0.5) * 2.0), t);
#elif defined(K_exposureFlash)
    float flash = 1.0 + 2.5 * (1.0 - abs(2.0 * t - 1.0));
    vec4 mixed = mix(pixA, pixB, t);
    p = vec4(min(vec3(mixed.a), mixed.rgb * flash), mixed.a);
#elif defined(K_lumaFade)
    float lum = 0.2126 * pixA.r + 0.7152 * pixA.g + 0.0722 * pixA.b;
    p = mix(pixA, pixB, smoothC(edge - soft, edge + soft, lum));
#elif defined(K_additive)
    float alpha = min(1.0, pixA.a * (1.0 - t) + pixB.a * t);
    float boost = 0.25 * (1.0 - abs(2.0 * t - 1.0));
    p = vec4(min(vec3(alpha), pixA.rgb * (1.0 - t) + pixB.rgb * t + (pixA.rgb + pixB.rgb) * boost), alpha);
#elif defined(K_subtract)
    vec4 mixed = mix(pixA, pixB, t);
    float sub = 0.3 * sin(t * pi);
    p = vec4(max(vec3(0.0), mixed.rgb - sub * mixed.a), mixed.a);
#elif defined(K_multiply)
    float d = max(0.01, pixA.a * pixB.a);
    vec4 mult = vec4(pixA.rgb * pixB.rgb / d, min(pixA.a, pixB.a));
    p = t < 0.5 ? mix(pixA, mult, t * 2.0) : mix(mult, pixB, (t - 0.5) * 2.0);

    // ---- 2. Motion ----------------------------------------------------------------------------------------------
#elif defined(K_slideLeft) || defined(K_slideRight) || defined(K_slideUp) || defined(K_slideDown) || defined(K_slideTopLeft) || defined(K_slideTopRight) || defined(K_slideBottomLeft) || defined(K_slideBottomRight)
    float dx = 0.0;
    float dy = 0.0;
#if defined(K_slideLeft)
    dx = (1.0 - t) * w;
#elif defined(K_slideRight)
    dx = -(1.0 - t) * w;
#elif defined(K_slideUp)
    dy = (1.0 - t) * h;
#elif defined(K_slideDown)
    dy = -(1.0 - t) * h;
#elif defined(K_slideTopLeft)
    dx = (1.0 - t) * w; dy = (1.0 - t) * h;
#elif defined(K_slideTopRight)
    dx = -(1.0 - t) * w; dy = (1.0 - t) * h;
#elif defined(K_slideBottomLeft)
    dx = (1.0 - t) * w; dy = -(1.0 - t) * h;
#else
    dx = -(1.0 - t) * w; dy = -(1.0 - t) * h;
#endif
    float bx = x + 0.5 - dx;
    float by = y + 0.5 - dy;
    if (bx >= 0.0 && bx < w && by >= 0.0 && by < h) {
        vec4 sampB = sampleB(bx, by);
        p = sampB.a >= 1.0 ? sampB : mix(pixA, sampB, sampB.a);
    } else {
        p = pixA;
    }
#elif defined(K_pushLeft) || defined(K_pushRight) || defined(K_pushUp) || defined(K_pushDown) || defined(K_pushTopLeft) || defined(K_pushTopRight) || defined(K_pushBottomLeft) || defined(K_pushBottomRight)
    float dx = 0.0;
    float dy = 0.0;
#if defined(K_pushLeft)
    dx = (1.0 - t) * w;
#elif defined(K_pushRight)
    dx = -(1.0 - t) * w;
#elif defined(K_pushUp)
    dy = (1.0 - t) * h;
#elif defined(K_pushDown)
    dy = -(1.0 - t) * h;
#elif defined(K_pushTopLeft)
    dx = (1.0 - t) * w; dy = (1.0 - t) * h;
#elif defined(K_pushTopRight)
    dx = -(1.0 - t) * w; dy = (1.0 - t) * h;
#elif defined(K_pushBottomLeft)
    dx = (1.0 - t) * w; dy = -(1.0 - t) * h;
#else
    dx = -(1.0 - t) * w; dy = -(1.0 - t) * h;
#endif
    float bx = x + 0.5 - dx;
    float by = y + 0.5 - dy;
    if (bx >= 0.0 && bx < w && by >= 0.0 && by < h) {
        p = sampleB(bx, by);
    } else {
        float signX = dx > 0.0 ? 1.0 : (dx < 0.0 ? -1.0 : 0.0);
        float signY = dy > 0.0 ? 1.0 : (dy < 0.0 ? -1.0 : 0.0);
        p = sampleA(x + 0.5 + t * w * signX, y + 0.5 + t * h * signY);
    }

    // ---- 3. Zoom & Spin -----------------------------------------------------------------------------------------
#elif defined(K_zoomIn)
    float zoom = 1.0 + t * 2.0;
    p = mix(sampleA(cx + dxc / zoom, cy + dyc / zoom), pixB, smoothC(0.2, 1.0, t));
#elif defined(K_zoomOut)
    float zoom = 1.0 + (1.0 - t) * 2.0;
    p = mix(pixA, sampleB(cx + dxc / zoom, cy + dyc / zoom), smoothC(0.0, 0.8, t));
#elif defined(K_crossZoom)
    float zA = 1.0 + t * 1.5;
    float zB = 1.0 + (1.0 - t) * 1.5;
    p = mix(sampleA(cx + dxc / zA, cy + dyc / zA), sampleB(cx + dxc / zB, cy + dyc / zB), t);
#elif defined(K_warpZoom)
    float warp = 1.0 + t * (radius / maxRadius) * 3.0;
    p = mix(sampleA(cx + dxc / warp, cy + dyc / warp), pixB, smoothC(0.3, 1.0, t));
#elif defined(K_spinCw) || defined(K_spinCcw)
#if defined(K_spinCw)
    float angle = t * 2.0 * pi;
#else
    float angle = -t * 2.0 * pi;
#endif
    vec2 r = turn(x, y, cx, cy, cos(angle), sin(angle));
    p = t < 0.5 ? mix(sampleA(r.x, r.y), pixB, t * 2.0) : mix(pixA, sampleB(r.x, r.y), (t - 0.5) * 2.0);
#elif defined(K_spinZoomIn) || defined(K_spinZoomOut)
#if defined(K_spinZoomIn)
    float factor = 1.0 + t * 2.0;
#else
    float factor = 1.0 + (1.0 - t) * 2.0;
#endif
    float angle = t * pi;
    vec2 r = turn(x, y, cx, cy, cos(angle) / factor, sin(angle) / factor);
    p = mix(sampleA(r.x, r.y), pixB, t);
#elif defined(K_rotateScaleCw) || defined(K_rotateScaleCcw)
#if defined(K_rotateScaleCw)
    float angle = t * pi * 0.5;
#else
    float angle = -t * pi * 0.5;
#endif
    float scale = 1.0 + t * 0.5;
    vec2 r = turn(x, y, cx, cy, cos(angle) / scale, sin(angle) / scale);
    p = mix(sampleA(r.x, r.y), pixB, t);
#elif defined(K_dollyZoomIn) || defined(K_dollyZoomOut)
    float dd = radius / maxRadius;
#if defined(K_dollyZoomIn)
    float f = 1.0 + t * (1.0 - dd) * 2.0;
#else
    float f = 1.0 + (1.0 - t) * (1.0 - dd) * 2.0;
#endif
    p = mix(sampleA(cx + dxc / f, cy + dyc / f), pixB, t);

    // ---- 4. Wipes & Bands ---------------------------------------------------------------------------------------
#elif defined(K_wipeLeft) || defined(K_wipeRight) || defined(K_wipeUp) || defined(K_wipeDown) || defined(K_wipeTopLeft) || defined(K_wipeTopRight) || defined(K_wipeBottomLeft) || defined(K_wipeBottomRight)
    float u = (x + 0.5) / w;
    float v = (y + 0.5) / h;
#if defined(K_wipeLeft)
    float s = 1.0 - u;
#elif defined(K_wipeRight)
    float s = u;
#elif defined(K_wipeUp)
    float s = 1.0 - v;
#elif defined(K_wipeDown)
    float s = v;
#elif defined(K_wipeTopLeft)
    float s = 1.0 - 0.5 * (u + v);
#elif defined(K_wipeTopRight)
    float s = 0.5 * (u + 1.0 - v);
#elif defined(K_wipeBottomLeft)
    float s = 0.5 * (1.0 - u + v);
#else
    float s = 0.5 * (u + v);
#endif
    p = mix(pixA, pixB, 1.0 - smoothC(edge - soft, edge + soft, s));
#elif defined(K_splitHorizontal)
    p = mix(pixA, pixB, 1.0 - smoothC(edge - soft, edge + soft, abs(dyc / cy)));
#elif defined(K_splitVertical)
    p = mix(pixA, pixB, 1.0 - smoothC(edge - soft, edge + soft, abs(dxc / cx)));
#elif defined(K_barnDoorHorizontal)
    p = mix(pixA, pixB, 1.0 - smoothC(edge - soft, edge + soft, 1.0 - abs(dyc / cy)));
#elif defined(K_barnDoorVertical)
    p = mix(pixA, pixB, 1.0 - smoothC(edge - soft, edge + soft, 1.0 - abs(dxc / cx)));
#elif defined(K_blindsHorizontal)
    p = mix(pixA, pixB, 1.0 - smoothC(edge - soft, edge + soft, fract((y + 0.5) / 16.0)));
#elif defined(K_blindsVertical)
    p = mix(pixA, pixB, 1.0 - smoothC(edge - soft, edge + soft, fract((x + 0.5) / 16.0)));
#elif defined(K_checkerboard)
    float cell = idiv(x, 24.0) + idiv(y, 24.0);
    bool odd = cell - 2.0 * floor((cell + 0.5) / 2.0) > 0.5;
    p = t < 0.5 ? (odd ? mix(pixA, pixB, t * 2.0) : pixA) : (odd ? pixB : mix(pixA, pixB, (t - 0.5) * 2.0));
#elif defined(K_mosaicWipe)
    float r = rnd(idiv(x, 16.0), idiv(y, 16.0));
    p = mix(pixA, pixB, smoothC(r - 0.1, r + 0.1, t));

    // ---- 5. Shapes & Geometry -----------------------------------------------------------------------------------
#elif defined(K_iris)
    p = mix(pixA, pixB, 1.0 - smoothC(edge - soft, edge + soft, radius / maxRadius));
#elif defined(K_irisDiamond)
    float d = (abs(dxc) / cx + abs(dyc) / cy) * 0.5;
    p = mix(pixA, pixB, 1.0 - smoothC(edge - soft, edge + soft, d));
#elif defined(K_irisStar)
    float angle = atan(dyc, dxc);
    float dist = (radius / maxRadius) / (0.7 + 0.3 * cos(5.0 * angle));
    p = mix(pixA, pixB, 1.0 - smoothC(edge - soft, edge + soft, dist));
#elif defined(K_irisHeart)
    float nx = dxc / cx;
    float ny = -dyc / cy;
    float dist = sqrt(nx * nx + ny * ny) - 0.3 * sqrt(abs(nx));
    float heartEdge = t * 1.5 - 0.2;
    p = mix(pixA, pixB, smoothC(heartEdge - soft, heartEdge + soft, 1.0 - dist));
#elif defined(K_irisTriangle)
    float nx = abs(dxc / cx);
    float ny = dyc / cy;
    float d = max(nx * 0.866025 + ny * 0.5, -ny);
    p = mix(pixA, pixB, 1.0 - smoothC(edge - soft, edge + soft, d));
#elif defined(K_clock)
    float angle = atan(dxc, cy - (y + 0.5)) / (2.0 * pi);
    if (angle < 0.0)
        angle += 1.0;
    p = mix(pixA, pixB, 1.0 - smoothC(edge - soft, edge + soft, angle));
#elif defined(K_clockCounter)
    float angle = atan(cx - (x + 0.5), cy - (y + 0.5)) / (2.0 * pi);
    if (angle < 0.0)
        angle += 1.0;
    p = mix(pixA, pixB, 1.0 - smoothC(edge - soft, edge + soft, angle));
#elif defined(K_clockDual)
    float angle = abs(atan(dxc, cy - (y + 0.5))) / pi;
    p = mix(pixA, pixB, 1.0 - smoothC(edge - soft, edge + soft, angle));
#elif defined(K_hexagonGrid)
    float r = rnd(floor((x + 0.5) / 28.0), floor((y + 0.5) / 24.0));
    p = mix(pixA, pixB, smoothC(r - 0.15, r + 0.15, t));
#elif defined(K_polygonOpen)
    float nx = abs(dxc) / cx;
    float ny = abs(dyc) / cy;
    float poly = max(max(nx, ny), (nx + ny) * 0.7071);
    p = mix(pixA, pixB, 1.0 - smoothC(edge - soft, edge + soft, poly));
#elif defined(K_diagonalSliceLeft) || defined(K_diagonalSliceRight)
#if defined(K_diagonalSliceLeft)
    float stripe = truncC((x + y) / 32.0);
#else
    float stripe = truncC((x - y) / 32.0);
#endif
    float even = abs(stripe) - 2.0 * floor((abs(stripe) + 0.5) / 2.0);
    float offset = (even < 0.5 ? 1.0 : -1.0) * (1.0 - t) * w;
    float bx = x + 0.5 - offset;
    p = (bx >= 0.0 && bx < w) ? mix(pixA, sampleB(bx, y + 0.5), t) : pixA;

    // ---- 6. Blurs & Camera --------------------------------------------------------------------------------------
#elif defined(K_blurDissolve)
    float bd = sin(t * pi) * 12.0;
    vec4 bA = mix(mix(sampleA(x - bd, y), sampleA(x + bd, y), 0.5), mix(sampleA(x, y - bd), sampleA(x, y + bd), 0.5), 0.5);
    vec4 bB = mix(mix(sampleB(x - bd, y), sampleB(x + bd, y), 0.5), mix(sampleB(x, y - bd), sampleB(x, y + bd), 0.5), 0.5);
    p = mix(bA, bB, t);
#elif defined(K_directionalBlurLeft) || defined(K_directionalBlurRight)
#if defined(K_directionalBlurLeft)
    float bd = -sin(t * pi) * 20.0;
#else
    float bd = sin(t * pi) * 20.0;
#endif
    p = mix(mix(sampleA(x - bd, y), sampleA(x + bd, y), 0.5), mix(sampleB(x - bd, y), sampleB(x + bd, y), 0.5), t);
#elif defined(K_radialBlur)
    float bd = sin(t * pi) * 0.15;
    vec4 bA = mix(sampleA(cx + (x - cx) * (1.0 - bd), cy + (y - cy) * (1.0 - bd)), sampleA(cx + (x - cx) * (1.0 + bd), cy + (y - cy) * (1.0 + bd)), 0.5);
    vec4 bB = mix(sampleB(cx + (x - cx) * (1.0 - bd), cy + (y - cy) * (1.0 - bd)), sampleB(cx + (x - cx) * (1.0 + bd), cy + (y - cy) * (1.0 + bd)), 0.5);
    p = mix(bA, bB, t);
#elif defined(K_tiltShift)
    float bd = abs((y - cy) / cy) * sin(t * pi) * 10.0;
    p = mix(mix(sampleA(x, y - bd), sampleA(x, y + bd), 0.5), mix(sampleB(x, y - bd), sampleB(x, y + bd), 0.5), t);
#elif defined(K_whipPanLeft) || defined(K_whipPanRight) || defined(K_whipPanUp) || defined(K_whipPanDown)
    float blur = sin(t * pi) * 30.0;
    float ox = 0.0;
    float oy = 0.0;
#if defined(K_whipPanLeft)
    ox = (1.0 - t) * w;
#elif defined(K_whipPanRight)
    ox = -(1.0 - t) * w;
#elif defined(K_whipPanUp)
    oy = (1.0 - t) * h;
#else
    oy = -(1.0 - t) * h;
#endif
    vec4 sA = mix(sampleA(x + 0.5 - blur, y + 0.5), sampleA(x + 0.5 + blur, y + 0.5), 0.5);
    vec4 sB = mix(sampleB(x + 0.5 - ox - blur, y + 0.5 - oy), sampleB(x + 0.5 - ox + blur, y + 0.5 - oy), 0.5);
    p = mix(sA, sB, t);
#elif defined(K_cameraShutter)
    float blade = 1.0 - abs(2.0 * t - 1.0);
    p = radius / maxRadius > (1.0 - blade * 0.8) ? black : mix(pixA, pixB, t);

    // ---- 7. Glitch & Digital ------------------------------------------------------------------------------------
#elif defined(K_glitchRgb)
    float shift = sin(t * pi) * 16.0;
    vec4 rS = mix(sampleA(x + shift, y), sampleB(x + shift, y), t);
    vec4 bS = mix(sampleA(x - shift, y), sampleB(x - shift, y), t);
    vec4 center = mix(pixA, pixB, t);
    p = vec4(rS.r, center.g, bS.b, center.a);
#elif defined(K_pixelate)
    float bs = max(1.0, floor(sin(t * pi) * 20.0));
    float px = floor((x + 0.5) / bs) * bs + floor(bs / 2.0);
    float py = floor((y + 0.5) / bs) * bs + floor(bs / 2.0);
    p = mix(sampleA(px, py), sampleB(px, py), t);
#elif defined(K_scanlineTear)
    bool line = y - 4.0 * floor((y + 0.5) / 4.0) < 0.5;
    float shift = line ? sin(t * pi) * 15.0 : 0.0;
    p = mix(sampleA(x + shift, y), sampleB(x - shift, y), t);
#elif defined(K_vhsDistortion)
    float n = (rnd(x, y) - 0.5) * sin(t * pi) * 0.2;
    float wave = sin(y * 0.1 + t * 10.0) * sin(t * pi) * 8.0;
    vec4 samp = mix(sampleA(x + wave, y), sampleB(x, y), t);
    p = vec4(clamp(samp.rgb + n, vec3(0.0), vec3(samp.a)), samp.a);
#elif defined(K_blockDissolve)
    p = t > rnd(idiv(x, 20.0), idiv(y, 20.0)) ? pixB : pixA;
#elif defined(K_digitalNoise)
    float n = rnd(x, y);
    p = mix(pixA, pixB, smoothC(n - 0.2, n + 0.2, t));
#elif defined(K_jitter)
    float jx = (rnd(idiv(x, 4.0), idiv(y, 4.0)) - 0.5) * sin(t * pi) * 12.0;
    float jy = (rnd(idiv(y, 4.0), idiv(x, 4.0)) - 0.5) * sin(t * pi) * 12.0;
    p = mix(sampleA(x + jx, y + jy), sampleB(x - jx, y - jy), t);
#elif defined(K_dataCorruption)
    float r = rnd(idiv(x, 32.0), idiv(y, 32.0));
    p = (sin(t * pi) > 0.5 && r > 0.7) ? vec4(pixA.g, pixA.b, pixA.r, pixA.a) : mix(pixA, pixB, t);
#elif defined(K_screenTear)
    p = y < h * t ? pixB : pixA;
#elif defined(K_lumaGlitch)
    float lum = 0.2126 * pixA.r + 0.7152 * pixA.g + 0.0722 * pixA.b;
    float shift = lum > 0.6 ? sin(t * pi) * 25.0 : 0.0;
    p = mix(sampleA(x + shift, y), pixB, t);

    // ---- 8. Lights & Leaks --------------------------------------------------------------------------------------
#elif defined(K_lightLeakWarm)
    float leak = sin(t * pi) * 0.8;
    vec4 mixed = mix(pixA, pixB, t);
    p = vec4(min(vec3(mixed.a), mixed.rgb + leak * vec3(0.9, 0.5, 0.2) * mixed.a), mixed.a);
#elif defined(K_lightLeakCool)
    float leak = sin(t * pi) * 0.8;
    vec4 mixed = mix(pixA, pixB, t);
    p = vec4(min(vec3(mixed.a), mixed.rgb + leak * vec3(0.2, 0.6, 0.9) * mixed.a), mixed.a);
#elif defined(K_lensGlow)
    float glow = sin(t * pi) * 1.5;
    vec4 mixed = mix(pixA, pixB, t);
    p = vec4(min(vec3(mixed.a), mixed.rgb * (1.0 + glow)), mixed.a);
#elif defined(K_filmBurn)
    float burnEdge = rnd(idiv(x, 8.0), idiv(y, 8.0)) * sin(t * pi);
    vec4 mixed = mix(pixA, pixB, t);
    p = burnEdge > 0.4 ? vec4(mixed.a, mixed.a * 0.8, mixed.a * 0.3, mixed.a) : mixed;
#elif defined(K_rainbowFlash)
    float flash = sin(t * pi);
    float hue = fract((x + 0.5) / w + t);
    vec4 mixed = mix(pixA, pixB, t);
    p = vec4(min(mixed.a, mixed.r + flash * abs(sin(hue * pi))), min(mixed.a, mixed.g + flash * abs(sin((hue + 0.33) * pi))),
             min(mixed.a, mixed.b + flash * abs(sin((hue + 0.66) * pi))), mixed.a);
#elif defined(K_colorInvert)
    float inv = sin(t * pi);
    vec4 mixed = mix(pixA, pixB, t);
    p = vec4(mixed.rgb + (vec3(mixed.a) - mixed.rgb - mixed.rgb) * inv, mixed.a);
#elif defined(K_solarize)
    float sol = sin(t * pi);
    vec4 mixed = mix(pixA, pixB, t);
    vec3 norm = mixed.a > 0.0 ? mixed.rgb / mixed.a : vec3(0.0);
    vec3 folded = vec3(norm.r > 0.5 ? (1.0 - norm.r) * 2.0 : norm.r * 2.0, norm.g > 0.5 ? (1.0 - norm.g) * 2.0 : norm.g * 2.0,
                       norm.b > 0.5 ? (1.0 - norm.b) * 2.0 : norm.b * 2.0);
    p = vec4(folded * mixed.a * sol + mixed.rgb * (1.0 - sol), mixed.a);
#elif defined(K_neonFlash)
    float neon = sin(t * pi) * 0.6;
    vec4 mixed = mix(pixA, pixB, t);
    p = vec4(min(vec3(mixed.a), mixed.rgb + neon * vec3(0.1, 0.9, 0.9) * mixed.a), mixed.a);
#elif defined(K_strobe)
    float phase = floor(t * 12.0);
    bool flash = phase - 2.0 * floor((phase + 0.5) / 2.0) > 0.5;
    p = flash ? (t < 0.5 ? white : black) : mix(pixA, pixB, t);
#elif defined(K_lumaGlow)
    float glow = sin(t * pi) * 0.7;
    vec4 mixed = mix(pixA, pixB, t);
    float lum = 0.2126 * mixed.r + 0.7152 * mixed.g + 0.0722 * mixed.b;
    p = vec4(min(vec3(mixed.a), mixed.rgb + lum * glow), mixed.a);

    // ---- 9. Distortions -----------------------------------------------------------------------------------------
#elif defined(K_waveHorizontal)
    float wave = sin(y * 0.05 + t * 4.0 * pi) * sin(t * pi) * 20.0;
    p = mix(sampleA(x + wave, y), sampleB(x - wave, y), t);
#elif defined(K_waveVertical)
    float wave = sin(x * 0.05 + t * 4.0 * pi) * sin(t * pi) * 20.0;
    p = mix(sampleA(x, y + wave), sampleB(x, y - wave), t);
#elif defined(K_rippleWater)
    float ripple = sin(radius * 0.1 - t * 10.0) * sin(t * pi) * 10.0;
    p = mix(sampleA(x + ripple, y + ripple), sampleB(x - ripple, y - ripple), t);
#elif defined(K_vortex)
    float angle = (1.0 - radius / maxRadius) * sin(t * pi) * pi;
    vec2 r = turn(x, y, cx, cy, cos(angle), sin(angle));
    p = mix(sampleA(r.x, r.y), sampleB(x, y), t);
#elif defined(K_shockwave)
    float ring = t * maxRadius;
    float gap = abs(radius - ring);
    float wave = sin(gap * 0.1) * max(0.0, 1.0 - gap / 30.0) * 15.0;
    p = mix(sampleA(x + wave, y + wave), pixB, t);
#elif defined(K_pinch)
    float f = 1.0 + sin(t * pi) * (1.0 - radius / maxRadius) * 0.8;
    p = mix(sampleA(cx + dxc * f, cy + dyc * f), pixB, t);
#elif defined(K_pinchTwist)
    float rr = radius / maxRadius;
    float angle = (1.0 - rr) * sin(t * pi) * pi * 0.5;
    float scale = 1.0 + sin(t * pi) * (1.0 - rr) * 0.5;
    vec2 r = turn(x, y, cx, cy, cos(angle) * scale, sin(angle) * scale);
    p = mix(sampleA(r.x, r.y), pixB, t);
#elif defined(K_sphereLens)
    float nx = dxc / cx;
    float ny = dyc / cy;
    float lens = 1.0 + sin(t * pi) * (1.0 - min(1.0, nx * nx + ny * ny)) * 0.5;
    p = mix(sampleA(cx + dxc / lens, cy + dyc / lens), pixB, t);
#elif defined(K_swirl)
    float angle = radius * 0.02 * sin(t * pi);
    vec2 r = turn(x, y, cx, cy, cos(angle), sin(angle));
    p = mix(sampleA(r.x, r.y), pixB, t);
#elif defined(K_kaleidoscope)
    float angle = atan(dyc, dxc);
    float segment = pi / 3.0;
    float symAngle = abs(fmodC(angle, segment) - segment * 0.5);
    p = mix(mix(pixA, sampleA(cx + radius * cos(symAngle), cy + radius * sin(symAngle)), sin(t * pi)), pixB, t);

    // ---- 10. 3D -------------------------------------------------------------------------------------------------
#elif defined(K_cubeLeft) || defined(K_cubeRight)
#if defined(K_cubeLeft)
    float dir = 1.0;
#else
    float dir = -1.0;
#endif
    float rot = t * 0.5 * pi;
    if (t < 0.5) {
        float scaleX = cos(rot);
        float offset = dir * (1.0 - scaleX) * w * 0.5;
        p = sampleA(cx + (x + 0.5 - cx - offset) / max(0.01, scaleX), y + 0.5);
        p.rgb *= 1.0 - t * 0.5;
    } else {
        float scaleX = sin(rot);
        float offset = -dir * (1.0 - scaleX) * w * 0.5;
        p = sampleB(cx + (x + 0.5 - cx - offset) / max(0.01, scaleX), y + 0.5);
        p.rgb *= 0.5 + (t - 0.5);
    }
#elif defined(K_cubeUp) || defined(K_cubeDown)
#if defined(K_cubeUp)
    float dir = 1.0;
#else
    float dir = -1.0;
#endif
    float rot = t * 0.5 * pi;
    if (t < 0.5) {
        float scaleY = cos(rot);
        float offset = dir * (1.0 - scaleY) * h * 0.5;
        p = sampleA(x + 0.5, cy + (y + 0.5 - cy - offset) / max(0.01, scaleY));
        p.rgb *= 1.0 - t * 0.5;
    } else {
        float scaleY = sin(rot);
        float offset = -dir * (1.0 - scaleY) * h * 0.5;
        p = sampleB(x + 0.5, cy + (y + 0.5 - cy - offset) / max(0.01, scaleY));
        p.rgb *= 0.5 + (t - 0.5);
    }
#elif defined(K_flipHorizontal)
    float scaleX = cos(t * pi);
    p = t < 0.5 ? sampleA(cx + dxc / max(0.01, scaleX), y + 0.5) : sampleB(cx + dxc / max(0.01, -scaleX), y + 0.5);
    p.rgb *= 0.5 + 0.5 * abs(scaleX);
#elif defined(K_flipVertical)
    float scaleY = cos(t * pi);
    p = t < 0.5 ? sampleA(x + 0.5, cy + dyc / max(0.01, scaleY)) : sampleB(x + 0.5, cy + dyc / max(0.01, -scaleY));
    p.rgb *= 0.5 + 0.5 * abs(scaleY);
#elif defined(K_doorSwingOpen)
    float ax = (x + 0.5) / max(0.01, 1.0 - t);
    p = ax < w ? sampleA(ax, y + 0.5) : pixB;
#elif defined(K_foldOver)
    p = (x + y) < t * (w + h) ? pixB : pixA;
#endif

    // Stored as the CPU stores: straight alpha, each channel clamped.
    gl_FragColor = p.a <= 0.0 ? vec4(0.0) : vec4(clamp(p.rgb / p.a, 0.0, 1.0), clamp(p.a, 0.0, 1.0));
}
