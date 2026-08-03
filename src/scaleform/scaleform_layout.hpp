#pragma once

#include <cstddef>
#include <cstdint>

namespace sf::layout {

inline constexpr std::size_t kValueSize = 0x40;
inline constexpr std::size_t kValueInternalOneOffset = 0x10;
inline constexpr std::size_t kValueTypeOffset = 0x18;
inline constexpr std::size_t kValueInternalTwoOffset = 0x20;
inline constexpr std::size_t kFunctionArgumentSize = 0x30;
inline constexpr std::size_t kPrimaryArgumentTypeOffset = 0x18;
inline constexpr std::size_t kPrimaryArgumentPointerOffset = 0x20;
inline constexpr std::size_t kFallbackArgumentTypeOffset = 0x08;
inline constexpr std::size_t kFallbackArgumentPointerOffset = 0x10;
inline constexpr std::size_t kCreateObjectVtableOffset = 0x170;
inline constexpr std::size_t kCreateFunctionVtableOffset = 0x180;
inline constexpr std::size_t kGetVariableVtableOffset = 0x190;
inline constexpr std::uint8_t kTypeMask = 0x8F;
inline constexpr std::uint8_t kOwnedValueFlag = 0x40;
inline constexpr std::uint8_t kBooleanType = 2;
inline constexpr std::uint8_t kStringType = 6;
inline constexpr std::uint8_t kObjectType = 8;
inline constexpr std::uint8_t kDisplayObjectType = 10;
inline constexpr std::size_t kMaxScaleformStringLength = 0x1000000;
inline constexpr std::size_t kReturnStringBufferSize = 0x4000;
inline constexpr std::size_t kMaxReturnStringLength = kReturnStringBufferSize - 1;

}