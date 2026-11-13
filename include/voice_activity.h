#pragma once
// ponytail: energy VAD, not speech recognition. Add wake word for noisy rooms.
struct VoiceActivity {
    unsigned voiced = 0, quiet = 0, frames = 0;
    float noise = 100;
    bool active = false;
    bool update(unsigned level) {
        ++frames;
        if (!active) {
            if (frames < 125) { noise = noise * .96f + level * .04f; return false; }
            unsigned threshold = noise * 3 + 120;
            if (level > threshold) ++voiced;
            else { voiced = 0; noise = noise * .995f + level * .005f; }
            if (voiced >= 5) { active = true; quiet = 0; return true; }
        } else quiet = level > noise * 2 + 100 ? 0 : quiet + 1;
        return false;
    }
    bool finished() const { return active && quiet >= 50; }
    void reset() { active = false; voiced = quiet = 0; }
};
