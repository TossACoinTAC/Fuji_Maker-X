#pragma once

#include "fuji_expression_policy.h"

#include <lvgl.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

class LvglGif;
class LvglRawImage;

class FujiExpressionController {
public:
    explicit FujiExpressionController(lv_obj_t* parent);
    ~FujiExpressionController();

    void SetServerEmotionHint(const char* emotion);
    void SetScreenEnabled(bool enabled);

private:
    struct HeapDeleter {
        void operator()(uint8_t* data) const;
    };

    struct Asset {
        std::unique_ptr<uint8_t, HeapDeleter> storage;
        std::unique_ptr<LvglRawImage> image;
        bool is_gif = false;
    };

    static constexpr uint32_t kFramePeriodMs = 83;
    static constexpr int64_t kMetricsPeriodUs = 60LL * 1000 * 1000;

    static void TimerCallback(lv_timer_t* timer);
    static FujiExpressionActivity MapActivity(int device_state);
    static const char* AssetStem(FujiExpression expression);
    static bool IsAnimatedState(FujiExpression expression);
    static bool GifFrameRateIsSafe(const uint8_t* data, size_t size);

    FujiExpressionInputs ReadInputs() const;
    void LoadAssets();
    void LoadAsset(FujiExpression expression);
    void PrewarmCoreAssets();
    void Tick(bool force = false);
    void ApplyState(FujiExpression expression);
    void ApplyFallback(FujiExpression expression);
    void AnimateFrame();
    void StopAnimation();
    void SetFallbackVisible(bool visible);
    void LogMemoryMetrics();

    lv_obj_t* root_ = nullptr;
    lv_obj_t* face_ = nullptr;
    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;
    lv_obj_t* mouth_ = nullptr;
    std::array<lv_obj_t*, 3> dots_{};
    lv_obj_t* asset_image_ = nullptr;
    lv_timer_t* timer_ = nullptr;

    std::array<Asset, static_cast<size_t>(FujiExpression::kCount)> assets_{};
    std::unique_ptr<LvglGif> gif_;
    FujiExpression current_ = FujiExpression::kCount;
    std::atomic<FujiExpression> hint_{FujiExpression::kIdle};
    bool screen_enabled_ = true;
    bool using_asset_ = false;
    uint8_t frame_ = 0;
    uint16_t uptime_minutes_ = 0;
    int64_t next_metrics_at_us_ = 0;
    size_t warm_internal_free_ = 0;
    size_t warm_psram_free_ = 0;
};
