#version 330
// =============================================================================
// OWNER: Developer A (Graphics & Shaders)
// FILE:  assets/shaders/postprocess.fs
// DESC:  GLSL 330 post-processing neon glow/bloom + subtle CRT scanline shader
//        applied to the full-screen RenderTexture2D.
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

    // 2. Multi-ring 2D Gaussian-style glow/bloom kernel for neon vector lines
    vec3 bloom = vec3(0.0);
    float totalWeight = 0.0;

    for (int x = -3; x <= 3; x++) {
        for (int y = -3; y <= 3; y++) {
            vec2 offset = vec2(float(x), float(y)) * texel * 2.25;
            float weight = exp(-0.22 * float(x * x + y * y));
            vec3 sampleCol = texture(texture0, uv + offset).rgb;
            bloom += extractBright(sampleCol) * weight;
            totalWeight += weight;
        }
    }
    bloom /= max(totalWeight, 0.0001);

    // Combine base scene with intense neon bloom
    vec3 color = baseColor + bloom * 1.35;

    // 3. Subtle CRT horizontal scanline modulation
    float scanlineWave = sin(uv.y * res.y * 3.14159265 + uTime * 2.0);
    float scanlineFactor = 0.93 + 0.07 * scanlineWave;
    color *= scanlineFactor;

    // 4. Subtle arcade monitor edge vignette
    float vignette = 1.0 - edgeDist * 0.55;
    color *= clamp(vignette, 0.0, 1.0);

    finalColor = vec4(color, 1.0) * fragColor;
}
