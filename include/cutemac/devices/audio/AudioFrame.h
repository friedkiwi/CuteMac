#pragma once

#include <QByteArray>

namespace cutemac::devices::audio {

enum class SampleFormat {
    SignedInt16,
};

// Frontend-neutral interleaved PCM produced by an emulated audio device.
struct AudioFrame {
    int sampleRate = 0;
    int channelCount = 0;
    SampleFormat format = SampleFormat::SignedInt16;
    QByteArray samples;

    [[nodiscard]] bool isValid() const
    {
        return sampleRate > 0 && channelCount > 0 && !samples.isEmpty();
    }
};

} // namespace cutemac::devices::audio
