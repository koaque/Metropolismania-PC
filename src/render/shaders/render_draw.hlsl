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

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position.x / 320.0 - 1.0, 1.0 - input.position.y / 224.0, input.depth, 1.0);
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
    uint Flags;      // 1 textured, 2 texture alpha (TCC), 4 fog, 8 bilinear, 16/32 sample x/y at the 1x pixel's centre;
                     // bits 8-9 the texture function (TFX), 12-13 and 14-15 the wrap modes (WMS, WMT)
    uint AlphaTest;  // ATST: 0 never, 1 always, 2 less, 3 lequal, 4 equal, 5 gequal, 6 greater, 7 notequal
    float AlphaRef;  // AREF, 0..255
    float Scale;     // render target pixels per frame pixel
    float4 FogColor; // FOGCOL / 255
    uint4 Region;    // CLAMP's MINU, MAXU, MINV, MAXV
};

static const float kScale = 255.0 / 128.0;

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

float4 fetch(int2 c, int2 size)
{
    const uint wms = (Flags >> 12) & 3u, wmt = (Flags >> 14) & 3u;
    const int x = wrap(c.x, wms, size.x, (int)Region.x, (int)Region.y);
    const int y = wrap(c.y, wmt, size.y, (int)Region.z, (int)Region.w);
    return Texture.Load(int3(x & (size.x - 1), y & (size.y - 1), 0));
}

float4 PSMain(VSOutput input) : SV_Target0
{
    float4 vertex = input.color;
    float3 rgb = vertex.rgb;
    float alpha = vertex.a;

    uint width, height;
    Texture.GetDimensions(width, height);
    const int2 size = int2(width, height);
    float2 uv = input.stq.xy / input.stq.z * float2(size);
    // Where the texture is read. The GS samples a pixel at its integer position, where the GPU takes the centre, so texture
    // coordinates are taken half a 1x pixel back (at 1x: the GS's texels exactly). A sprite drawn one texel to one frame pixel
    // instead takes each 1x pixel's texel whole, at the texel's centre, so that it upscales exactly with no bleeding of the
    // neighbouring texels into its edges. (The game draws its text bilinear from texel edges, so on the GS each pixel is half
    // of one texel and half of the next; sampled at the centres it stays sharp.)
    const float2 duvdx = ddx_fine(uv), duvdy = ddy_fine(uv);
    {
        const float2 pos = input.position.xy;
        const float2 centre = (floor(pos / Scale) + 0.5) * Scale;
        const float2 shift = float2((Flags & 16u) ? centre.x - pos.x : -0.5 * Scale, (Flags & 32u) ? centre.y - pos.y : -0.5 * Scale);
        uv += duvdx * shift.x + duvdy * shift.y;
    }

    if (Flags & 1u)
    {
        float4 texel;
        if (Flags & 8u) // the GS's bilinear filter: half a texel back, then the four texels around
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
        const uint tfx = (Flags >> 8) & 3u;
        const bool tcc = (Flags & 2u) != 0u;
        if (tfx == 0u) // modulate
        {
            rgb = texel.rgb * vertex.rgb * kScale;
            alpha = tcc ? texel.a * vertex.a * kScale : vertex.a;
        }
        else if (tfx == 1u) // decal
        {
            rgb = texel.rgb;
            alpha = tcc ? texel.a : vertex.a;
        }
        else // highlight, highlight2
        {
            rgb = texel.rgb * vertex.rgb * kScale + vertex.a;
            alpha = tcc ? (tfx == 2u ? texel.a + vertex.a : texel.a) : vertex.a;
        }
    }
    rgb = saturate(rgb);
    alpha = saturate(alpha);
    if (Flags & 4u)
        rgb = lerp(FogColor.rgb, rgb, input.fog);

    // The alpha test compares the byte the GS would have.
    const float a = round(alpha * 255.0);
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
    // Blending reads alpha as the GS does (0x80 is 1.0).
    return float4(rgb, saturate(alpha * kScale));
}
