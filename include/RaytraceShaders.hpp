#pragma once

namespace qm {
// Selected from representative s/p/d/f convergence checks through n=7.
inline constexpr int raytraceSteps = 256;
inline constexpr const char* raytraceVertex = R"(#version 330 core
layout(location=0) in vec2 aPos;
out vec2 vUV;
void main() {
    vUV = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
})";

inline constexpr const char* raytraceFragment = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform vec3 uCamPos;
uniform vec3 uCamFwd;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform int  uN;
uniform int  uL;
uniform int  uM;
uniform float uZ;
uniform float uAspect;
uniform float uTanHalfFov;
uniform float uRadius;
uniform float uDensityScale;
uniform float uLengthScale;
uniform int uSteps;

float laguerre(int k, int alpha, float x) {
    if (k <= 0) return 1.0;
    float Lm2 = 1.0;
    float Lm1 = 1.0 + float(alpha) - x;
    if (k == 1) return Lm1;
    float L = 0.0;
    for (int j = 2; j <= k; ++j) {
        L = ((2.0*float(j) - 1.0 + float(alpha) - x) * Lm1
            - (float(j) - 1.0 + float(alpha)) * Lm2) / float(j);
        Lm2 = Lm1;
        Lm1 = L;
    }
    return L;
}

float associatedLegendre(int l, int m, float x) {
    m = abs(m);
    if (m > l) return 0.0;
    float pmm = 1.0;
    if (m > 0) {
        float somx2 = sqrt(max(0.0, (1.0 - x)*(1.0 + x)));
        float fact = 1.0;
        for (int i = 1; i <= m; ++i) {
            pmm *= -fact * somx2;
            fact += 2.0;
        }
    }
    if (l == m) return pmm;
    float pmmp1 = x * float(2*m + 1) * pmm;
    if (l == m + 1) return pmmp1;
    float pll = 0.0;
    for (int ll = m + 2; ll <= l; ++ll) {
        pll = (float(2*ll - 1)*x*pmmp1 - float(ll + m - 1)*pmm) / float(ll - m);
        pmm = pmmp1;
        pmmp1 = pll;
    }
    return pll;
}

float density(vec3 p) {
    float r = length(p);

    float rho = 2.0 * uZ * r / float(uN);
    int k = uN - uL - 1;
    int alpha = 2 * uL + 1;
    float L = laguerre(k, alpha, rho);
    float R = exp(-rho * 0.5) * (uL == 0 ? 1.0 : pow(rho, float(uL))) * L;
    float radial = R * R;

    float ct = clamp((r > 0.0 ? p.y / r : 1.0), -1.0, 1.0);
    float Plm = associatedLegendre(uL, uM, ct);
    float angular = Plm * Plm;

    return radial * angular;
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    ndc *= uTanHalfFov;
    ndc.x *= uAspect;
    vec3 rayDir = normalize(uCamFwd + ndc.x * uCamRight + ndc.y * uCamUp);
    vec3 background = vec3(0.02, 0.02, 0.05);
    float projection = dot(uCamPos, rayDir);
    float discriminant = projection * projection - dot(uCamPos, uCamPos) + uRadius * uRadius;
    if (discriminant <= 0.0) {
        FragColor = vec4(background, 1.0);
        return;
    }
    float root = sqrt(discriminant);
    float tMin = max(0.0, -projection - root);
    float tMax = -projection + root;
    if (tMax <= tMin) {
        FragColor = vec4(background, 1.0);
        return;
    }
    int steps = clamp(uSteps, 32, 4096);
    // Stretch the grid away from the nucleus; inner radial structure needs
    // finer resolution than exponentially faint tails. uN/uZ is in a0.
    float radialScale = float(uN) / uZ;
    float lower = asinh((tMin + projection) / radialScale);
    float upper = asinh((tMax + projection) / radialScale);
    float increment = (upper - lower) / float(steps);
    float previous = tMin;
    vec3 col = vec3(0.0);
    float transmittance = 1.0;
    for (int i = 0; i < 4096; ++i) {
        if (i >= steps) break;
        float next = radialScale * sinh(lower + float(i + 1) * increment) - projection;
        float dt = next - previous;
        float t = 0.5 * (previous + next);
        previous = next;
        vec3 p = uCamPos + rayDir * t;
        float d = clamp(density(p) * uDensityScale, 0.0, 1.0);

        float v = clamp(d * 1.5, 0.0, 1.0);
        vec3 c;
        if (v < 0.25)      c = mix(vec3(0.0), vec3(0.4,0.0,0.7), v/0.25);
        else if (v < 0.5)  c = mix(vec3(0.4,0.0,0.7), vec3(0.9,0.05,0.05), (v-0.25)/0.25);
        else if (v < 0.75) c = mix(vec3(0.9,0.05,0.05), vec3(1.0,0.45,0.0), (v-0.5)/0.25);
        else               c = mix(vec3(1.0,0.45,0.0), vec3(1.0,1.0,0.9), (v-0.75)/0.25);

        float alpha = 1.0 - exp(-d * 6.0 * dt / uLengthScale);
        col += transmittance * alpha * c;
        transmittance *= (1.0 - alpha);
        if (transmittance < 0.001) break;
    }

    FragColor = vec4(col + transmittance * background, 1.0);
})";


} // namespace qm
