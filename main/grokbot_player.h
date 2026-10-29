#pragma once

#include <stddef.h>
#include <stdint.h>

class GrokBotPlayer {
public:
    struct FrameInfo {
        const uint8_t *data;
        uint32_t compressed_size;
        bool delta;
    };

    bool init(const uint8_t *data, size_t size);

    int clip_count() const { return clip_count_; }
    uint16_t width() const { return width_; }
    uint16_t height() const { return height_; }
    uint8_t fps() const { return fps_; }

    bool select(int index);
    void restart();
    bool seek(int frame_index);
    bool retreat();
    int current_index() const { return current_index_; }
    int selected_frame_count() const;
    int start_frame() const;
    bool frame_info(int frame_index, FrameInfo *frame) const;
    bool current_frame(FrameInfo *frame) const;
    bool advance();

    const char *clip_name(int index) const;
    int selected_index() const { return selected_; }
    const char *selected_name() const;
    uint32_t packed_frame_size() const { return packed_frame_size_; }

private:
    struct ClipInfo {
        const char *name;
        uint16_t frames;
        uint16_t start_frame;
        uint32_t offset;
        uint32_t size;
        uint32_t packed_frame_size;
    };

    const uint8_t *data_ = nullptr;
    size_t data_size_ = 0;
    uint16_t width_ = 0;
    uint16_t height_ = 0;
    uint8_t fps_ = 0;
    int clip_count_ = 0;
    int selected_ = -1;
    int current_index_ = 0;
    uint32_t packed_frame_size_ = 0;
    ClipInfo clips_[64] = {};
};
