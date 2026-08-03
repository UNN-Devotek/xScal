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

thread_local char return_string_buffer[layout::kReturnStringBufferSize]{};


template <typename T>
[[nodiscard]] T ReadValue(const ScaleformValue& value, std::size_t offset) noexcept {
    T result{};
    std::memcpy(&result, value.storage + offset, sizeof(result));
    return result;
}

[[nodiscard]] bool WriteBooleanResult(void* raw_result, bool boolean_value) noexcept {
    if (raw_result == nullptr) {
        return false;
    }

    std::byte value[sizeof(ScaleformValue)]{};
    constexpr std::uint64_t kBooleanType = layout::kBooleanType;
    const std::uint64_t encoded_value = boolean_value ? 1 : 0;
    std::memcpy(value + layout::kValueTypeOffset, &kBooleanType, sizeof(kBooleanType));
    std::memcpy(value + layout::kValueInternalTwoOffset, &encoded_value, sizeof(encoded_value));
    std::memcpy(raw_result, value, sizeof(value));
    return true;
}

[[nodiscard]] bool WriteStringResult(
    void* raw_result,
    const char* text,
    std::size_t text_length) noexcept {
    if (raw_result == nullptr || text == nullptr) {
        return false;
    }

    const std::size_t copied_length =
        text_length < layout::kMaxReturnStringLength ? text_length : layout::kMaxReturnStringLength;
    std::memcpy(return_string_buffer, text, copied_length);
    return_string_buffer[copied_length] = '\0';

    std::byte value[sizeof(ScaleformValue)]{};
    constexpr std::uint64_t kStringType = layout::kStringType;
    const char* const value_text = return_string_buffer;
    std::memcpy(value + layout::kValueTypeOffset, &kStringType, sizeof(kStringType));
    std::memcpy(value + layout::kValueInternalTwoOffset, &value_text, sizeof(value_text));
    std::memcpy(raw_result, value, sizeof(value));
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
    const char*& span_end) noexcept {
    return platform::GetReadableSpanEnd(&::VirtualQuery, current, remaining, span_end);
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
    std::uint64_t encoded_type,
    const void* encoded_pointer,
    std::string_view& decoded_string) noexcept {
    if ((static_cast<unsigned char>(encoded_type) & layout::kTypeMask) != 6 || encoded_pointer == nullptr) {
        return false;
    }

    const char* text = static_cast<const char*>(encoded_pointer);
    if ((static_cast<unsigned char>(encoded_type) & layout::kOwnedValueFlag) != 0) {
        const auto pointer_bits = reinterpret_cast<std::uintptr_t>(encoded_pointer);
        if ((pointer_bits & 7) != 0 || !IsReadableRange(encoded_pointer, sizeof(const char*))) {
            return false;
        }
        std::memcpy(&text, encoded_pointer, sizeof(text));
    }

    if (text == nullptr) {
        return false;
    }

    const char* current = text;
    const char* readable_end = nullptr;
    for (std::size_t index = 0; index < layout::kMaxScaleformStringLength; ++index) {
        if (current == readable_end &&
            !GetReadableSpanEnd(current, layout::kMaxScaleformStringLength - index, readable_end)) {
            return false;
        }
        const unsigned char value = static_cast<unsigned char>(*current);
        if (value == 0) {

            const std::string_view candidate{text, index};
            if (!IsStrictUtf8(candidate)) {
                return false;
            }
            decoded_string = candidate;
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
    const auto base_type = static_cast<std::uint8_t>(info.raw_type & layout::kTypeMask);
    return base_type >= 8 && base_type <= 10 &&
        info.object_interface != nullptr && info.data != nullptr;
}

NativeFunctionHandler::NativeFunctionHandler(CallbackRegistry& callback_registry) noexcept
    : vtable_{nullptr},
      reference_count_{1},
      reference_count_padding_{0} {
    static_assert(offsetof(NativeFunctionHandler, vtable_) == 0x00);
    static_assert(offsetof(NativeFunctionHandler, reference_count_) == 0x08);
    static_assert(offsetof(NativeFunctionHandler, reference_count_padding_) == 0x0C);
    static_assert(sizeof(NativeFunctionHandler) == 0x10);
    static const HandlerVtableEntry kVtable[] = {
        reinterpret_cast<HandlerVtableEntry>(&NativeFunctionHandler::Identity),
        reinterpret_cast<HandlerVtableEntry>(&NativeFunctionHandler::Invoke),
    };
    vtable_ = kVtable;
    callback_registry_ = &callback_registry;
}

CallbackRegistry* NativeFunctionHandler::callback_registry_ = nullptr;

void* __fastcall NativeFunctionHandler::Identity(void* handler) noexcept {
    return handler;
}

void __fastcall NativeFunctionHandler::Invoke(
    void* handler,
    const FunctionParams* params) noexcept {
    if (handler == nullptr || callback_registry_ == nullptr ||
        params == nullptr || params->arguments == nullptr || params->argument_count == 0) {
        return;
    }

    std::string_view callback_name;
    if (!TryDecodeScaleformString(params->arguments, callback_name)) {
        return;
    }

    ScaleformResult callback_result;
    const ScaleformCall call{
        params->arguments,
        params->argument_count,
        &callback_result,
    };
    if (!callback_registry_->Dispatch(callback_name, call)) {
        return;
    }
    if (callback_result.kind == ScaleformResultKind::Boolean) {
        (void)WriteBooleanResult(params->result, callback_result.boolean_value);
    } else if (callback_result.kind == ScaleformResultKind::String) {
        (void)WriteStringResult(
            params->result,
            callback_result.string_value.data(),
            callback_result.string_value.size());
    }
}

bool TryDecodeScaleformString(
    const std::byte* argument_record,
    std::string_view& decoded_string) noexcept {
    decoded_string = {};
    if (argument_record == nullptr) {
        return false;
    }

    // 0x18015086F passes +0x18/+0x20 as the preferred pair to 0x180145E30;
    // the helper retries +0x08/+0x10 when the preferred pair is not a string.
    const auto primary_type = ReadArgumentValue<std::uint64_t>(argument_record, layout::kPrimaryArgumentTypeOffset);
    const auto primary_pointer = ReadArgumentValue<const void*>(argument_record, layout::kPrimaryArgumentPointerOffset);
    if (DecodeStringCandidate(primary_type, primary_pointer, decoded_string)) {
        return true;
    }

    const auto fallback_type = ReadArgumentValue<std::uint64_t>(argument_record, layout::kFallbackArgumentTypeOffset);
    const auto fallback_pointer = ReadArgumentValue<const void*>(argument_record, layout::kFallbackArgumentPointerOffset);
    return DecodeStringCandidate(fallback_type, fallback_pointer, decoded_string);
}




bool TryGetScaleformStringArgument(
    const ScaleformCall& call,
    std::size_t argument_index,
    std::string_view& decoded_string) noexcept {
    decoded_string = {};
    if (call.arguments == nullptr || argument_index >= call.argument_count ||
        argument_index > (std::numeric_limits<std::size_t>::max)() / layout::kFunctionArgumentSize) {
        return false;
    }
    const auto* arguments = static_cast<const std::byte*>(call.arguments);
    return TryDecodeScaleformString(
        arguments + argument_index * layout::kFunctionArgumentSize,
        decoded_string);
}}
