#version 330
// =============================================================================
// OWNER: Developer A (Graphics & Shaders)
// FILE:  assets/shaders/background.fs
// DESC:  GLSL 330 procedural scrolling starfield & subtle neon nebula background
//        driven by uTime and uResolution uniforms.
// =============================================================================

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform float uTime;
uniform vec2 uResolution;

// 2D pseudo-random hash function
float hash21(vec2 p) {
    p = fract(p * vec2(234.34, 435.345));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}

// Smooth 2D value noise for nebula clouds
float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// 4-octave Fractional Brownian Motion (FBM) for organic nebula waves
float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    mat2 rot = mat2(0.8, 0.6, -0.6, 0.8);
    for (int i = 0; i < 4; i++) {
        value += amplitude * valueNoise(p);
        p = rot * p * 2.02 + vec2(1.7, 9.2);
        amplitude *= 0.5;
    }
    return value;
}

// Procedural parallax star layer
vec3 starLayer(vec2 uv, float scale, float speed, float seedOffset) {
    vec2 scrolledUV = uv * scale + vec2(uTime * speed, uTime * speed * 0.35) + vec2(seedOffset);
    vec2 gridId = floor(scrolledUV);
    vec2 gridUV = fract(scrolledUV) - 0.5;

    float rand = hash21(gridId);
    if (rand < 0.78) {
        return vec3(0.0);
    }

    // Sub-cell offset so stars are not aligned to a strict grid
    vec2 starOffset = vec2(hash21(gridId + 11.3), hash21(gridId + 27.9)) - 0.5;
    starOffset *= 0.65;

    float dist = length(gridUV - starOffset);
    float twinkle = 0.65 + 0.35 * sin(uTime * (2.5 + rand * 4.0) + rand * 6.28318);
    float brightness = smoothstep(0.085, 0.0, dist) * twinkle;

    // Sharp core highlight
    brightness += smoothstep(0.025, 0.0, dist) * 1.2;

    // Tint stars between electric cyan, pure white, and neon magenta
    vec3 starTint = mix(vec3(0.45, 0.90, 1.0), vec3(1.0, 0.55, 0.95), fract(rand * 13.37));
    return starTint * brightness * smoothstep(0.78, 1.0, rand);
}

void main() {
    vec2 res = max(uResolution, vec2(1.0, 1.0));
    vec2 uv = gl_FragCoord.xy / res;
    vec2 centeredUV = (gl_FragCoord.xy - 0.5 * res) / res.y;

    // 1. Deep space void base color
    vec3 col = mix(vec3(0.02, 0.02, 0.06), vec3(0.04, 0.01, 0.08), uv.y);

    // 2. Subtle animated dual-tone neon nebula (Cyan vs Magenta)
    vec2 nebulaUV1 = centeredUV * 2.2 + vec2(uTime * 0.025, -uTime * 0.015);
    vec2 nebulaUV2 = centeredUV * 1.8 + vec2(-uTime * 0.02, uTime * 0.018);

    float n1 = fbm(nebulaUV1);
    float n2 = fbm(nebulaUV2 + vec2(4.2, 1.9));

    vec3 cyanNebula = vec3(0.0, 0.55, 0.85) * smoothstep(0.38, 0.82, n1) * 0.22;
    vec3 magentaNebula = vec3(0.85, 0.08, 0.60) * smoothstep(0.40, 0.85, n2) * 0.20;
    col += cyanNebula + magentaNebula;

    // 3. Subtle neon arena background grid
    vec2 gridCoord = fract(centeredUV * 12.0 + vec2(uTime * 0.04, 0.0));
    float gridLineX = smoothstep(0.025, 0.0, abs(gridCoord.x - 0.5));
    float gridLineY = smoothstep(0.025, 0.0, abs(gridCoord.y - 0.5));
    float gridMask = max(gridLineX, gridLineY);
    col += vec3(0.05, 0.16, 0.28) * gridMask * 0.18;

    // 4. Multi-layer scrolling parallax starfield
    col += starLayer(centeredUV, 14.0, 0.12, 1.0) * 0.45;  // Distant slow stars
    col += starLayer(centeredUV, 9.0,  0.25, 37.0) * 0.75; // Mid-field stars
    col += starLayer(centeredUV, 5.5,  0.45, 91.0) * 1.10; // Foreground bright stars

    finalColor = vec4(col, 1.0);
}
