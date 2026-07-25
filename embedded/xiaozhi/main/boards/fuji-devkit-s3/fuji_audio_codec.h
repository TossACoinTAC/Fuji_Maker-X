#ifndef FUJI_AUDIO_CODEC_H
#define FUJI_AUDIO_CODEC_H

#include "audio/codecs/no_audio_codec.h"

#include <atomic>

class FujiAudioCodec : public NoAudioCodecDuplex {
private:
    std::atomic_bool software_muted_{false};

protected:
    int Read(int16_t* dest, int samples) override;
    void EnableOutput(bool enable) override;

public:
    FujiAudioCodec();

    bool ToggleSoftwareMute();
    void RunSelfTest();
};

void InitializeFujiAmplifierSafeState();

#endif  // FUJI_AUDIO_CODEC_H
