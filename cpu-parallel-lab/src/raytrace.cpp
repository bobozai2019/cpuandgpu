#include "engine.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace lab {
namespace {
struct Vec {
    float x, y, z;
    Vec operator+(Vec v) const { return {x+v.x, y+v.y, z+v.z}; }
    Vec operator-(Vec v) const { return {x-v.x, y-v.y, z-v.z}; }
    Vec operator*(float s) const { return {x*s, y*s, z*s}; }
    Vec operator*(Vec v) const { return {x*v.x, y*v.y, z*v.z}; }
};
float Dot(Vec a, Vec b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
Vec Unit(Vec v) { return v * (1.0f / std::sqrt(std::max(1e-20f, Dot(v,v)))); }
struct Rng {
    uint32_t state;
    float Next() {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return float(state >> 8) * (1.0f / 16777216.0f);
    }
    Vec Direction() {
        const float z = 2*Next()-1, a = 6.283185307f*Next(), r = std::sqrt(std::max(0.0f, 1-z*z));
        return {r*std::cos(a), z, r*std::sin(a)};
    }
};
struct Sphere { Vec center; float radius; Vec color; bool metal; };
constexpr std::array<Sphere, 10> scene{{
    {{-1.5f,0.8f,0},0.8f,{0.08f,0.65f,0.72f},false},
    {{0.35f,1.0f,-0.5f},1.0f,{0.82f,0.65f,0.32f},true},
    {{1.85f,0.65f,0.6f},0.65f,{0.85f,0.18f,0.12f},false},
    {{-2.8f,0.5f,-1.5f},0.5f,{0.42f,0.25f,0.75f},true},
    {{2.3f,0.5f,-1.3f},0.5f,{0.25f,0.7f,0.4f},false},
    {{-0.7f,0.3f,1.5f},0.3f,{0.9f,0.5f,0.12f},false},
    {{0.5f,0.25f,1.7f},0.25f,{0.75f,0.78f,0.85f},true},
    {{-1.2f,0.4f,-2.0f},0.4f,{0.7f,0.2f,0.4f},false},
    {{0.3f,0.3f,-2.3f},0.3f,{0.2f,0.7f,0.8f},false},
    {{3.1f,0.35f,0.2f},0.35f,{0.7f,0.7f,0.75f},true}
}};
Vec Trace(Vec origin, Vec direction, Rng& rng, int bounces) {
    Vec throughput{1,1,1};
    for (int bounce = 0; bounce < bounces; ++bounce) {
        float closest = 1e30f;
        Vec normal{}, color{};
        bool metal = false;
        for (const auto& sphere : scene) {
            const Vec oc = origin - sphere.center;
            const float halfB = Dot(oc, direction), c = Dot(oc,oc) - sphere.radius*sphere.radius;
            const float discriminant = halfB*halfB - c;
            if (discriminant < 0) continue;
            float t = -halfB - std::sqrt(discriminant);
            if (t < 0.001f) t = -halfB + std::sqrt(discriminant);
            if (t < 0.001f || t >= closest) continue;
            closest = t; normal = (origin + direction*t - sphere.center)*(1/sphere.radius);
            color = sphere.color; metal = sphere.metal;
        }
        if (direction.y < -0.0001f) {
            const float t = -origin.y / direction.y;
            if (t > 0.001f && t < closest) {
                closest = t; normal = {0,1,0}; metal = false;
                const Vec p = origin + direction*t;
                const bool checker = (int(std::floor(p.x)) + int(std::floor(p.z))) % 2 == 0;
                color = checker ? Vec{0.65f,0.68f,0.72f} : Vec{0.20f,0.25f,0.32f};
            }
        }
        if (closest == 1e30f) {
            const float blend = 0.5f*(direction.y+1);
            const Vec sky = Vec{0.95f,0.97f,1}*(1-blend) + Vec{0.34f,0.58f,0.92f}*blend;
            return throughput*sky;
        }
        origin = origin + direction*closest + normal*0.001f;
        direction = metal ? Unit(direction - normal*(2*Dot(direction,normal)) + rng.Direction()*0.06f)
                          : Unit(normal + rng.Direction());
        if (Dot(direction,normal) <= 0) return {0,0,0};
        throughput = throughput*color;
    }
    return {0,0,0};
}
uint32_t Channel(float v) { return uint32_t(255.99f*std::sqrt(std::clamp(v,0.0f,0.999f))); }
} // namespace
bool RenderTile(const Config& c, const TileView& tile, std::vector<uint32_t>& pixels,
                const std::atomic<bool>& cancel) {
    const Vec camera{0,2.2f,6.5f};
    const Vec forward = Unit(Vec{0,0.7f,0}-camera);
    const Vec right{1,0,0}, up{0,-forward.z,forward.y};
    for (int y = tile.y; y < tile.y+tile.h; ++y) {
        for (int x = tile.x; x < tile.x+tile.w; ++x) {
            if (cancel.load(std::memory_order_relaxed)) return false;
            // Pixel-local randomness: scheduling and tile size cannot change the image.
            const uint32_t index = uint32_t(y*c.width+x);
            Rng rng{(index*747796405u + 2891336453u) | 1u};
            Vec sum{0,0,0};
            for (int sample = 0; sample < c.samples; ++sample) {
                const float u = ((float(x)+rng.Next())/float(c.width)*2-1)*0.48f*float(c.width)/float(c.height);
                const float v = (1-(float(y)+rng.Next())/float(c.height)*2)*0.48f;
                sum = sum + Trace(camera, Unit(forward + right*u + up*v), rng, c.bounces);
            }
            sum = sum*(1.0f/float(c.samples));
            pixels[size_t(index)] = (Channel(sum.x)<<16) | (Channel(sum.y)<<8) | Channel(sum.z);
        }
    }
    return true;
}
} // namespace lab
