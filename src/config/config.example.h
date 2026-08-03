#pragma once

namespace sf::config {

// Replace these inert placeholders with values verified for one exact executable build.
// Every address below is an RVA relative to the main executable's image base:
//     IDA is our friend. 
//

inline constexpr ExecutableConfig kSteam{
    {1, 7, 25, 39},  // exact executable file version
    0x1000,         // primary GetVariable() slot. 
    0x2000,         // GetMember: entry point of the executable Scaleform helper
    0x3000,         // SetMember() 
    0x4000,         // RVL entry point of the executable Scaleform helper that releases an onwed ScaleformValue
};

inline constexpr ExecutableConfig kGamePass{
    {1, 7, 25, 39},  // exact executable file version
    0x5000,         // primary GetVariable() slot. 
    0x6000,         // GetMember: entry point of the executable Scaleform helper
    0x7000,         // SetMember()
    0x8000,         // RVL entry point of the executable Scaleform helper that releases an onwed ScaleformValue
};

}
