#include "fuji_expression_controller.h"

#include "application.h"
#include "assets.h"
#include "audio/audio_codec.h"
#include "board.h"
#include "device_state.h"
#include "display/lvgl_display/gif/lvgl_gif.h"
#include "display/lvgl_display/lvgl_image.h"
#include "wifi_manager.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <array>
#include <cstring>
#include <string>

#define TAG "FujiExpression"

namespace {

constexpr size_t ExpressionIndex(FujiExpression expression) {
    return static_cast<size_t>(expression);
}

void StyleBlob(lv_obj_t* object, uint32_t color) {
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

}  // namespace

FujiExpressionController::FujiExpressionController(lv_obj_t* parent) {
    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, 320, 320);
    lv_obj_center(root_);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    face_ = lv_obj_create(root_);
    lv_obj_set_size(face_, 296, 296);
    lv_obj_center(face_);
    StyleBlob(face_, 0xEAF3EF);

    left_eye_ = lv_obj_create(root_);
    right_eye_ = lv_obj_create(root_);
    mouth_ = lv_obj_create(root_);
    StyleBlob(left_eye_, 0x17211D);
    StyleBlob(right_eye_, 0x17211D);
    StyleBlob(mouth_, 0x2A8E69);

    for (size_t i = 0; i < dots_.size(); ++i) {
        dots_[i] = lv_obj_create(root_);
        lv_obj_set_size(dots_[i], 14, 14);
        lv_obj_align(dots_[i], LV_ALIGN_CENTER, -24 + static_cast<int>(i) * 24, 86);
        StyleBlob(dots_[i], 0xE2A83B);
    }

    asset_image_ = lv_image_create(root_);
    lv_obj_center(asset_image_);
    lv_obj_add_flag(asset_image_, LV_OBJ_FLAG_HIDDEN);

    LoadAssets();
    PrewarmCoreAssets();
    next_metrics_at_us_ = esp_timer_get_time() + kMetricsPeriodUs;
    timer_ = lv_timer_create(TimerCallback, kFramePeriodMs, this);
    Tick(true);
}

FujiExpressionController::~FujiExpressionController() {
    StopAnimation();
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void FujiExpressionController::HeapDeleter::operator()(uint8_t* data) const {
    heap_caps_free(data);
}

void FujiExpressionController::SetServerEmotionHint(const char* emotion) {
    hint_.store(FujiExpressionHintFromName(emotion != nullptr ? emotion : "neutral"),
                std::memory_order_relaxed);
}

void FujiExpressionController::SetScreenEnabled(bool enabled) {
    if (screen_enabled_ == enabled) {
        return;
    }
    screen_enabled_ = enabled;
    if (!enabled) {
        ApplyState(FujiExpression::kPaused);
        if (timer_ != nullptr) {
            lv_timer_pause(timer_);
        }
        return;
    }

    frame_ = 0;
    if (timer_ != nullptr) {
        lv_timer_resume(timer_);
        lv_timer_reset(timer_);
    }
    Tick(true);
}

void FujiExpressionController::TriggerBargeInTransition() {
    interrupting_request_until_us_.store(esp_timer_get_time() + 500 * 1000,
                                         std::memory_order_relaxed);
}

void FujiExpressionController::TimerCallback(lv_timer_t* timer) {
    auto* controller = static_cast<FujiExpressionController*>(lv_timer_get_user_data(timer));
    if (controller != nullptr) {
        controller->Tick();
    }
}

FujiExpressionActivity FujiExpressionController::MapActivity(int device_state) {
    switch (static_cast<DeviceState>(device_state)) {
        case kDeviceStateListening:
        case kDeviceStateAudioTesting:
            return FujiExpressionActivity::kListening;
        case kDeviceStateConnecting:
        case kDeviceStateUpgrading:
            return FujiExpressionActivity::kThinking;
        case kDeviceStateSpeaking:
            return FujiExpressionActivity::kSpeaking;
        case kDeviceStateStarting:
        case kDeviceStateWifiConfiguring:
        case kDeviceStateActivating:
            return FujiExpressionActivity::kConnecting;
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
        case kDeviceStateFatalError:
            return FujiExpressionActivity::kIdle;
    }
    return FujiExpressionActivity::kIdle;
}

const char* FujiExpressionController::AssetStem(FujiExpression expression) {
    switch (expression) {
        case FujiExpression::kIdle:
            return "fuji_idle";
        case FujiExpression::kListening:
            return "fuji_listening";
        case FujiExpression::kThinking:
            return "fuji_thinking";
        case FujiExpression::kConnecting:
            return "fuji_connecting";
        case FujiExpression::kSpeaking:
            return "fuji_speaking";
        case FujiExpression::kInterrupting:
            return nullptr;
        case FujiExpression::kSuccess:
            return "fuji_success";
        case FujiExpression::kError:
            return "fuji_error";
        case FujiExpression::kOffline:
            return "fuji_offline";
        case FujiExpression::kMuted:
            return "fuji_muted";
        case FujiExpression::kPaused:
        case FujiExpression::kCount:
            return nullptr;
    }
    return nullptr;
}

bool FujiExpressionController::IsAnimatedState(FujiExpression expression) {
    return expression == FujiExpression::kListening || expression == FujiExpression::kThinking ||
           expression == FujiExpression::kConnecting || expression == FujiExpression::kSpeaking ||
           expression == FujiExpression::kInterrupting;
}

bool FujiExpressionController::GifFrameRateIsSafe(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 16 || data[0] != 'G' || data[1] != 'I' || data[2] != 'F') {
        return false;
    }

    bool found_frame_delay = false;
    for (size_t i = 0; i + 7 < size; ++i) {
        if (data[i] != 0x21 || data[i + 1] != 0xF9 || data[i + 2] != 0x04) {
            continue;
        }
        const uint16_t delay_centiseconds =
            static_cast<uint16_t>(data[i + 4]) |
            static_cast<uint16_t>(static_cast<uint16_t>(data[i + 5]) << 8);
        if (delay_centiseconds < 9) {
            return false;
        }
        found_frame_delay = true;
    }
    return found_frame_delay;
}

FujiExpressionInputs FujiExpressionController::ReadInputs() const {
    const DeviceState state = Application::GetInstance().GetDeviceState();
    auto& wifi = WifiManager::GetInstance();
    const bool connecting_wifi = state == kDeviceStateStarting ||
                                 state == kDeviceStateWifiConfiguring ||
                                 state == kDeviceStateActivating;

    auto* codec = Board::GetInstance().GetAudioCodec();
    return {
        .screen_enabled = screen_enabled_,
        .fatal_error = state == kDeviceStateFatalError,
        .offline = wifi.IsInitialized() && !wifi.IsConnected() && !connecting_wifi,
        .muted = codec != nullptr && codec->output_volume() == 0,
        .activity = MapActivity(state),
        .hint = hint_.load(std::memory_order_relaxed),
    };
}

void FujiExpressionController::LoadAssets() {
    for (size_t i = ExpressionIndex(FujiExpression::kIdle);
         i < ExpressionIndex(FujiExpression::kCount); ++i) {
        LoadAsset(static_cast<FujiExpression>(i));
    }
}

void FujiExpressionController::LoadAsset(FujiExpression expression) {
    const char* stem = AssetStem(expression);
    if (stem == nullptr) {
        return;
    }

    auto& assets = Assets::GetInstance();
    void* data = nullptr;
    size_t size = 0;
    bool is_gif = false;
    std::string filename;
    if (IsAnimatedState(expression)) {
        filename = std::string(stem) + ".gif";
        is_gif = assets.GetAssetData(filename, data, size);
    }
    if (!is_gif) {
        filename = std::string(stem) + ".png";
        if (!assets.GetAssetData(filename, data, size)) {
            ESP_LOGW(TAG, "%s asset missing; LVGL fallback enabled", stem);
            return;
        }
    }

    auto* storage = static_cast<uint8_t*>(
        heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (storage == nullptr) {
        storage = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
        ESP_LOGW(TAG, "%s did not fit in PSRAM; using internal-capable heap", filename.c_str());
    }
    if (storage == nullptr) {
        ESP_LOGE(TAG, "%s allocation failed; using fallback", filename.c_str());
        return;
    }
    std::memcpy(storage, data, size);

    auto image = std::make_unique<LvglRawImage>(storage, size);
    if (is_gif) {
        if (!GifFrameRateIsSafe(storage, size)) {
            ESP_LOGE(TAG, "%s exceeds 12 fps or has no valid frame delay; using fallback",
                     filename.c_str());
            heap_caps_free(storage);
            return;
        }
    } else {
        lv_image_header_t header = {};
        if (lv_image_decoder_get_info(image->image_dsc(), &header) != LV_RESULT_OK ||
            header.w != 320 || header.h != 320) {
            ESP_LOGE(TAG, "%s must decode as a 320x320 image; using fallback", filename.c_str());
            heap_caps_free(storage);
            return;
        }
    }

    Asset asset;
    asset.storage.reset(storage);
    asset.image = std::move(image);
    asset.is_gif = is_gif;
    assets_[ExpressionIndex(expression)] = std::move(asset);
}

void FujiExpressionController::PrewarmCoreAssets() {
    static constexpr std::array<FujiExpression, 5> kCoreStates = {
        FujiExpression::kConnecting,
        FujiExpression::kThinking,
        FujiExpression::kListening,
        FujiExpression::kSpeaking,
        FujiExpression::kIdle,
    };

    const int64_t started_at_us = esp_timer_get_time();
    size_t warmed = 0;
    for (FujiExpression expression : kCoreStates) {
        auto& asset = assets_[ExpressionIndex(expression)];
        if (asset.image == nullptr || asset.is_gif) {
            continue;
        }
        lv_image_set_src(asset_image_, asset.image->image_dsc());
        SetFallbackVisible(false);
        lv_obj_remove_flag(asset_image_, LV_OBJ_FLAG_HIDDEN);
        lv_refr_now(nullptr);
        ++warmed;
    }
    lv_obj_add_flag(asset_image_, LV_OBJ_FLAG_HIDDEN);
    SetFallbackVisible(true);

    ESP_LOGI(TAG, "prewarmed %u core PNG assets in %lld ms", static_cast<unsigned>(warmed),
             (esp_timer_get_time() - started_at_us) / 1000);
}

void FujiExpressionController::Tick(bool force) {
    const int64_t now_us = esp_timer_get_time();
    const FujiExpression resolved = ResolveFujiExpression(ReadInputs());
    const int64_t request_until_us =
        interrupting_request_until_us_.load(std::memory_order_relaxed);
    if (resolved == FujiExpression::kListening && request_until_us >= now_us) {
        interrupting_until_us_ = now_us + kInterruptingDurationUs;
        interrupting_request_until_us_.store(0, std::memory_order_relaxed);
    } else if (request_until_us != 0 && request_until_us < now_us) {
        interrupting_request_until_us_.store(0, std::memory_order_relaxed);
    }
    const FujiExpression next =
        resolved == FujiExpression::kListening && now_us < interrupting_until_us_
            ? FujiExpression::kInterrupting
            : resolved;
    if (force || next != current_) {
        ApplyState(next);
    }
    if (next != FujiExpression::kPaused) {
        AnimateFrame();
    }
    if (now_us >= next_metrics_at_us_) {
        const int64_t elapsed_periods =
            1 + (now_us - next_metrics_at_us_) / kMetricsPeriodUs;
        uptime_minutes_ = static_cast<uint16_t>(uptime_minutes_ + elapsed_periods);
        next_metrics_at_us_ += elapsed_periods * kMetricsPeriodUs;
        LogMemoryMetrics();
    }
}

void FujiExpressionController::ApplyState(FujiExpression expression) {
    StopAnimation();
    current_ = expression;
    frame_ = 0;

    if (expression == FujiExpression::kPaused) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);

    auto& asset = assets_[ExpressionIndex(expression)];
    if (asset.image != nullptr) {
        if (asset.is_gif) {
            gif_ = std::make_unique<LvglGif>(asset.image->image_dsc());
            if (gif_->IsLoaded() && gif_->width() == 320 && gif_->height() == 320) {
                gif_->SetFrameCallback(
                    [this]() { lv_image_set_src(asset_image_, gif_->image_dsc()); });
                lv_image_set_src(asset_image_, gif_->image_dsc());
                gif_->Start();
                using_asset_ = true;
            } else {
                ESP_LOGE(TAG, "%s GIF must be 320x320; using fallback",
                         FujiExpressionName(expression));
                gif_.reset();
            }
        } else {
            lv_image_set_src(asset_image_, asset.image->image_dsc());
            using_asset_ = true;
        }
    }

    if (using_asset_) {
        SetFallbackVisible(false);
        lv_image_set_scale(asset_image_, 256);
        lv_obj_set_style_opa(asset_image_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(asset_image_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(asset_image_, LV_OBJ_FLAG_HIDDEN);
        SetFallbackVisible(true);
        ApplyFallback(expression);
    }
    ESP_LOGI(TAG, "expression=%s source=%s", FujiExpressionName(expression),
             using_asset_ ? (asset.is_gif ? "gif" : "png") : "lvgl");
}

void FujiExpressionController::ApplyFallback(FujiExpression expression) {
    constexpr uint32_t kInk = 0x17211D;
    constexpr uint32_t kGreen = 0x2A8E69;
    constexpr uint32_t kYellow = 0xE2A83B;
    constexpr uint32_t kRed = 0xC84A4A;
    constexpr uint32_t kGray = 0x69736F;

    lv_obj_set_style_bg_color(face_, lv_color_hex(0xEAF3EF), 0);
    lv_obj_set_style_bg_color(left_eye_, lv_color_hex(kInk), 0);
    lv_obj_set_style_bg_color(right_eye_, lv_color_hex(kInk), 0);
    lv_obj_set_style_bg_color(mouth_, lv_color_hex(kGreen), 0);
    lv_obj_set_size(left_eye_, 46, 72);
    lv_obj_set_size(right_eye_, 46, 72);
    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -62, -38);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, 62, -38);
    lv_obj_set_size(mouth_, 76, 16);
    lv_obj_align(mouth_, LV_ALIGN_CENTER, 0, 62);
    for (auto* dot : dots_) {
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(dot, lv_color_hex(kYellow), 0);
    }

    switch (expression) {
        case FujiExpression::kListening:
            lv_obj_set_size(left_eye_, 54, 82);
            lv_obj_set_size(right_eye_, 54, 82);
            for (auto* dot : dots_) {
                lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
            }
            break;
        case FujiExpression::kThinking:
            lv_obj_set_size(right_eye_, 30, 30);
            lv_obj_set_size(mouth_, 32, 16);
            lv_obj_align(mouth_, LV_ALIGN_CENTER, 25, 62);
            for (auto* dot : dots_) {
                lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
            }
            break;
        case FujiExpression::kConnecting:
            lv_obj_set_style_bg_color(mouth_, lv_color_hex(kYellow), 0);
            for (auto* dot : dots_) {
                lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
            }
            break;
        case FujiExpression::kSpeaking:
            lv_obj_set_size(mouth_, 94, 42);
            break;
        case FujiExpression::kInterrupting:
            lv_obj_set_style_bg_color(mouth_, lv_color_hex(kYellow), 0);
            lv_obj_set_size(left_eye_, 58, 86);
            lv_obj_set_size(right_eye_, 58, 86);
            lv_obj_set_size(mouth_, 54, 18);
            for (auto* dot : dots_) {
                lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
            }
            break;
        case FujiExpression::kSuccess:
            lv_obj_set_size(left_eye_, 60, 14);
            lv_obj_set_size(right_eye_, 60, 14);
            lv_obj_set_size(mouth_, 104, 18);
            break;
        case FujiExpression::kError:
            lv_obj_set_style_bg_color(face_, lv_color_hex(0xF6E9E9), 0);
            lv_obj_set_style_bg_color(left_eye_, lv_color_hex(kRed), 0);
            lv_obj_set_style_bg_color(right_eye_, lv_color_hex(kRed), 0);
            lv_obj_set_style_bg_color(mouth_, lv_color_hex(kRed), 0);
            lv_obj_set_size(left_eye_, 58, 16);
            lv_obj_set_size(right_eye_, 58, 16);
            break;
        case FujiExpression::kOffline:
            lv_obj_set_style_bg_color(face_, lv_color_hex(0xE7EAE9), 0);
            lv_obj_set_style_bg_color(left_eye_, lv_color_hex(kGray), 0);
            lv_obj_set_style_bg_color(right_eye_, lv_color_hex(kGray), 0);
            lv_obj_set_style_bg_color(mouth_, lv_color_hex(kGray), 0);
            lv_obj_set_size(left_eye_, 58, 12);
            lv_obj_set_size(right_eye_, 58, 12);
            break;
        case FujiExpression::kMuted:
            lv_obj_set_style_bg_color(mouth_, lv_color_hex(kRed), 0);
            lv_obj_set_size(mouth_, 112, 12);
            break;
        case FujiExpression::kIdle:
        case FujiExpression::kPaused:
        case FujiExpression::kCount:
            break;
    }
}

void FujiExpressionController::AnimateFrame() {
    if (!IsAnimatedState(current_)) {
        return;
    }
    static constexpr std::array<int16_t, 8> kPulse = {0, 1, 2, 1, 0, -1, -2, -1};
    const int16_t pulse = kPulse[frame_ % kPulse.size()];
    frame_ = static_cast<uint8_t>((frame_ + 1) % kPulse.size());

    if (using_asset_ && gif_ == nullptr) {
        lv_image_set_scale(asset_image_, static_cast<uint16_t>(256 + pulse * 2));
        return;
    }
    if (using_asset_) {
        return;
    }

    if (current_ == FujiExpression::kSpeaking) {
        lv_obj_set_height(mouth_, 42 + pulse * 4);
    } else if (current_ == FujiExpression::kListening ||
               current_ == FujiExpression::kConnecting ||
               current_ == FujiExpression::kThinking ||
               current_ == FujiExpression::kInterrupting) {
        for (size_t i = 0; i < dots_.size(); ++i) {
            const uint8_t step = static_cast<uint8_t>((frame_ + i * 2) % kPulse.size());
            lv_obj_set_style_opa(dots_[i], static_cast<lv_opa_t>(120 + step * 16), 0);
        }
    }
}

void FujiExpressionController::StopAnimation() {
    if (gif_ != nullptr) {
        gif_->Stop();
        gif_.reset();
    }
    if (asset_image_ != nullptr) {
        lv_image_set_scale(asset_image_, 256);
        lv_obj_set_style_opa(asset_image_, LV_OPA_COVER, 0);
    }
    using_asset_ = false;
    frame_ = 0;
}

void FujiExpressionController::SetFallbackVisible(bool visible) {
    const std::array<lv_obj_t*, 7> objects = {
        face_, left_eye_, right_eye_, mouth_, dots_[0], dots_[1], dots_[2],
    };
    for (auto* object : objects) {
        if (visible) {
            lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void FujiExpressionController::LogMemoryMetrics() {
    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const bool warm_baseline = warm_internal_free_ == 0 && uptime_minutes_ >= 5;
    if (warm_baseline) {
        warm_internal_free_ = internal_free;
        warm_psram_free_ = psram_free;
    }

    const int32_t internal_drift =
        warm_internal_free_ == 0
            ? 0
            : static_cast<int32_t>(static_cast<int64_t>(warm_internal_free_) -
                                   static_cast<int64_t>(internal_free));
    const int32_t psram_drift =
        warm_psram_free_ == 0
            ? 0
            : static_cast<int32_t>(static_cast<int64_t>(warm_psram_free_) -
                                   static_cast<int64_t>(psram_free));
    ESP_LOGI(TAG,
             "memory minute=%u internal_free=%u psram_free=%u drift_internal=%d "
             "drift_psram=%d%s",
             static_cast<unsigned>(uptime_minutes_), static_cast<unsigned>(internal_free),
             static_cast<unsigned>(psram_free), static_cast<int>(internal_drift),
             static_cast<int>(psram_drift), warm_baseline ? " warm_baseline" : "");
}
