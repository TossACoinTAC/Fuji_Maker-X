#include "fuji_expression_policy.h"

#include <cassert>
#include <iostream>

int main() {
    FujiExpressionInputs inputs;
    assert(ResolveFujiExpression(inputs) == FujiExpression::kIdle);

    inputs.hint = FujiExpression::kSuccess;
    assert(ResolveFujiExpression(inputs) == FujiExpression::kSuccess);

    inputs.activity = FujiExpressionActivity::kSpeaking;
    assert(ResolveFujiExpression(inputs) == FujiExpression::kSpeaking);

    inputs.muted = true;
    assert(ResolveFujiExpression(inputs) == FujiExpression::kMuted);

    inputs.offline = true;
    assert(ResolveFujiExpression(inputs) == FujiExpression::kOffline);

    inputs.fatal_error = true;
    assert(ResolveFujiExpression(inputs) == FujiExpression::kError);

    inputs.screen_enabled = false;
    assert(ResolveFujiExpression(inputs) == FujiExpression::kPaused);

    assert(FujiExpressionHintFromName("happy") == FujiExpression::kSuccess);
    assert(FujiExpressionHintFromName("cancel") == FujiExpression::kError);
    assert(FujiExpressionHintFromName("offline") == FujiExpression::kOffline);
    assert(FujiExpressionHintFromName("thinking") == FujiExpression::kThinking);
    assert(FujiExpressionHintFromName("surprised") == FujiExpression::kIdle);

    std::cout << "Fuji expression policy tests passed\n";
    return 0;
}
