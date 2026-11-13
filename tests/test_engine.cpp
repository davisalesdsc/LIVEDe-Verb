// Standalone tests for the de-reverb engine (no JUCE needed).
//   g++ -O2 -std=c++17 -I../Source tests/test_engine.cpp -o test_engine && ./test_engine
#include "dsp/AuroraDeVerbEngine.h"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

using namespace aurora;

static int failures = 0;
static void check (bool ok, const std::string& what)
{
    std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

static double energy (const std::vector<float>& x, size_t from, size_t to)
{
    double e = 0;
    for (size_t i = from; i < std::min (to, x.size()); ++i) e += (double) x[i] * x[i];
    return e;
}

// FFT convolution with our own FFT (power-of-two padding).
static std::vector<float> convolve (const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t outLen = a.size() + b.size() - 1;
    int order = 1;
    while ((size_t) (1 << order) < outLen) ++order;
    Fft fft; fft.prepare (order);
    const int n = fft.getSize();
    std::vector<Fft::Complex> A ((size_t) n), B ((size_t) n);
    for (size_t i = 0; i < a.size(); ++i) A[i] = a[i];
    for (size_t i = 0; i < b.size(); ++i) B[i] = b[i];
    fft.forward (A.data()); fft.forward (B.data());
    for (int i = 0; i < n; ++i) A[(size_t) i] *= B[(size_t) i];
    fft.inverse (A.data());
    std::vector<float> y (outLen);
    for (size_t i = 0; i < outLen; ++i) y[i] = A[i].real();
    return y;
}

struct Signals { std::vector<float> dryL, dryR, wetL, wetR, targetL; std::vector<std::pair<size_t,size_t>> notes; };

// Plucked/struck "instrument" notes: sharp attack, harmonic body, ~300 ms natural decay.
static Signals makeInstrumentInReverbRoom (float sr, float rt60)
{
    Signals s;
    const size_t len = (size_t) (sr * 8.0f);
    s.dryL.assign (len, 0.0f);
    const float freqs[] = { 110.0f, 196.0f, 146.8f, 261.6f, 220.0f, 329.6f };
    std::mt19937 rng (1234);
    std::normal_distribution<float> nd (0.0f, 1.0f);

    for (int note = 0; note < 6; ++note)
    {
        const size_t start = (size_t) (sr * (0.25f + 1.25f * (float) note));
        const size_t noteLen = (size_t) (sr * 0.6f);
        s.notes.push_back ({ start, start + noteLen });
        for (size_t i = 0; i < noteLen && start + i < len; ++i)
        {
            const float t = (float) i / sr;
            const float env = std::exp (-t / 0.12f) * std::min (1.0f, t / 0.002f);
            float v = 0.0f;
            for (int h = 1; h <= 8; ++h)
                v += std::sin (2.0f * kPi * freqs[note] * (float) h * t) / (float) h;
            v += 0.3f * nd (rng) * std::exp (-t / 0.01f); // pick/attack noise
            s.dryL[start + i] += 0.3f * v * env;
        }
    }
    s.dryR = s.dryL;

    // Room impulse response: direct path + exponentially decaying noise from 8 ms.
    auto makeRir = [&] (uint32_t seed)
    {
        std::mt19937 r (seed);
        std::normal_distribution<float> n (0.0f, 1.0f);
        std::vector<float> h ((size_t) (sr * rt60 * 1.5f), 0.0f);
        h[0] = 1.0f;
        const float tau = rt60 / (3.0f * std::log (10.0f)); // amplitude time constant
        const size_t pre = (size_t) (0.008f * sr);
        for (size_t i = pre; i < h.size(); ++i)
            h[i] += 0.06f * n (r) * std::exp (-(float) i / sr / tau);
        return h;
    };
    const auto rirL = makeRir (11);
    s.wetL = convolve (s.dryL, rirL); s.wetL.resize (len);
    // Ideal result of removing only the late reverb: direct sound + first 40 ms.
    std::vector<float> early (rirL.begin(), rirL.begin() + (ptrdiff_t) (0.04f * sr));
    s.targetL = convolve (s.dryL, early); s.targetL.resize (len);
    s.wetR = convolve (s.dryR, makeRir (22)); s.wetR.resize (len);
    return s;
}

static void runEngine (DeVerbEngine& e, std::vector<float>& L, std::vector<float>& R, int block, bool randomBlocks = false)
{
    std::mt19937 rng (7);
    size_t pos = 0;
    while (pos < L.size())
    {
        int n = randomBlocks ? 1 + (int) (rng() % 1500) : block;
        n = (int) std::min ((size_t) n, L.size() - pos);
        e.process (L.data() + pos, R.data() + pos, n);
        pos += (size_t) n;
    }
}

int main()
{
    for (float sr : { 44100.0f, 48000.0f, 96000.0f })
    {
        std::printf ("\n=== sample rate %.0f ===\n", sr);
        const float rt60 = 1.0f;
        const Signals s = makeInstrumentInReverbRoom (sr, rt60);

        // 1) Transparency: reduction 0 must reconstruct input (delayed by latency).
        {
            DeVerbEngine e; e.prepare (sr, 2);
            DeVerbParameters p; p.reduction = 0.0f; e.setParameters (p);
            auto L = s.wetL, R = s.wetR;
            runEngine (e, L, R, 256);
            const int lat = e.getLatencySamples();
            float maxErr = 0.0f;
            for (size_t i = (size_t) lat; i < L.size(); ++i)
            {
                maxErr = std::max (maxErr, std::abs (L[i] - s.wetL[i - (size_t) lat]));
                maxErr = std::max (maxErr, std::abs (R[i] - s.wetR[i - (size_t) lat]));
            }
            check (maxErr < 1e-4f, "reducao 0 = reconstrucao perfeita (erro max " + std::to_string (maxErr) + ", latencia " + std::to_string (lat) + ")");
        }

        // 2) De-reverb effectiveness on the instrument recording.
        DeVerbParameters p;
        p.reduction = 1.0f; p.decaySec = rt60; p.earlyMs = 25.0f; p.floorDb = -30.0f; p.release = 0.3f; p.lowProtectHz = 60.0f;
        DeVerbEngine e; e.prepare (sr, 2); e.setParameters (p);
        auto L = s.wetL, R = s.wetR;
        runEngine (e, L, R, 512);
        const size_t lat = (size_t) e.getLatencySamples();
        std::vector<float> outL (L.begin() + (ptrdiff_t) lat, L.end());
        outL.resize (L.size(), 0.0f);

        double tailIn = 0, tailOut = 0, attackIn = 0, attackOut = 0, errIn = 0, errOut = 0;
        for (auto [a, b] : s.notes)
        {
            const size_t atkEnd = a + (size_t) (0.03f * sr);
            attackIn  += energy (s.wetL, a, atkEnd);
            attackOut += energy (outL, a, atkEnd);
            // Region where only the room tail remains (after the note has died).
            const size_t tailA = b + (size_t) (0.05f * sr), tailB = a + (size_t) (1.2f * sr);
            tailIn  += energy (s.wetL, tailA, tailB);
            tailOut += energy (outL, tailA, tailB);
        }
        for (size_t i = (size_t) sr; i < s.dryL.size() - (size_t) sr; ++i)
        {
            errIn  += std::pow ((double) s.wetL[i] - s.targetL[i], 2.0);
            errOut += std::pow ((double) outL[i] - s.targetL[i], 2.0);
        }
        const double tailRed = 10.0 * std::log10 (tailIn / tailOut);
        const double atkChange = 10.0 * std::log10 (attackOut / attackIn);
        const double lateRed = 10.0 * std::log10 (errIn / errOut);
        std::printf ("    cauda apos as notas: -%.1f dB | reverb tardia total: -%.1f dB | ataques: %+.2f dB\n",
                     tailRed, lateRed, atkChange);
        check (tailRed > 15.0, "cauda de reverb apos as notas reduzida > 15 dB");
        check (lateRed > 4.0, "reverberacao tardia (vs. referencia ideal) reduzida > 4 dB");
        check (atkChange > -1.0, "ataques preservados (perda < 1 dB)");
        check (e.getReductionDb() <= 0.0f, "medidor de reducao em dB valido");

        // 3) Block-size independence.
        {
            DeVerbEngine e2; e2.prepare (sr, 2); e2.setParameters (p);
            auto L2 = s.wetL, R2 = s.wetR;
            runEngine (e2, L2, R2, 0, true);
            float diff = 0;
            for (size_t i = 0; i < L.size(); ++i) diff = std::max (diff, std::abs (L[i] - L2[i]));
            check (diff < 1e-6f, "resultado identico com qualquer tamanho de buffer");
        }

        // 4) Listen-removed + normal output = original (delta is exact).
        {
            DeVerbEngine e3; e3.prepare (sr, 2);
            auto p3 = p; p3.listenRemoved = true; e3.setParameters (p3);
            auto L3 = s.wetL, R3 = s.wetR;
            runEngine (e3, L3, R3, 512);
            float maxErr = 0;
            for (size_t i = lat; i < L.size(); ++i)
                maxErr = std::max (maxErr, std::abs (L[i] + L3[i] - s.wetL[i - lat]));
            check (maxErr < 1e-4f, "modo 'ouvir removido' e exatamente a diferenca");
        }

        // 5) Robustness: mono, silence, extreme parameters, loud input.
        {
            DeVerbEngine e4; e4.prepare (sr, 1);
            DeVerbParameters px; px.reduction = 1.0f; px.decaySec = 5.0f; px.earlyMs = 10.0f; px.floorDb = -40.0f; px.release = 0.0f; px.outputDb = 12.0f;
            e4.setParameters (px);
            std::vector<float> m (s.wetL.size(), 0.0f);
            for (size_t i = m.size() / 2; i < m.size(); ++i) m[i] = 4.0f * s.wetL[i];
            e4.process (m.data(), nullptr, (int) m.size());
            bool finite = true; float peak = 0;
            for (float v : m) { finite &= std::isfinite (v); peak = std::max (peak, std::abs (v)); }
            check (finite && peak < 100.0f, "mono + silencio + parametros extremos: sem NaN/explosao");
        }
    }

    std::printf ("\n%s (%d falha%s)\n", failures == 0 ? "TODOS OS TESTES PASSARAM" : "HA FALHAS", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
