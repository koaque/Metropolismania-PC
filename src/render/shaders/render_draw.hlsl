// The native renderer's one draw shader pair (plan N3): what the GS does per vertex and per pixel for the game's draws.
// Positions arrive in a progressive 640 x 448 frame; colours as the GS has them (0x80 is 1.0, so a byte b reads as b / 255
// here and the GS's "/ 128" becomes "* 255 / 128"). Bindings follow SDL GPU's convention: the fragment stage's texture and
// sampler in space2, its uniforms in space3.

struct VSInput
{
    float2 position : TEXCOORD0; // frame pixels
    float depth : TEXCOORD1;     // GS Z / 2^32
    float4 color : TEXCOORD2;    // RGBA bytes / 255
    float3 stq : TEXCOORD3;
    float fog : TEXCOORD4;
};

struct VSOutput
{
    float4 position : SV_Position;
    noperspective float4 color : TEXCOORD0;
    noperspective float3 stq : TEXCOORD1;
    noperspective float fog : TEXCOORD2;
};

// x in the frame -> x on the target: 1 and 0, except for the HUD on a widened frame (kept at its own proportions in the middle;
// with Extend, a point at or past the frame's edge goes to the target's edge, so a HUD model that covers the screen still does)
cbuffer VertexParams : register(b0, space1)
{
    float XScale;
    float XOffset;
    float Extend;
    float VertexUnused;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float x = input.position.x * XScale + XOffset;
    if (Extend != 0.0 && input.position.x <= 0.5)
        x = min(x, 0.0);
    else if (Extend != 0.0 && input.position.x >= 639.5)
        x = max(x, 640.0);
    output.position = float4(x / 320.0 - 1.0, 1.0 - input.position.y / 224.0, input.depth, 1.0);
    output.color = input.color;
    output.stq = input.stq;
    output.fog = input.fog;
    return output;
}

// Texels are read one by one (Load) so that the GS's wrap modes, region clamp included, and its bilinear filter can be
// followed exactly; the sampler is bound because SDL GPU binds textures with one, and is not used.
Texture2D<float4> Texture : register(t0, space2);
SamplerState Sampler : register(s0, space2);

cbuffer DrawParams : register(b0, space3)
{
    uint Flags;      // 1 textured, 2 texture alpha (TCC), 4 fog, 8 bilinear, 16/32 drawn one texel to one frame pixel in x/y;
                     // bits 8-9 the texture function (TFX), 12-13 and 14-15 the wrap modes (WMS, WMT), 16-17 how such 2D is
                     // enlarged (MMPC_RENDER_FILTER_2D: 0 sharp, 1 smooth, 2 ps2)
    uint AlphaTest;  // ATST: 0 never, 1 always, 2 less, 3 lequal, 4 equal, 5 gequal, 6 greater, 7 notequal
    float AlphaRef;  // AREF, 0..255
    float Scale;     // render target pixels per frame pixel, down
    float4 FogColor; // FOGCOL / 255
    uint4 Region;    // CLAMP's MINU, MAXU, MINV, MAXV
    float4 SnapX;    // across: render target pixels per frame pixel of this draw, and where its frame x 0 lies on the target
    uint4 TexRect;   // the texture's corner in the bound texture (a cell of an atlas page) and its size
    uint4 Mip;       // anisotropic filtering: the most samples (1: off) and the texture's mip levels
};

// One texel coordinate through the GS's wrap mode: 0 repeat, 1 clamp, 2 region clamp, 3 region repeat.
int wrap(int c, uint mode, int size, int lo, int hi)
{
    if (mode == 0u)
        return c & (size - 1);
    if (mode == 1u)
        return clamp(c, 0, size - 1);
    if (mode == 2u)
        return clamp(c, lo, hi);
    return (c & lo) | hi;
}

// A texture from GS memory has a power-of-two size, and the region modes can name texels past it (the GS reads on into
// memory; here they wrap within the texture, so as not to read a neighbour in the atlas). The previous frame (any size,
// 640 x 448 times the scale) is only ever clamped, so its coordinates are in range already and must not be masked.
int inside(int c, int size)
{
    return (size & (size - 1)) == 0 ? c & (size - 1) : clamp(c, 0, size - 1);
}

float4 fetch(int2 c, int2 size)
{
    const uint wms = (Flags >> 12) & 3u, wmt = (Flags >> 14) & 3u;
    const int x = wrap(c.x, wms, size.x, (int)Region.x, (int)Region.y);
    const int y = wrap(c.y, wmt, size.y, (int)Region.z, (int)Region.w);
    return Texture.Load(int3(int2(TexRect.xy) + int2(inside(x, size.x), inside(y, size.y)), 0));
}

// The same at mip level `level` (anisotropic filtering): the texture there is its size >> level, at its place >> level; the
// wrap modes act as at the first level (region bounds shifted down with it).
float4 fetchLevel(int2 c, int2 size0, int level)
{
    const int2 size = max(size0 >> level, int2(1, 1));
    const uint wms = (Flags >> 12) & 3u, wmt = (Flags >> 14) & 3u;
    int x, y;
    if (wms == 3u)
        x = (((c.x << level) & (int)Region.x) | (int)Region.y) >> level;
    else
        x = wrap(c.x, wms, size.x, (int)Region.x >> level, (int)Region.y >> level);
    if (wmt == 3u)
        y = (((c.y << level) & (int)Region.z) | (int)Region.w) >> level;
    else
        y = wrap(c.y, wmt, size.y, (int)Region.z >> level, (int)Region.w >> level);
    return Texture.Load(int3((int2(TexRect.xy) >> level) + int2(inside(x, size.x), inside(y, size.y)), level));
}

// The GS's bilinear filter (half a texel back, four texels) at a mip level; uv in texels of the first level.
float4 bilinearLevel(float2 uv, int2 size0, int level)
{
    const float2 p = uv / float(1u << (uint)level) - 0.5;
    const float2 base = floor(p);
    const float2 f = p - base;
    const int2 i = int2(base);
    const float4 a = fetchLevel(i, size0, level), b = fetchLevel(i + int2(1, 0), size0, level);
    const float4 c = fetchLevel(i + int2(0, 1), size0, level), d = fetchLevel(i + int2(1, 1), size0, level);
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// Anisotropic filtering (an enhancement; the GS takes one sample a pixel however many texels the pixel covers, which is what
// makes distant and slanted textures shimmer): up to Mip.x bilinear samples spread along the long axis of the pixel's
// footprint in the texture, from the mip level that makes each sample cover about a texel across the short axis, blended
// with the next level down (trilinear). `axisX`/`axisY` are the texel steps of one render pixel across and down.
float4 anisotropic(float2 uv, float2 axisX, float2 axisY, int2 size0)
{
    const float lenX = length(axisX), lenY = length(axisY);
    const float major = max(lenX, lenY), minor = max(min(lenX, lenY), 1e-4);
    const float2 axis = lenX >= lenY ? axisX : axisY;
    const float n = clamp(ceil(major / minor), 1.0, float(Mip.x));
    const float lod = clamp(log2(major / n), 0.0, float(Mip.y - 1u));
    const int l0 = int(floor(lod));
    const int l1 = min(l0 + 1, int(Mip.y) - 1);
    const float t = lod - float(l0);
    float4 sum = 0.0;
    for (int k = 0; k < 16; ++k)
    {
        if (float(k) >= n)
            break;
        const float2 at = uv + axis * ((float(k) + 0.5) / n - 0.5);
        float4 tap = bilinearLevel(at, size0, l0);
        if (l1 != l0 && t > 0.0)
            tap = lerp(tap, bilinearLevel(at, size0, l1), t);
        sum += tap;
    }
    return sum / n;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    // Colours as the GS computes them, in bytes: the vertex colour, the texture function (modulate is (texel x vertex) >> 7),
    // fog ((colour x F + FOGCOL x (255 - F)) >> 8), each truncated and clamped to 255 as the GS does.
    const float3 vrgb = round(input.color.rgb * 255.0);
    const float va = round(input.color.a * 255.0);
    float3 rgb = vrgb;
    float alpha = va;

    const int2 size = int2(TexRect.zw);
    // S, T and Q are linear across the screen (the divide is per pixel), so moving the point read is done on them before the
    // divide: exact under perspective too.
    const float3 dstqdx = ddx_fine(input.stq), dstqdy = ddy_fine(input.stq);
    float2 uv;
    // Where the texture is read. The GS samples a pixel at its integer position, where the GPU takes the centre, so texture
    // coordinates are taken half a 1x pixel back (at 1x: the GS's texels exactly). A sprite drawn one texel to one frame pixel
    // instead takes, for each 1x pixel, the texel under the GS's sample point whole (read at the texel's centre), so that it
    // upscales exactly with no bleeding of the neighbouring texels into its edges. (The game draws its text bilinear from
    // texel edges, so on the GS each pixel is half of one texel and half of the next; the texel under the sample point is the
    // second, sharp. A sprite whose coordinates carry the half-texel offset already gets exactly the texel the GS reads.) That
    // is the "sharp" way of enlarging such 2D. "smooth" reads the 2D the GS filters bilinear (the text, pictures) as other
    // draws are read, at every render pixel's own place half a 1x pixel back: in between the GS's own sample points, so no
    // texel the GS does not read; 2D the GS reads nearest stays sharp. "ps2" reads it where the GS does, once a 1x pixel, with
    // the GS's own filter: the GS's 1x picture enlarged.
    const uint filter2d = (Flags >> 16) & 3u;
    const bool smooth = filter2d == 1u && (Flags & 8u) != 0u;
    {
        const float2 pos = input.position.xy;
        const float2 step = float2(SnapX.x, Scale), origin = float2(SnapX.y, 0.0);
        const float2 cell = floor((pos - origin) / step) * step + origin; // the 1x pixel's GS sample point
        const bool snapX = (Flags & 16u) != 0u && !smooth, snapY = (Flags & 32u) != 0u && !smooth;
        const float2 shift = float2(snapX ? cell.x - pos.x : -0.5 * step.x, snapY ? cell.y - pos.y : -0.5 * step.y);
        const float3 stq = input.stq + dstqdx * shift.x + dstqdy * shift.y;
        uv = stq.xy / stq.z * float2(size);
        if (filter2d != 2u) // the texel under it, at its centre (the GS's coordinates land on texel edges: a hair lifted)
        {
            if (snapX)
                uv.x = floor(uv.x + 1.0 / 64.0) + 0.5;
            if (snapY)
                uv.y = floor(uv.y + 1.0 / 64.0) + 0.5;
        }
    }

    // The texel steps of a render pixel across and down, for anisotropic filtering: used where a texture with mip levels is
    // drawn smaller than its texels (more than one texel a pixel), but never for 2D drawn one texel to a pixel.
    const float2 axisX = ddx_fine(uv), axisY = ddy_fine(uv);
    const bool minified = Mip.x > 1u && Mip.y > 1u && (Flags & 48u) == 0u && max(length(axisX), length(axisY)) > 1.0;

    if (Flags & 1u)
    {
        float4 texel;
        if (minified)
            texel = anisotropic(uv, axisX, axisY, size);
        else if (Flags & 8u) // the GS's bilinear filter: half a texel back, then the four texels around
        {
            const float2 p = uv - 0.5;
            const float2 base = floor(p);
            const float2 f = p - base;
            const int2 i = int2(base);
            const float4 a = fetch(i, size), b = fetch(i + int2(1, 0), size);
            const float4 c = fetch(i + int2(0, 1), size), d = fetch(i + int2(1, 1), size);
            texel = lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
        }
        else // nearest; the GS's fixed-point coordinates land exactly on texel edges, so a float a hair below one is lifted
            texel = fetch(int2(floor(uv + 1.0 / 64.0)), size);
        const float4 t = round(texel * 255.0);
        const uint tfx = (Flags >> 8) & 3u;
        const bool tcc = (Flags & 2u) != 0u;
        if (tfx == 0u) // modulate
        {
            rgb = floor(t.rgb * vrgb / 128.0);
            alpha = tcc ? floor(t.a * va / 128.0) : va;
        }
        else if (tfx == 1u) // decal
        {
            rgb = t.rgb;
            alpha = tcc ? t.a : va;
        }
        else // highlight, highlight2
        {
            rgb = floor(t.rgb * vrgb / 128.0) + va;
            alpha = tcc ? (tfx == 2u ? t.a + va : t.a) : va;
        }
    }
    rgb = min(rgb, 255.0);
    alpha = min(alpha, 255.0);
    if (Flags & 4u)
    {
        const float f = round(saturate(input.fog) * 255.0);
        rgb = floor((rgb * f + round(FogColor.rgb * 255.0) * (255.0 - f)) / 256.0);
    }

    // The alpha test compares the byte the GS has.
    const float a = alpha;
    bool pass = true;
    switch (AlphaTest)
    {
    case 0u: pass = false; break;
    case 2u: pass = a < AlphaRef; break;
    case 3u: pass = a <= AlphaRef; break;
    case 4u: pass = a == AlphaRef; break;
    case 5u: pass = a >= AlphaRef; break;
    case 6u: pass = a > AlphaRef; break;
    case 7u: pass = a != AlphaRef; break;
    default: break;
    }
    if (!pass)
        discard;
    // Blending reads alpha as the GS does (0x80 is 1.0); the target keeps the GS's byte (the pipelines store alpha x 128 / 255,
    // render_native.cpp), which a later frame reading this one back as a texture sees.
    return float4(rgb / 255.0, saturate(alpha / 128.0));
}
