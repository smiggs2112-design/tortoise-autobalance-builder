#pragma once
// Small in-memory test double. The complete pinned core is compiled by the image job.
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <set>
using uint8 = uint8_t; using uint32 = uint32_t; using int32 = int32_t;
constexpr uint8 MAX_QUEST_LOG_SIZE=25, QUEST_OBJECTIVES_COUNT=4, QUEST_ITEM_OBJECTIVES_COUNT=4;
constexpr uint8 INVENTORY_SLOT_BAG_0=0, EQUIPMENT_SLOT_START=0, EQUIPMENT_SLOT_END=19;
constexpr uint8 EQUIPMENT_SLOT_MAINHAND=15, EQUIPMENT_SLOT_OFFHAND=16;
constexpr uint8 INVENTORY_SLOT_BAG_START=19, INVENTORY_SLOT_BAG_END=23;
constexpr uint8 INVENTORY_SLOT_ITEM_START=23, INVENTORY_SLOT_ITEM_END=39;
constexpr uint32 MAX_SPELL_REAGENTS=8, PLAYERSPELL_REMOVED=2;
constexpr uint32 UNIT_NPC_FLAG_QUESTGIVER=2,UNIT_NPC_FLAG_VENDOR=128,UNIT_NPC_FLAG_TRAINER=16;
enum Powers { POWER_MANA=0, POWER_RAGE=1, POWER_FOCUS=2, POWER_ENERGY=3 };
enum QuestStatus { QUEST_STATUS_NONE, QUEST_STATUS_INCOMPLETE, QUEST_STATUS_COMPLETE, QUEST_STATUS_FAILED };
struct QuestStatusData { uint32 m_creatureOrGOcount[4]{}, m_itemcount[4]{}; };
struct Quest
{
    uint32 id=0, level=10, minimum=1,nextChain=0; int32 zone=1,prev=0,nextQuest=0; std::string title,objectives;
    bool eligible=true;
    uint32 ReqCreatureOrGOCount[4]{}, ReqItemCount[4]{}, ReqItemId[4]{};
    int32 ReqCreatureOrGOId[4]{};
    uint32 GetQuestId() const { return id; } uint32 GetQuestLevel() const { return level; }
    uint32 GetMinLevel() const { return minimum; } int32 GetZoneOrSort() const { return zone; }
    std::string const& GetTitle() const { return title; }
    std::string const& GetObjectives() const { return objectives; }
    int32 GetPrevQuestId() const { return prev; } int32 GetNextQuestId() const { return nextQuest; }
    uint32 GetNextQuestInChain() const { return nextChain; }
};
struct ItemPrototype { std::string Name1; uint32 ItemLevel=1, RequiredLevel=1; };
struct Item { ItemPrototype proto; uint32 count=1; ItemPrototype const* GetProto() const { return &proto; } uint32 GetCount() const { return count; } };
struct Bag : Item { uint32 GetBagSize() const { return 16; } };
struct CreatureInfo { std::string name; uint32 level_min=1, level_max=1,npc_flags=0; };
struct Position { uint32 mapId=1; float x=1,y=2; };
struct CreatureData { Position position; std::array<uint32,4> creature_id{}; bool HasCreatureId(uint32 e) const { return std::find(creature_id.begin(),creature_id.end(),e)!=creature_id.end(); } };
using CreatureDataPair = std::pair<uint32 const, CreatureData>;
struct GameObjectInfo { std::string name;uint32 type=0; };
struct GameObjectData { uint32 id=0;Position position; };
using GameObjectDataPair=std::pair<uint32 const,GameObjectData>;
struct SpellEntry
{
    std::string SpellName[1]; uint32 Category=0, powerType=0, manaCost=0,spellLevel=1,RecoveryTime=0,SpellFamilyName=0;
    int32 Reagent[8]{}; uint32 ReagentCount[8]{};
    bool IsPassiveSpell() const { return false; }
};
struct Learned { uint32 state=0; bool disabled=false; };
struct Guid { uint64_t id=1; uint64_t GetRawValue() const { return id; } };
class Player;
struct GroupReference { Player* player=nullptr; GroupReference* following=nullptr;
    Player* getSource() { return player; } GroupReference* next() { return following; } };
struct Group { GroupReference* first=nullptr; GroupReference* GetFirstMember() { return first; } };
class Player
{
public:
    bool bot=false, online=true, cooldown=false; uint8 roles=0;
    Group* group=nullptr; Guid guid; std::string name="Companion";
    uint32 money=12345, map=1, zone=1, level=10, power=100; float x=0,y=0;
    std::set<uint32> rewarded;
    uint32 log[25]{}; std::map<uint32, QuestStatus> status;
    std::map<uint32, QuestStatusData> progress; std::map<uint32, Learned> spells;
    std::map<std::pair<uint8,uint8>,Item*> inventory;
    Group* GetGroup() const { return group; } bool IsInWorld() const { return online; }
    bool IsAlive() const { return true; } Guid GetObjectGuid() const { return guid; }
    char const* GetName() const { return name.c_str(); } uint32 GetMoney() const { return money; }
    uint8 GetLevel() const { return level; } uint32 GetZoneId() const { return zone; }
    uint32 GetMapId() const { return map; } float GetHealthPercent() const { return 80; }
    float GetPositionX() const { return x; } float GetPositionY() const { return y; }
    bool CanTakeQuest(Quest const* q,bool) const { return q && q->eligible && level>=q->minimum && !rewarded.count(q->id) && GetQuestStatus(q->id)==QUEST_STATUS_NONE; }
    bool CanAddQuest(Quest const*,bool) const { uint32 n=0;for(auto id:log)if(id)++n;return n<25; }
    float GetPowerPercent(Powers) const { return power; } uint32 GetPower(Powers) const { return power; }
    uint32 GetMaxPower(Powers) const { return 100; }
    bool HasSpellCooldown(uint32) const { return cooldown; }
    bool HasSpellCategoryCooldown(uint32) const { return false; }
    bool IsWithinDistInMap(Player const* p, float) const { return p->map==map; }
    uint32 GetItemCount(uint32) const { return 0; }
    std::map<uint32, Learned> const& GetSpellMap() const { return spells; }
    Item* GetItemByPos(uint8 bag, uint8 slot) const {
        auto i=inventory.find({bag,slot}); return i==inventory.end()?nullptr:i->second;
    }
    QuestStatus GetQuestStatus(uint32 id) const {
        auto i=status.find(id); return i==status.end()?QUEST_STATUS_NONE:i->second;
    }
    QuestStatusData const* GetQuestStatusData(uint32 id) const {
        auto i=progress.find(id); return i==progress.end()?nullptr:&i->second;
    }
};
struct ObjectMgr
{
    std::map<uint32,Quest*> quests;
    std::map<uint32,std::unique_ptr<CreatureInfo>> creatures;
    std::map<uint32,ItemPrototype> items;
    std::map<uint32,CreatureData> spawns;
    std::map<uint32,GameObjectInfo> objects;
    std::map<uint32,GameObjectData> objectSpawns;
    std::multimap<uint32,uint32> starts,ends,goStarts,goEnds;
    Quest const* GetQuestTemplate(uint32 id) const { auto i=quests.find(id); return i==quests.end()?nullptr:i->second; }
    CreatureInfo const* GetCreatureTemplate(uint32 id) const { auto i=creatures.find(id); return i==creatures.end()?nullptr:i->second.get(); }
    ItemPrototype const* GetItemPrototype(uint32 id) const { auto i=items.find(id); return i==items.end()?nullptr:&i->second; }
    auto const& GetQuestTemplates() const { return quests; }
    auto const& GetCreatureInfoMap() const { return creatures; }
    auto const& GetItemPrototypeMap() const { return items; }
    auto const& GetGameObjectInfoMap() const { return objects; }
    GameObjectInfo const* GetGameObjectInfo(uint32 id) const { auto i=objects.find(id);return i==objects.end()?nullptr:&i->second; }
    auto GetCreatureQuestRelationsMapBounds(uint32 e) const { return starts.equal_range(e); }
    auto GetCreatureQuestInvolvedRelationsMapBounds(uint32 e) const { return ends.equal_range(e); }
    auto GetGOQuestRelationsMapBounds(uint32 e) const { return goStarts.equal_range(e); }
    auto GetGOQuestInvolvedRelationsMapBounds(uint32 e) const { return goEnds.equal_range(e); }
    template<class F> void DoCreatureData(F& f) const { for (auto const& p:spawns) if(f(p)) break; }
    template<class F> void DoGOData(F& f) const { for (auto const& p:objectSpawns) if(f(p)) break; }
};
struct SpellMgr
{
    std::map<uint32,SpellEntry> entries;
    SpellEntry const* GetSpellEntry(uint32 id) const { auto i=entries.find(id); return i==entries.end()?nullptr:&i->second; }
    uint8 GetSpellRank(uint32 id) const { return id%10; }
    uint32 GetMaxSpellId() const { return entries.empty()?1:entries.rbegin()->first+1; }
};
struct ScriptMgr { uint8 GetBotRoles(Player* p) const { return p->roles; } };
inline ObjectMgr sObjectMgr; inline SpellMgr sSpellMgr; inline ScriptMgr sScriptMgr;
namespace AzerothVoices {
inline bool Script_IsAIControlled(Player const* p) { return p && p->bot; }
inline uint32 GetPlayerQuestSlotQuestId(Player const* p,uint8 slot) { return p->log[slot]; }
}
