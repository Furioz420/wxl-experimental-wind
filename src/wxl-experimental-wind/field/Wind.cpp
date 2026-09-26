// The wind: one field, shared by everything that answers to it.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include "wxl-experimental-wind/field/Wind.hpp"

#include "config.hpp"
#include "common/Log.hpp"
#include "engine/hook/Registry.hpp"
#include "engine/events/Event.hpp"
#include "offsets/game/Weather.hpp"

#include <windows.h>

#include <cmath>

namespace
{
    namespace wo = wxl::offsets::game::weather;
    using namespace wxl::wind;

    constexpr float kTwoPi = 6.28318530718f;

    /// A hash, not a random source -- the same reasoning as the sea's, and for the same reason: the
    /// phases below have to come out identical on every machine without anything being exchanged.
    ///
    /// Its own copy rather than the sea's is deliberate and is about DEPENDENCY, not about the
    /// arithmetic. The wind sits under the water and the water reads it; reaching the other way for
    /// seven lines would make the lower layer depend on the upper one.
    float SeededUnit(uint32_t seed, int index)
    {
        uint32_t h = seed ^ (static_cast<uint32_t>(index) * 0x9E3779B9u);
        h ^= h >> 16;
        h *= 0x7FEB352Du;
        h ^= h >> 15;
        h *= 0x846CA68Bu;
        h ^= h >> 16;
        return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
    }

    /// Components in each envelope.
    ///
    /// Three, and the ratios below are what makes three enough. With periods sharing no common
    /// measure the sum repeats only at their least common multiple, which for these is hours -- so a
    /// player never sees the pattern come round, and a fourth component would buy nothing anybody
    /// could perceive.
    constexpr int kComponents = 3;
    /// Deliberately not simple fractions. At 1/2 or 1/3 the components would share a period again
    /// and the sum would be a shape repeating at the slowest of them.
    constexpr float kRatio[kComponents] = { 1.0f, 0.4370f, 0.1913f };
    constexpr float kWeight[kComponents] = { 0.60f, 0.28f, 0.12f };

    /// A band-limited wander in [-1, 1], as a pure function of (seed, time).
    float Wander(uint32_t seed, int lane, float period, double t)
    {
        if (period < 0.05f) period = 0.05f;

        float sum = 0.0f;
        for (int i = 0; i < kComponents; ++i)
        {
            const float phase = SeededUnit(seed, lane * 97 + i) * kTwoPi;
            const double turns = t / static_cast<double>(period * kRatio[i]);
            // Folded before it reaches a float. Left as seconds-over-period it would lose its
            // fractional part after a few hours of uptime and the wind would quietly seize up.
            const double wrapped = turns - std::floor(turns);
            sum += kWeight[i] * std::sin(static_cast<float>(wrapped) * kTwoPi + phase);
        }
        return sum;
    }

    WindProfile g_profile;
    WindSample  g_frame;

    /// Counter reading the wind is measured from. Taken once, at load -- see Now().
    const double g_origin = [] {
        LARGE_INTEGER f{}, n{};
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&n);
        return (f.QuadPart > 0) ? static_cast<double>(n.QuadPart) / static_cast<double>(f.QuadPart)
                                : 0.0;
    }();

    /// What the sky is doing, 0 in a calm and 1 in a full storm.
    ///
    /// The client's own dead zone is reused rather than replaced -- below the knee nothing else in
    /// the world reacts either, so a wind that rose there would be the only thing on screen
    /// answering a sky that has not changed. A missing weather object is a calm, not a fault: that
    /// is the normal state indoors and during a load.
    float StormIntensity()
    {
        void* w = *reinterpret_cast<void**>(wo::kWorldWeather);
        if (!w) return 0.0f;

        const float raw = *reinterpret_cast<const float*>(static_cast<uint8_t*>(w) + wo::kIntensity);
        if (raw <= wo::kIntensityKnee) return 0.0f;
        const float t = (raw - wo::kIntensityKnee) / (1.0f - wo::kIntensityKnee);
        return (t > 1.0f) ? 1.0f : t;
    }

    void OnFrameBoundary(void*, const void*)
    {
        g_frame = WindAt(g_profile, Now());
    }

    bool Install()
    {
        wxl::events::Subscribe(wxl::events::Event::OnFrame, &OnFrameBoundary, nullptr);
        g_frame = WindAt(g_profile, 0.0);   // so the first consumer of the session has a wind

        WLOG_INFO("wind: installed (base %.1f u/s heading %.0f deg, gust %.0f%% over %.0fs, "
                  "veer %.0f deg over %.0fs)",
                  g_profile.baseSpeed, g_profile.headingDeg, g_profile.gust * 100.0f,
                  g_profile.gustPeriod, g_profile.veerDeg, g_profile.veerPeriod);
        return true;
    }
}

namespace wxl::wind
{
    double Now()
    {
        LARGE_INTEGER f{}, n{};
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&n);
        if (f.QuadPart <= 0) return 0.0;
        return static_cast<double>(n.QuadPart) / static_cast<double>(f.QuadPart) - g_origin;
    }

    WindSample WindAt(const WindProfile& p, double seconds)
    {
        WindSample s;

        // THE GUST ENVELOPE, and its shape is the whole difference between wind and a slider.
        //
        // The raw wander is a sum of sines, which spends as much time high as low -- that reads as
        // heaving, not as gusting. Real wind sits in a lull and visits its peak briefly, so the
        // envelope is pushed towards its floor before it is used. The exponent is what does it, and
        // it is applied to the 0..1 form rather than the signed one so a lull cannot become a wind
        // blowing backwards.
        const float raw = Wander(p.seed, 0, p.gustPeriod, seconds);
        const float env = std::pow((raw + 1.0f) * 0.5f, 1.7f);
        s.gust = env;

        // Centred on the SUSTAINED speed rather than added to it, so raising the gust amount makes
        // the wind more variable instead of simply stronger -- which is what the control claims to
        // do and what makes it usable without re-tuning the base every time.
        const float gain = 1.0f + p.gust * (env - 0.5f) * 2.0f;

        // The sky, folded in as a gain the coupling can dial to nothing. Written so that a coupling
        // of zero is a MULTIPLIER OF ONE rather than a multiplier of zero: it means "ignore the
        // weather", not "no wind", and the difference is a dead calm nobody asked for.
        //
        // Up to a bit over double in a full storm. The sea's own amplitude already answers to the
        // same intensity, so this stays modest -- otherwise the two gains multiply and a storm
        // arrives as a wall of water rather than as weather.
        const float weather = 1.0f + StormIntensity() * 1.1f * p.weatherCoupling;

        s.speed = p.baseSpeed * gain * weather;
        if (s.speed < 0.0f) s.speed = 0.0f;

        // Its own lane and its own period, so the direction is not a function of the strength. Tying
        // them would make every gust arrive from a new quarter, which reads as a broken compass.
        s.headingDeg = p.headingDeg + p.veerDeg * Wander(p.seed, 1, p.veerPeriod, seconds);

        const float rad = s.headingDeg * (kTwoPi / 360.0f);
        s.dir[0] = std::cos(rad);
        s.dir[1] = std::sin(rad);
        return s;
    }

    WindProfile&      Settings() { return g_profile; }
    const WindSample& Frame()    { return g_frame; }
}

WXL_REGISTER_FEATURE("wind", wxl::features::wind, Install)
