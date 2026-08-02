#pragma once

#include <memory>

#include <QByteArray>
#include <QObject>

#include "cutemac/devices/audio/AudioFrame.h"

class QAudioSink;
class QIODevice;

namespace cutemac::session {

// Qt-side consumer for the frontend-neutral PCM stream exposed by IMachine.
class AudioOutput final : public QObject {
public:
    explicit AudioOutput(QObject* parent = nullptr);
    ~AudioOutput() override;

    [[nodiscard]] bool enqueue(devices::audio::AudioFrame frame);
    void setPaused(bool paused);

private:
    void configure(const devices::audio::AudioFrame& frame);
    void pump();

    std::unique_ptr<QAudioSink> m_sink;
    QIODevice* m_device = nullptr;
    QByteArray m_pending;
    int m_sampleRate = 0;
    int m_channelCount = 0;
    bool m_paused = false;
};

} // namespace cutemac::session
