#include "grokbot_player.h"

#include <string.h>

#include "esp_log.h"

namespace {
constexpr const char *TAG = "grokbot";
constexpr size_t kGlobalHeaderSize = 16;
constexpr size_t kClipHeaderSize = 32;
constexpr size_t kFrameIndexSize = 8;
constexpr size_t kFrameHeaderSize = 8;

uint16_t read_u16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t read_u32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
}  // namespace

bool GrokBotPlayer::init(const uint8_t *data, size_t size)
{
    if (data == nullptr || size < kGlobalHeaderSize ||
        memcmp(data, "GBZ1", 4) != 0 || read_u16(data + 4) != 1) {
        ESP_LOGE(TAG, "Invalid GrokBot atlas");
        return false;
    }

    data_ = data;
    data_size_ = size;
    width_ = read_u16(data + 6);
    height_ = read_u16(data + 8);
    fps_ = data[10];
    clip_count_ = data[11];
    if (width_ == 0 || height_ == 0 || fps_ == 0 || clip_count_ <= 0 ||
        clip_count_ > static_cast<int>(sizeof(clips_) / sizeof(clips_[0]))) {
        ESP_LOGE(TAG, "Invalid atlas metadata");
        return false;
    }

    const size_t table_end = kGlobalHeaderSize + static_cast<size_t>(clip_count_) * kClipHeaderSize;
    if (table_end > size) {
        ESP_LOGE(TAG, "Atlas clip table truncated");
        return false;
    }

    for (int i = 0; i < clip_count_; ++i) {
        const uint8_t *header = data + kGlobalHeaderSize + static_cast<size_t>(i) * kClipHeaderSize;
        ClipInfo &clip = clips_[i];
        clip.name = reinterpret_cast<const char *>(header);
        clip.frames = read_u16(header + 16);
        clip.start_frame = read_u16(header + 18);
        clip.offset = read_u32(header + 20);
        clip.size = read_u32(header + 24);
        clip.packed_frame_size = read_u32(header + 28);
        if (clip.frames == 0 || clip.start_frame >= clip.frames ||
            clip.offset < table_end || clip.offset > size ||
            clip.size > size - clip.offset || clip.packed_frame_size == 0 ||
            static_cast<size_t>(clip.frames) * kFrameIndexSize > clip.size) {
            ESP_LOGE(TAG, "Invalid clip %d", i);
            return false;
        }
    }

    return select(0);
}

bool GrokBotPlayer::select(int index)
{
    if (index < 0 || index >= clip_count_) {
        return false;
    }
    selected_ = index;
    packed_frame_size_ = clips_[index].packed_frame_size;
    current_index_ = 0;
    return true;
}

void GrokBotPlayer::restart()
{
    current_index_ = 0;
}

bool GrokBotPlayer::seek(int frame_index)
{
    if (selected_ < 0 || frame_index < 0 || frame_index >= clips_[selected_].frames) {
        return false;
    }
    current_index_ = frame_index;
    return true;
}

bool GrokBotPlayer::retreat()
{
    if (selected_ < 0 || current_index_ <= 0) {
        return false;
    }
    --current_index_;
    return true;
}

int GrokBotPlayer::selected_frame_count() const
{
    return selected_ < 0 ? 0 : clips_[selected_].frames;
}

int GrokBotPlayer::start_frame() const
{
    return selected_ < 0 ? 0 : static_cast<int>(clips_[selected_].start_frame);
}

bool GrokBotPlayer::frame_info(int frame_index, FrameInfo *frame) const
{
    if (frame == nullptr || selected_ < 0 ||
        frame_index < 0 || frame_index >= clips_[selected_].frames) {
        return false;
    }
    const ClipInfo &clip = clips_[selected_];
    const uint8_t *index_entry = data_ + clip.offset + static_cast<size_t>(frame_index) * kFrameIndexSize;
    const uint32_t record_offset = read_u32(index_entry);
    const uint32_t size_flags = read_u32(index_entry + 4);
    const uint32_t compressed_size = size_flags & 0x7fffffffU;
    if (record_offset + kFrameHeaderSize + compressed_size > clip.size) {
        return false;
    }
    const uint8_t *record = data_ + clip.offset + record_offset;
    if (read_u32(record) != compressed_size) {
        return false;
    }
    frame->data = record + kFrameHeaderSize;
    frame->compressed_size = compressed_size;
    frame->delta = (size_flags >> 31) != 0;
    return true;
}

bool GrokBotPlayer::current_frame(FrameInfo *frame) const
{
    return frame_info(current_index_, frame);
}

bool GrokBotPlayer::advance()
{
    if (selected_ < 0) {
        return true;
    }
    ++current_index_;
    if (current_index_ >= clips_[selected_].frames) {
        current_index_ = clips_[selected_].start_frame;
        return true;
    }
    return false;
}

const char *GrokBotPlayer::clip_name(int index) const
{
    if (index < 0 || index >= clip_count_) {
        return "";
    }
    return clips_[index].name;
}

const char *GrokBotPlayer::selected_name() const
{
    return clip_name(selected_);
}
