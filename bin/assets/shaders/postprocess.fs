#version 330
// =============================================================================
// OWNER: Developer A (Graphics & Shaders)
// FILE:  assets/shaders/postprocess.fs
// DESC:  GLSL 330 high-speed post-processing neon glow/bloom + subtle CRT
//        scanline shader applied to the full-screen RenderTexture2D.
// =============================================================================

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 uResolution;
uniform float uTime;

// Extract bright neon elements for bloom contribution
vec3 extractBright(vec3 color) {
    float luminance = max(color.r, max(color.g, color.b));
    float knee = smoothstep(0.28, 0.85, luminance);
    return color * knee;
}

void main() {
    vec2 res = max(uResolution, vec2(1.0, 1.0));
    vec2 texel = 1.0 / res;
    vec2 uv = fragTexCoord;

    // 1. Subtle radial RGB chromatic aberration near screen edges
    vec2 fromCenter = uv - vec2(0.5);
    float edgeDist = dot(fromCenter, fromCenter);
    vec2 chromaOffset = fromCenter * edgeDist * 0.008;

    vec3 baseColor;
    baseColor.r = texture(texture0, uv + chromaOffset).r;
    baseColor.g = texture(texture0, uv).g;
    baseColor.b = texture(texture0, uv - chromaOffset).b;

    // 2. High-speed 13-tap dual-ring bloom kernel (optimized for integrated GPUs)
    vec3 bloom = extractBright(baseColor) * 0.24;

    vec2 inner = texel * 2.1;
    bloom += extractBright(texture(texture0, uv + vec2(-inner.x, -inner.y)).rgb) * 0.11;
    bloom += extractBright(texture(texture0, uv + vec2( inner.x, -inner.y)).rgb) * 0.11;
    bloom += extractBright(texture(texture0, uv + vec2(-inner.x,  inner.y)).rgb) * 0.11;
    bloom += extractBright(texture(texture0, uv + vec2( inner.x,  inner.y)).rgb) * 0.11;

    vec2 mid = texel * 4.8;
    bloom += extractBright(texture(texture0, uv + vec2(-mid.x,   0.0)).rgb) * 0.055;
    bloom += extractBright(texture(texture0, uv + vec2( mid.x,   0.0)).rgb) * 0.055;
    bloom += extractBright(texture(texture0, uv + vec2(  0.0, -mid.y)).rgb) * 0.055;
    bloom += extractBright(texture(texture0, uv + vec2(  0.0,  mid.y)).rgb) * 0.055;

    vec2 outer = texel * 7.6;
    bloom += extractBright(texture(texture0, uv + vec2(-outer.x, -outer.y)).rgb) * 0.025;
    bloom += extractBright(texture(texture0, uv + vec2( outer.x, -outer.y)).rgb) * 0.025;
    bloom += extractBright(texture(texture0, uv + vec2(-outer.x,  outer.y)).rgb) * 0.025;
    bloom += extractBright(texture(texture0, uv + vec2( outer.x,  outer.y)).rgb) * 0.025;

    // Combine base scene with intense neon bloom
    vec3 color = baseColor + bloom * 1.40;

    // 3. Subtle CRT horizontal scanline modulation
    float scanlineWave = sin(uv.y * res.y * 3.14159265 + uTime * 2.0);
    float scanlineFactor = 0.93 + 0.07 * scanlineWave;
    color *= scanlineFactor;

    // 4. Subtle arcade monitor edge vignette
    float vignette = 1.0 - edgeDist * 0.55;
    color *= clamp(vignette, 0.0, 1.0);

    finalColor = vec4(color, 1.0) * fragColor;
}
