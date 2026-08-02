#include "cutemac/session/AudioOutput.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include <cstdint>
#include <cstring>

namespace cutemac::session {

AudioOutput::AudioOutput(QObject* parent)
    : QObject(parent)
{
}

AudioOutput::~AudioOutput() = default;

void AudioOutput::configure(const devices::audio::AudioFrame& frame)
{
    m_pending.clear();
    m_device = nullptr;
    m_sink.reset();

    QAudioFormat format;
    format.setSampleRate(frame.sampleRate);
    format.setChannelCount(frame.channelCount);
    format.setSampleFormat(QAudioFormat::Int16);

    const auto output = QMediaDevices::defaultAudioOutput();
    if (output.isNull() || !output.isFormatSupported(format)) {
        qWarning("No audio output supports %d Hz, %d-channel signed 16-bit PCM",
            frame.sampleRate, frame.channelCount);
        return;
    }

    m_sink = std::make_unique<QAudioSink>(output, format, this);
    m_sink->setBufferSize(format.bytesForDuration(100000));
    m_device = m_sink->start();
    m_sampleRate = frame.sampleRate;
    m_channelCount = frame.channelCount;
    if (m_paused && m_sink) {
        m_sink->suspend();
    }
}

bool AudioOutput::enqueue(devices::audio::AudioFrame frame)
{
    if (!frame.isValid() || frame.format != devices::audio::SampleFormat::SignedInt16) {
        return false;
    }
    bool audible = false;
    for (qsizetype offset = 0; offset + qsizetype(sizeof(std::int16_t)) <= frame.samples.size();
         offset += sizeof(std::int16_t)) {
        std::int16_t sample = 0;
        std::memcpy(&sample, frame.samples.constData() + offset, sizeof(sample));
        if (sample != 0) {
            audible = true;
            break;
        }
    }
    if (!m_sink || frame.sampleRate != m_sampleRate || frame.channelCount != m_channelCount) {
        configure(frame);
    }
    if (!m_sink || !m_device || m_paused) {
        return false;
    }

    m_pending.append(frame.samples);
    const auto maximumBytes = frame.sampleRate * frame.channelCount * 2 / 2;
    if (m_pending.size() > maximumBytes) {
        m_pending.remove(0, m_pending.size() - maximumBytes);
    }
    pump();
    return audible;
}

void AudioOutput::pump()
{
    if (!m_sink || !m_device || m_pending.isEmpty()) {
        return;
    }
    const auto count = qMin<qsizetype>(m_pending.size(), m_sink->bytesFree());
    if (count <= 0) {
        return;
    }
    const auto written = m_device->write(m_pending.constData(), count);
    if (written > 0) {
        m_pending.remove(0, written);
    }
}

void AudioOutput::setPaused(bool paused)
{
    m_paused = paused;
    if (!m_sink) {
        return;
    }
    if (paused) {
        m_pending.clear();
        m_sink->suspend();
    } else {
        m_sink->resume();
    }
}

} // namespace cutemac::session
