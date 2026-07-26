#include "fuji_expression_policy.h"

FujiExpression ResolveFujiExpression(const FujiExpressionInputs& inputs) {
    if (!inputs.screen_enabled) {
        return FujiExpression::kPaused;
    }
    if (inputs.fatal_error) {
        return FujiExpression::kError;
    }
    if (inputs.offline) {
        return FujiExpression::kOffline;
    }
    if (inputs.muted) {
        return FujiExpression::kMuted;
    }

    switch (inputs.activity) {
        case FujiExpressionActivity::kListening:
            return FujiExpression::kListening;
        case FujiExpressionActivity::kThinking:
            return FujiExpression::kThinking;
        case FujiExpressionActivity::kConnecting:
            return FujiExpression::kConnecting;
        case FujiExpressionActivity::kSpeaking:
            return FujiExpression::kSpeaking;
        case FujiExpressionActivity::kIdle:
            break;
    }

    switch (inputs.hint) {
        case FujiExpression::kSuccess:
        case FujiExpression::kError:
        case FujiExpression::kOffline:
        case FujiExpression::kThinking:
            return inputs.hint;
        default:
            return FujiExpression::kIdle;
    }
}

FujiExpression FujiExpressionHintFromName(std::string_view name) {
    if (name == "happy" || name == "laughing" || name == "loving" ||
        name == "confident" || name == "delicious" || name == "success") {
        return FujiExpression::kSuccess;
    }
    if (name == "cancel" || name == "sad" || name == "crying" || name == "angry" ||
        name == "error") {
        return FujiExpression::kError;
    }
    if (name == "offline") {
        return FujiExpression::kOffline;
    }
    if (name == "thinking" || name == "confused") {
        return FujiExpression::kThinking;
    }
    return FujiExpression::kIdle;
}

const char* FujiExpressionName(FujiExpression expression) {
    switch (expression) {
        case FujiExpression::kPaused:
            return "paused";
        case FujiExpression::kIdle:
            return "idle";
        case FujiExpression::kListening:
            return "listening";
        case FujiExpression::kThinking:
            return "thinking";
        case FujiExpression::kConnecting:
            return "connecting";
        case FujiExpression::kSpeaking:
            return "speaking";
        case FujiExpression::kInterrupting:
            return "interrupting";
        case FujiExpression::kSuccess:
            return "success";
        case FujiExpression::kError:
            return "error";
        case FujiExpression::kOffline:
            return "offline";
        case FujiExpression::kMuted:
            return "muted";
        case FujiExpression::kCount:
            return "invalid";
    }
    return "invalid";
}
