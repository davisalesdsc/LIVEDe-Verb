// Aurora DeVerb — spectral de-reverberation engine for instrument recordings.
//
// Original implementation of a well-known published approach:
//  * STFT analysis/resynthesis (sqrt-Hann windows, 75% overlap).
//  * Late-reverb power is predicted with the statistical room model
//    (Lebart et al. 2001 / Habets 2007): the reverberant tail at time t is the
//    signal power from T_early seconds ago, decayed by the room's RT60:
//        P_late(t, k) = 10^(-6 * T_early / RT60) * P(t - T_early, k)
//  * Power spectral subtraction with a gain floor, frequency smoothing,
//    asymmetric gain smoothing (instant attack so instrument transients pass,
//    adjustable release), and a low-frequency protection region.
//  * The same gain is applied to L and R, so the stereo image is preserved.
//
// Header-only and framework-independent so it can be unit-tested without JUCE.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace aurora
{

constexpr float kPi = 3.14159265358979323846f;

// ---------------------------------------------------------------- radix-2 complex FFT
class Fft
{
public:
    using Complex = std::complex<float>;

    void prepare (int order)
    {
        size = 1 << order;
        twiddles.resize ((size_t) size / 2);
        for (int k = 0; k < size / 2; ++k)
        {
            const double a = -2.0 * 3.14159265358979323846 * k / size;
            twiddles[(size_t) k] = Complex ((float) std::cos (a), (float) std::sin (a));
        }

        bitReverse.resize ((size_t) size);
        for (int i = 0; i < size; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b)
                r |= ((i >> b) & 1) << (order - 1 - b);
            bitReverse[(size_t) i] = r;
        }
    }

    int getSize() const noexcept { return size; }

    void forward (Complex* data) const noexcept { transform (data, false); }

    void inverse (Complex* data) const noexcept
    {
        transform (data, true);
        const float scale = 1.0f / (float) size;
        for (int i = 0; i < size; ++i)
            data[i] *= scale;
    }

private:
    void transform (Complex* d, bool inverse) const noexcept
    {
        for (int i = 0; i < size; ++i)
        {
            const int j = bitReverse[(size_t) i];
            if (i < j)
                std::swap (d[i], d[j]);
        }

        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len / 2, step = size / len;
            for (int i = 0; i < size; i += len)
                for (int k = 0; k < half; ++k)
                {
                    Complex w = twiddles[(size_t) (k * step)];
                    if (inverse)
                        w = std::conj (w);
                    const Complex u = d[i + k];
                    const Complex v = d[i + k + half] * w;
                    d[i + k]        = u + v;
                    d[i + k + half] = u - v;
                }
        }
    }

    int size = 0;
    std::vector<Complex> twiddles;
    std::vector<int> bitReverse;
};

// ---------------------------------------------------------------- parameters
struct DeVerbParameters
{
    float reduction    = 0.70f;   // 0..1   how much of the estimated reverb to remove
    float decaySec     = 0.80f;   // 0.1..5 RT60 of the room in the recording
    float earlyMs      = 30.0f;   // 10..150 time after a note that is treated as "direct"
    float floorDb      = -24.0f;  // -40..0  maximum attenuation per bin
    float release      = 0.30f;   // 0..1   gain release smoothing (0 = fast, 1 = slow)
    float lowProtectHz = 100.0f;  // 20..500 below this, processing fades out
    float mix          = 1.00f;   // 0..1
    float outputDb     = 0.0f;    // -12..12
    bool  listenRemoved = false;  // output only what is being removed (delta)
};

// ---------------------------------------------------------------- engine
class DeVerbEngine
{
public:
    static constexpr float kMaxEarlyMs = 150.0f;

    // numChannels: 1 or 2
    void prepare (double sampleRate, int numChannels)
    {
        sr = (float) sampleRate;
        stereo = numChannels > 1;

        // Keep ~43 ms analysis windows regardless of sample rate.
        const int order = sampleRate <= 50000.0 ? 11 : (sampleRate <= 100000.0 ? 12 : 13);
        fft.prepare (order);
        N = fft.getSize();
        hop = N / 4;
        bins = N / 2 + 1;

        window.resize ((size_t) N);
        for (int n = 0; n < N; ++n)
            window[(size_t) n] = std::sqrt (0.5f - 0.5f * std::cos (2.0f * kPi * (float) n / (float) N));

        inL.assign ((size_t) N, 0.0f);  inR.assign ((size_t) N, 0.0f);
        outL.assign ((size_t) N, 0.0f); outR.assign ((size_t) N, 0.0f);
        spectrum.assign ((size_t) N, {});

        power.assign ((size_t) bins, 0.0f);
        smoothedPower.assign ((size_t) bins, 0.0f);
        gainRaw.assign ((size_t) bins, 1.0f);
        gainSmoothed.assign ((size_t) bins, 1.0f);
        gainFinal.assign ((size_t) bins, 1.0f);
        onset.assign ((size_t) bins, 0);

        historyFrames = (int) std::ceil (kMaxEarlyMs * 0.001f * sr / (float) hop) + 2;
        history.assign ((size_t) (historyFrames * bins), 0.0f);

        prepared = true;
        reset();
    }

    void reset()
    {
        std::fill (inL.begin(), inL.end(), 0.0f);
        std::fill (inR.begin(), inR.end(), 0.0f);
        std::fill (outL.begin(), outL.end(), 0.0f);
        std::fill (outR.begin(), outR.end(), 0.0f);
        std::fill (smoothedPower.begin(), smoothedPower.end(), 0.0f);
        std::fill (gainSmoothed.begin(), gainSmoothed.end(), 1.0f);
        std::fill (history.begin(), history.end(), 0.0f);
        hopCounter = 0;
        historyPos = 0;
        outGainCurrent = dbToGain (params.outputDb);
        reductionDb.store (0.0f);
    }

    void setParameters (const DeVerbParameters& p) noexcept { params = p; }

    int getLatencySamples() const noexcept { return N; }

    // Average gain reduction of the latest frame, in dB (<= 0). Safe to read from the UI thread.
    float getReductionDb() const noexcept { return reductionDb.load (std::memory_order_relaxed); }

    // In-place. `right` may be nullptr for mono.
    void process (float* left, float* right, int numSamples) noexcept
    {
        if (! prepared)
            return;

        const float outTarget = dbToGain (params.outputDb);
        const float outCoeff = 1.0f - std::exp (-1.0f / (0.02f * sr));
        const bool useRight = stereo && right != nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            const int w = N - hop + hopCounter;
            inL[(size_t) w] = left[i];
            inR[(size_t) w] = useRight ? right[i] : 0.0f;

            outGainCurrent += outCoeff * (outTarget - outGainCurrent);
            left[i] = outL[(size_t) hopCounter] * outGainCurrent;
            if (right != nullptr)
                right[i] = useRight ? outR[(size_t) hopCounter] * outGainCurrent : left[i];

            if (++hopCounter == hop)
            {
                hopCounter = 0;
                processFrame (useRight);
            }
        }
    }

private:
    static float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

    void processFrame (bool useRight) noexcept
    {
        // Analysis. L goes into the real part, R into the imaginary part:
        // one complex FFT handles both channels.
        for (int n = 0; n < N; ++n)
            spectrum[(size_t) n] = { inL[(size_t) n] * window[(size_t) n],
                                     useRight ? inR[(size_t) n] * window[(size_t) n] : 0.0f };
        fft.forward (spectrum.data());

        for (int k = 0; k < bins; ++k)
        {
            const auto z = spectrum[(size_t) k];
            if (useRight)
            {
                const auto zc = std::conj (spectrum[(size_t) ((N - k) & (N - 1))]);
                const auto xl = 0.5f * (z + zc);
                const auto xr = Fft::Complex (0.0f, -0.5f) * (z - zc);
                power[(size_t) k] = 0.5f * (std::norm (xl) + std::norm (xr));
            }
            else
            {
                power[(size_t) k] = std::norm (z);
            }
        }

        computeGains();

        // Apply the (real, symmetric) gain to both halves of the spectrum.
        spectrum[0] *= gainFinal[0];
        spectrum[(size_t) (N / 2)] *= gainFinal[(size_t) (N / 2)];
        for (int k = 1; k < N / 2; ++k)
        {
            spectrum[(size_t) k] *= gainFinal[(size_t) k];
            spectrum[(size_t) (N - k)] *= gainFinal[(size_t) k];
        }

        fft.inverse (spectrum.data());

        // Overlap-add: drop the hop already sent out, then add the new frame.
        std::copy (outL.begin() + hop, outL.end(), outL.begin());
        std::copy (outR.begin() + hop, outR.end(), outR.begin());
        std::fill (outL.end() - hop, outL.end(), 0.0f);
        std::fill (outR.end() - hop, outR.end(), 0.0f);

        constexpr float olaScale = 0.5f; // sum of Hann windows at 75% overlap is 2
        for (int n = 0; n < N; ++n)
        {
            const float wn = window[(size_t) n] * olaScale;
            outL[(size_t) n] += spectrum[(size_t) n].real() * wn;
            outR[(size_t) n] += spectrum[(size_t) n].imag() * wn;
        }

        std::copy (inL.begin() + hop, inL.end(), inL.begin());
        std::copy (inR.begin() + hop, inR.end(), inR.begin());
    }

    void computeGains() noexcept
    {
        const float hopSec = (float) hop / sr;

        // Smooth the power estimate across +-2 bins (periodogram bins are very noisy),
        // then lightly in time (~15 ms).
        const float powerCoeff = std::exp (-hopSec / 0.015f);
        for (int k = 0; k < bins; ++k)
        {
            float sum = 0.0f, wsum = 0.0f;
            for (int j = -2; j <= 2; ++j)
            {
                const int kk = k + j;
                if (kk < 0 || kk >= bins) continue;
                const float w = 3.0f - (float) std::abs (j);
                sum += w * power[(size_t) kk];
                wsum += w;
            }
            smoothedPower[(size_t) k] = powerCoeff * smoothedPower[(size_t) k] + (1.0f - powerCoeff) * (sum / wsum);
        }

        // Store this frame in the history ring.
        historyPos = (historyPos + 1) % historyFrames;
        std::copy (smoothedPower.begin(), smoothedPower.end(), history.begin() + (std::ptrdiff_t) historyPos * bins);

        // Late-reverb prediction from T_early ago.
        const int delayFrames = std::clamp ((int) std::lround (std::clamp (params.earlyMs, 10.0f, kMaxEarlyMs) * 0.001f / hopSec),
                                            1, historyFrames - 1);
        const float tEarly = (float) delayFrames * hopSec;
        const float rt60 = std::clamp (params.decaySec, 0.05f, 10.0f);
        const float decay = std::pow (10.0f, -6.0f * tEarly / rt60);
        const float* past = history.data() + (std::ptrdiff_t) ((historyPos - delayFrames + historyFrames) % historyFrames) * bins;

        const float beta = 1.6f * std::clamp (params.reduction, 0.0f, 1.0f);
        const float floorGain = dbToGain (std::clamp (params.floorDb, -60.0f, 0.0f));
        const float protectHz = std::max (1.0f, params.lowProtectHz);
        const float binHz = sr / (float) N;

        for (int k = 0; k < bins; ++k)
        {
            const float f = (float) k * binHz;
            const float protect = f >= protectHz ? 1.0f : (f / protectHz) * (f / protectHz);
            const float late = decay * past[k];
            const float ratio = late / (smoothedPower[(size_t) k] + 1e-12f);
            const float g = std::sqrt (std::max (0.0f, 1.0f - beta * protect * ratio));
            gainRaw[(size_t) k] = std::max (g, floorGain + (1.0f - floorGain) * (1.0f - protect));
            onset[(size_t) k] = ratio < kOnsetRatio; // much more energy than the room can explain
        }

        // Asymmetric smoothing: on a real note onset the gain opens instantly (keeps
        // attacks); otherwise it moves with attack/release smoothing so the noisy
        // reverb tail does not make the gain flicker.
        const float releaseSec = 0.005f + 0.25f * std::clamp (params.release, 0.0f, 1.0f);
        const float releaseCoeff = 1.0f - std::exp (-hopSec / releaseSec);
        const float attackCoeff = 1.0f - std::exp (-hopSec / 0.03f);
        for (int k = 0; k < bins; ++k)
        {
            float& gs = gainSmoothed[(size_t) k];
            const float g = gainRaw[(size_t) k];
            if (g > gs)
                gs = onset[(size_t) k] ? g : gs + attackCoeff * (g - gs);
            else
                gs += releaseCoeff * (g - gs);
        }

        // Frequency smoothing (reduces "musical noise"), mix and delta listen.
        const float mix = std::clamp (params.mix, 0.0f, 1.0f);
        double energyIn = 0.0, energyOut = 0.0;
        for (int k = 0; k < bins; ++k)
        {
            const float a = gainSmoothed[(size_t) std::max (0, k - 1)];
            const float b = gainSmoothed[(size_t) k];
            const float c = gainSmoothed[(size_t) std::min (bins - 1, k + 1)];
            const float g = 0.25f * a + 0.5f * b + 0.25f * c;
            const float wet = 1.0f - mix * (1.0f - g);

            energyIn  += power[(size_t) k];
            energyOut += power[(size_t) k] * wet * wet;
            gainFinal[(size_t) k] = params.listenRemoved ? 1.0f - wet : wet;
        }

        const float db = energyIn > 1e-12 ? (float) (10.0 * std::log10 (std::max (energyOut / energyIn, 1e-6))) : 0.0f;
        reductionDb.store (db, std::memory_order_relaxed);
    }

    DeVerbParameters params;
    float sr = 44100.0f;
    bool stereo = true, prepared = false;

    Fft fft;
    int N = 2048, hop = 512, bins = 1025, hopCounter = 0;
    int historyFrames = 0, historyPos = 0;

    std::vector<float> window, inL, inR, outL, outR;
    std::vector<Fft::Complex> spectrum;
    std::vector<float> power, smoothedPower, gainRaw, gainSmoothed, gainFinal, history;
    std::vector<char> onset;
    static constexpr float kOnsetRatio = 0.35f;

    float outGainCurrent = 1.0f;
    std::atomic<float> reductionDb { 0.0f };
};

} // namespace aurora
