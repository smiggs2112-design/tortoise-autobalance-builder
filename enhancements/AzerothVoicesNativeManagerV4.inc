// Included once at file scope by AzerothVoicesManager.cpp, after its namespace.
namespace AzerothVoices {
IntelligenceV4::ReaderLimits Manager::SharedReaderLimits() const
{
    IntelligenceV4::ReaderLimits result;
    result.enabled=m_started && m_config && m_config->enabled && !m_paused;
    if(m_config)
    {
        result.maximumItems=std::min<uint32_t>(50,m_config->environmentMaximumItems);
        result.equipment=m_config->environmentIncludeEquipment;
        result.backpack=m_config->environmentIncludeBackpack;
    }
    return result;
}
IntelligenceV4::SocialSeed Manager::SharedSocialSeed(Player* bot,Player* human)
{
    IntelligenceV4::SocialSeed seed;
    if(!m_started || !m_config || !bot || !human || !IsOnlineRealPlayer(human) ||
        !bot->GetGroup() || bot->GetGroup()!=human->GetGroup())return seed;
    if(m_config->personalityEnabled)
    {
        BotPersonality personality;
        if(LoadPersonality(SnapshotBot(bot),personality,false))seed.traits=personality.traits;
    }
    if(m_config->sentimentEnabled)
    {
        SentimentRecord record;
        seed.hasSentiment=LoadSentiment({bot->GetObjectGuid().GetRawValue(),human->GetObjectGuid().GetRawValue()},record);
        seed.sentiment=record.score;
    }
    if(m_config->memoryEnabled)
    {
        MemoryKey key{bot->GetObjectGuid().GetRawValue(),human->GetObjectGuid().GetRawValue()};
        LoadMemories(key);auto found=m_memories.find(key);
        if(found!=m_memories.end())
        {
            auto selected=SelectMemoriesForPrompt(found->second,2,360);
            for(auto* record:selected)seed.memories.push_back(record->summary);
        }
    }
    return seed;
}
void Manager::SharedSocialDelivered(Player* bot,Player* human,int delta)
{
    if(!m_started || !m_config || !bot || !human || !IsOnlineRealPlayer(human) ||
        !bot->GetGroup() || bot->GetGroup()!=human->GetGroup())return;
    uint64_t actor=bot->GetObjectGuid().GetRawValue(), target=human->GetObjectGuid().GetRawValue();
    if(delta && m_config->sentimentEnabled && m_config->sentimentConversationMaximumDelta>0)
    {
        SentimentRecord record;LoadSentiment({actor,target},record);
        uint64_t now=UnixNow();
        record.key={actor,target};record.exists=true;
        if(!record.createdUnix)record.createdUnix=now;
        record.updatedUnix=now;record.lastInteractionUnix=now;
        record.score=ClampSentimentScore(record.score+std::max(-1,std::min(1,delta)));
        CacheSentiment(record);QueueSentimentWrite(record);
    }
    if(m_config->memoryEnabled)
    {
        MemoryKey key{actor,target};LoadMemories(key);auto found=m_memories.find(key);
        if(found==m_memories.end() || found->second.empty())
        {
            MemoryFacts facts;facts.playerName=human->GetName();facts.zone=SnapshotBot(bot).zone;
            RecordMemory(actor,target,MemoryType::FirstMet,facts);
        }
    }
}
}
