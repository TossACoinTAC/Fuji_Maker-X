#pragma once

#include <string_view>

enum class FujiExpressionActivity {
    kIdle,
    kListening,
    kThinking,
    kConnecting,
    kSpeaking,
};

enum class FujiExpression {
    kPaused,
    kIdle,
    kListening,
    kThinking,
    kConnecting,
    kSpeaking,
    kSuccess,
    kError,
    kOffline,
    kMuted,
    kCount,
};

struct FujiExpressionInputs {
    bool screen_enabled = true;
    bool fatal_error = false;
    bool offline = false;
    bool muted = false;
    FujiExpressionActivity activity = FujiExpressionActivity::kIdle;
    FujiExpression hint = FujiExpression::kIdle;
};

FujiExpression ResolveFujiExpression(const FujiExpressionInputs& inputs);
FujiExpression FujiExpressionHintFromName(std::string_view name);
const char* FujiExpressionName(FujiExpression expression);
