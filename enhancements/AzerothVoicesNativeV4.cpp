#include "AzerothVoicesIntelligenceV4.h"
#include "AzerothVoicesManager.h"
// Reuse TortoiseBots' existing shared indexes. No source-layout casts or
// independent loot SQL are introduced by Voices.
#include "playerbot/strategy/values/SharedValueContext.h"

namespace AzerothVoices { namespace IntelligenceV4 {
namespace {
NativeSources ReadNativeSources(uint32_t item,int32_t entry)
{
    NativeSources result;
    if(item)
    {
        auto* value=MaNGOS::Singleton<ai::SharedObjectContext>::Instance().GetValue<ai::DropMap*>("drop map");
        auto* drops=value?value->Get():nullptr;
        if(drops)
        {
            result.available=true;
            auto range=drops->equal_range(item);uint32_t visited=0;
            std::set<int32_t> unique;
            for(auto i=range.first;i!=range.second && visited++<256;++i)
                if(i->second && unique.insert(i->second).second)
                {result.drops.push_back(i->second);if(result.drops.size()==32)break;}
        }
        auto* vendorValue=MaNGOS::Singleton<ai::SharedObjectContext>::Instance().GetValue<ai::VendorMap*>("vendor map");
        auto* vendors=vendorValue?vendorValue->Get():nullptr;
        if(vendors)
        {
            result.available=true;
            auto range=vendors->equal_range(item);uint32_t visited=0;std::set<uint32_t> unique;
            for(auto i=range.first;i!=range.second && visited++<256;++i)
                if(i->second>0 && unique.insert(static_cast<uint32_t>(i->second)).second)
                {result.vendors.push_back(i->second);if(result.vendors.size()==16)break;}
        }
    }
    if(entry)
    {
        auto* value=MaNGOS::Singleton<ai::SharedObjectContext>::Instance().GetValue<std::list<uint32>>("entry loot list",entry);
        if(value)
        {
            result.available=true;auto const& items=value->GetRef();
            for(auto id:items) {result.loot.push_back(id);if(result.loot.size()==16)break;}
        }
    }
    return result;
}
}
void InstallNativeBridge()
{
    Knowledge()=Index{};
    RecentIntents().clear();
    Social()=SocialLedger{};
    Native().sources=&ReadNativeSources;
    Native().profile=[](Player* bot,Player* human){return Manager::Instance().SharedSocialSeed(bot,human);};
    Native().delivered=[](Player* bot,Player* human,int delta){Manager::Instance().SharedSocialDelivered(bot,human,delta);};
    Native().limits=[](){return Manager::Instance().SharedReaderLimits();};
}
}}
