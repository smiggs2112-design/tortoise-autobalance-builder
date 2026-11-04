#ifndef AZEROTH_VOICES_NATIVE_V4_H
#define AZEROTH_VOICES_NATIVE_V4_H
#include "AzerothVoicesSocialV4.h"
class Player;
namespace AzerothVoices { namespace IntelligenceV4 {
struct NativeSources { std::vector<int32_t> drops; std::vector<uint32_t> vendors, loot; bool available=false; };
struct ReaderLimits { uint32_t maximumItems=20; bool equipment=true, backpack=true, enabled=true; };
struct NativeBridge
{
    NativeSources (*sources)(uint32_t,int32_t)=nullptr;
    SocialSeed (*profile)(Player*,Player*)=nullptr;
    void (*delivered)(Player*,Player*,int)=nullptr;
    ReaderLimits (*limits)()=nullptr;
};
inline NativeBridge& Native() { static NativeBridge bridge; return bridge; }
inline ReaderLimits Limits() { return Native().limits?Native().limits():ReaderLimits{}; }
void InstallNativeBridge();
}}
#endif
