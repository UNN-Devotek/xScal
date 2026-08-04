#include "scaleform/bridge.hpp"
#include "platform/windows_memory.hpp"
#include "scaleform/scaleform_layout.hpp"

#include <cstring>
#include <limits>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace sf {
namespace {

thread_local char returnStringBuffer[layout::kReturnStringBufferSize]{};


template <typename T>
[[nodiscard]] T ReadValue(const ScaleformValue& value, std::size_t offset) noexcept {
    T result{};
    std::memcpy(&result, value.storage + offset, sizeof(result));
    return result;
}

[[nodiscard]] bool WriteBooleanResult(void* rawResult, bool booleanValue) noexcept {
    if (rawResult == nullptr) {
        return false;
    }

    std::byte value[sizeof(ScaleformValue)]{};
    constexpr std::uint64_t kBooleanType = layout::kBooleanType;
    const std::uint64_t encodedValue = booleanValue ? 1 : 0;
    std::memcpy(value + layout::kValueTypeOffset, &kBooleanType, sizeof(kBooleanType));
    std::memcpy(value + layout::kValueInternalTwoOffset, &encodedValue, sizeof(encodedValue));
    std::memcpy(rawResult, value, sizeof(value));
    return true;
}

[[nodiscard]] bool WriteStringResult(
    void* rawResult,
    const char* text,
    std::size_t textLength) noexcept {
    if (rawResult == nullptr || text == nullptr) {
        return false;
    }

    const std::size_t copiedLength =
        textLength < layout::kMaxReturnStringLength ? textLength : layout::kMaxReturnStringLength;
    std::memcpy(returnStringBuffer, text, copiedLength);
    returnStringBuffer[copiedLength] = '\0';

    std::byte value[sizeof(ScaleformValue)]{};
    constexpr std::uint64_t kStringType = layout::kStringType;
    const char* const valueText = returnStringBuffer;
    std::memcpy(value + layout::kValueTypeOffset, &kStringType, sizeof(kStringType));
    std::memcpy(value + layout::kValueInternalTwoOffset, &valueText, sizeof(valueText));
    std::memcpy(rawResult, value, sizeof(value));
    return true;
}

template <typename T>
[[nodiscard]] T ReadArgumentValue(const std::byte* argument, std::size_t offset) noexcept {
    T result{};
    std::memcpy(&result, argument + offset, sizeof(result));
    return result;
}


[[nodiscard]] bool IsScaleformStringByteAllowed(unsigned char value) noexcept {
    return value >= 0x20 || value == '\t' || value == '\n' || value == '\r';
}

[[nodiscard]] bool IsReadableRange(const void* address, std::size_t size) noexcept {
    return platform::IsReadableRange(&::VirtualQuery, address, size);
}

[[nodiscard]] bool GetReadableSpanEnd(
    const char* current,
    std::size_t remaining,
    const char*& spanEnd) noexcept {
    return platform::GetReadableSpanEnd(&::VirtualQuery, current, remaining, spanEnd);
}
[[nodiscard]] bool IsUtf8Continuation(unsigned char value) noexcept {
    return value >= 0x80 && value <= 0xBF;
}

[[nodiscard]] bool IsStrictUtf8(std::string_view text) noexcept {
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7F) {
            ++index;
            continue;
        }
        if (first >= 0xC2 && first <= 0xDF) {
            if (index + 1 >= text.size() ||
                !IsUtf8Continuation(static_cast<unsigned char>(text[index + 1]))) {
                return false;
            }
            index += 2;
            continue;
        }
        if (first == 0xE0) {
            if (index + 2 >= text.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(text[index + 1]);
            if (second < 0xA0 || second > 0xBF ||
                !IsUtf8Continuation(static_cast<unsigned char>(text[index + 2]))) {
                return false;
            }
            index += 3;
            continue;
        }
        if (first >= 0xE1 && first <= 0xEC || first >= 0xEE && first <= 0xEF) {
            if (index + 2 >= text.size() ||
                !IsUtf8Continuation(static_cast<unsigned char>(text[index + 1])) ||
                !IsUtf8Continuation(static_cast<unsigned char>(text[index + 2]))) {
                return false;
            }
            index += 3;
            continue;
        }
        if (first == 0xED) {
            if (index + 2 >= text.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(text[index + 1]);
            if (second < 0x80 || second > 0x9F ||
                !IsUtf8Continuation(static_cast<unsigned char>(text[index + 2]))) {
                return false;
            }
            index += 3;
            continue;
        }
        if (first == 0xF0) {
            if (index + 3 >= text.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(text[index + 1]);
            if (second < 0x90 || second > 0xBF ||
                !IsUtf8Continuation(static_cast<unsigned char>(text[index + 2])) ||
                !IsUtf8Continuation(static_cast<unsigned char>(text[index + 3]))) {
                return false;
            }
            index += 4;
            continue;
        }
        if (first >= 0xF1 && first <= 0xF3) {
            if (index + 3 >= text.size() ||
                !IsUtf8Continuation(static_cast<unsigned char>(text[index + 1])) ||
                !IsUtf8Continuation(static_cast<unsigned char>(text[index + 2])) ||
                !IsUtf8Continuation(static_cast<unsigned char>(text[index + 3]))) {
                return false;
            }
            index += 4;
            continue;
        }
        if (first == 0xF4) {
            if (index + 3 >= text.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(text[index + 1]);
            if (second < 0x80 || second > 0x8F ||
                !IsUtf8Continuation(static_cast<unsigned char>(text[index + 2])) ||
                !IsUtf8Continuation(static_cast<unsigned char>(text[index + 3]))) {
                return false;
            }
            index += 4;
            continue;
        }
        return false;
    }
    return true;
}

[[nodiscard]] bool DecodeStringCandidate(
    std::uint64_t encodedType,
    const void* encodedPointer,
    std::string_view& decodedString) noexcept {
    if ((static_cast<unsigned char>(encodedType) & layout::kTypeMask) != 6 || encodedPointer == nullptr) {
        return false;
    }

    const char* text = static_cast<const char*>(encodedPointer);
    if ((static_cast<unsigned char>(encodedType) & layout::kOwnedValueFlag) != 0) {
        const auto pointerBits = reinterpret_cast<std::uintptr_t>(encodedPointer);
        if ((pointerBits & 7) != 0 || !IsReadableRange(encodedPointer, sizeof(const char*))) {
            return false;
        }
        std::memcpy(&text, encodedPointer, sizeof(text));
    }

    if (text == nullptr) {
        return false;
    }

    const char* current = text;
    const char* readableEnd = nullptr;
    for (std::size_t index = 0; index < layout::kMaxScaleformStringLength; ++index) {
        if (current == readableEnd &&
            !GetReadableSpanEnd(current, layout::kMaxScaleformStringLength - index, readableEnd)) {
            return false;
        }
        const unsigned char value = static_cast<unsigned char>(*current);
        if (value == 0) {

            const std::string_view candidate{text, index};
            if (!IsStrictUtf8(candidate)) {
                return false;
            }
            decodedString = candidate;
            return true;
        }
        if (!IsScaleformStringByteAllowed(value)) {
            return false;
        }
        ++current;
    }
    return false;
}

}

ScaleformValueInfo InspectScaleformValue(const ScaleformValue& value) noexcept {
    return {
        ReadValue<std::uint8_t>(value, layout::kValueTypeOffset),
        ReadValue<void*>(value, layout::kValueInternalOneOffset),
        ReadValue<void*>(value, layout::kValueInternalTwoOffset),
    };
}

bool IsObjectLikeScaleformValue(const ScaleformValue& value) noexcept {
    const auto info = InspectScaleformValue(value);
    const auto baseType = static_cast<std::uint8_t>(info.rawType & layout::kTypeMask);
    return baseType >= 8 && baseType <= 10 &&
        info.objectInterface != nullptr && info.data != nullptr;
}

NativeFunctionHandler::NativeFunctionHandler(CallbackRegistry& callbackRegistry) noexcept
    : vtable_{nullptr},
      referenceCount_{1},
      referenceCountPadding_{0} {
    static_assert(offsetof(NativeFunctionHandler, vtable_) == 0x00);
    static_assert(offsetof(NativeFunctionHandler, referenceCount_) == 0x08);
    static_assert(offsetof(NativeFunctionHandler, referenceCountPadding_) == 0x0C);
    static_assert(sizeof(NativeFunctionHandler) == 0x10);
    static const HandlerVtableEntry kVtable[] = {
        reinterpret_cast<HandlerVtableEntry>(&NativeFunctionHandler::Identity),
        reinterpret_cast<HandlerVtableEntry>(&NativeFunctionHandler::Invoke),
    };
    vtable_ = kVtable;
    callbackRegistry_ = &callbackRegistry;
}

CallbackRegistry* NativeFunctionHandler::callbackRegistry_ = nullptr;

void* __fastcall NativeFunctionHandler::Identity(void* handler) noexcept {
    return handler;
}

void __fastcall NativeFunctionHandler::Invoke(
    void* handler,
    const FunctionParams* params) noexcept {
    if (handler == nullptr || callbackRegistry_ == nullptr ||
        params == nullptr || params->arguments == nullptr || params->argumentCount == 0) {
        return;
    }

    std::string_view callbackName;
    if (!TryDecodeScaleformString(params->arguments, callbackName)) {
        return;
    }

    ScaleformResult callbackResult;
    const ScaleformCall call{
        params->arguments,
        params->argumentCount,
        &callbackResult,
    };
    if (!callbackRegistry_->Dispatch(callbackName, call)) {
        return;
    }
    if (callbackResult.kind == ScaleformResultKind::Boolean) {
        (void)WriteBooleanResult(params->result, callbackResult.booleanValue);
    } else if (callbackResult.kind == ScaleformResultKind::String) {
        (void)WriteStringResult(
            params->result,
            callbackResult.stringValue.data(),
            callbackResult.stringValue.size());
    }
}

bool TryDecodeScaleformString(
    const std::byte* argumentRecord,
    std::string_view& decodedString) noexcept {
    decodedString = {};
    if (argumentRecord == nullptr) {
        return false;
    }

    // 0x18015086F passes +0x18/+0x20 as the preferred pair to 0x180145E30;
    // the helper retries +0x08/+0x10 when the preferred pair is not a string.
    const auto primaryType = ReadArgumentValue<std::uint64_t>(argumentRecord, layout::kPrimaryArgumentTypeOffset);
    const auto primaryPointer = ReadArgumentValue<const void*>(argumentRecord, layout::kPrimaryArgumentPointerOffset);
    if (DecodeStringCandidate(primaryType, primaryPointer, decodedString)) {
        return true;
    }

    const auto fallbackType = ReadArgumentValue<std::uint64_t>(argumentRecord, layout::kFallbackArgumentTypeOffset);
    const auto fallbackPointer = ReadArgumentValue<const void*>(argumentRecord, layout::kFallbackArgumentPointerOffset);
    return DecodeStringCandidate(fallbackType, fallbackPointer, decodedString);
}




bool TryGetScaleformStringArgument(
    const ScaleformCall& call,
    std::size_t argumentIndex,
    std::string_view& decodedString) noexcept {
    decodedString = {};
    if (call.arguments == nullptr || argumentIndex >= call.argumentCount ||
        argumentIndex > (std::numeric_limits<std::size_t>::max)() / layout::kFunctionArgumentSize) {
        return false;
    }
    const auto* arguments = static_cast<const std::byte*>(call.arguments);
    return TryDecodeScaleformString(
        arguments + argumentIndex * layout::kFunctionArgumentSize,
        decodedString);
}}
