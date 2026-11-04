#ifndef AZEROTH_VOICES_INTELLIGENCE_V4_H
#define AZEROTH_VOICES_INTELLIGENCE_V4_H

// All data comes from the *running* Turtle WoW core and its loaded databases.
// No Internet, external database or LLM is required. Does not move/cast/gear bots.
#include "Player.h"
#include "Group.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "SpellMgr.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace AzerothVoices { namespace IntelligenceV4 {

inline std::string Normalise(std::string const& source)
{
    std::string out;
    bool space = true;
    for (unsigned char ch : source)
    {
        if (std::isalnum(ch))
        {
            out.push_back(static_cast<char>(std::tolower(ch)));
            space = false;
        }
        else if (!space) { out.push_back(' '); space = true; }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

inline bool Contains(std::string const& text, std::string const& phrase)
{
    return !phrase.empty() &&
        (" " + text + " ").find(" " + phrase + " ") != std::string::npos;
}
inline bool Any(std::string const& text, std::initializer_list<char const*> p)
{
    for (auto* word : p) if (Contains(text, word)) return true;
    return false;
}

inline uint8_t RoleBits(Player* bot)
{
    return bot ? sScriptMgr.GetBotRoles(bot) : 0;
}
inline bool IsHealer(Player* bot) { return (RoleBits(bot) & 0x02) != 0; }
inline bool IsTank(Player* bot) { return (RoleBits(bot) & 0x01) != 0; }
inline bool IsDps(Player* bot) { return (RoleBits(bot) & 0x04) != 0; }
inline std::string RoleName(Player* bot)
{
    if (IsTank(bot)) return "tank";
    if (IsHealer(bot)) return "healer";
    if (IsDps(bot)) return "damage";
    return "unassigned";
}

inline std::string Limit(std::string s, size_t n = 220)
{
    if (s.size() > n) s.resize(n);
    return s;
}

inline bool HasInLog(Player const* p, uint32_t id)
{
    if (!p || !id) return false;
    for (uint8_t i = 0; i < MAX_QUEST_LOG_SIZE; ++i)
        if (GetPlayerQuestSlotQuestId(p, i) == id) return true;
    return false;
}

inline std::string CreatureLabel(int32_t entry)
{
    if (entry > 0)
    {
        CreatureInfo const* creature = sObjectMgr.GetCreatureTemplate(static_cast<uint32_t>(entry));
        if (creature && !creature->name.empty()) return creature->name;
    }
    if (entry < 0) return "a quest object";
    return "an objective";
}
inline std::string ItemLabel(uint32_t entry)
{
    ItemPrototype const* item = sObjectMgr.GetItemPrototype(entry);
    if (item && !item->Name1.empty()) return item->Name1;
    return "quest item";
}

// The plan is based on the *human's* quest log, never just the bot's.
// First priority: turn in a completed quest. Then shared quests with
// unresolved objectives, especially overlapping enemy and item targets.
inline bool QuestPlan(Player* human, Player* companion, std::string& answer,
                      std::string const& specificName = "")
{
    if (!human || !companion || !human->GetGroup() ||
        human->GetGroup() != companion->GetGroup())
        return false;

    struct Candidate { Quest const* quest=nullptr; int score=-10000; bool shared=false; };
    Candidate best;
    uint8_t const level = human->GetLevel();
    for (uint8_t slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32_t id = GetPlayerQuestSlotQuestId(human, slot);
        Quest const* quest = id ? sObjectMgr.GetQuestTemplate(id) : nullptr;
        if (!quest) continue;
        if (!specificName.empty() && !Contains(Normalise(quest->GetTitle()), specificName) &&
            !Contains(specificName, Normalise(quest->GetTitle())))
            continue;
        QuestStatus const status = human->GetQuestStatus(id);
        if (status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE)
            continue;

        int score = status == QUEST_STATUS_COMPLETE ? 150 : 0;
        bool const shared = HasInLog(companion, id);
        if (shared && companion->GetQuestStatus(id) != QUEST_STATUS_FAILED) score += 65;
        int32 const ql = static_cast<int32>(quest->GetQuestLevel());
        if (ql > 0)
        {
            int const delta = std::abs(static_cast<int>(level) - ql);
            score += std::max(0, 35 - delta * 5);
            if (ql > static_cast<int>(level) + 5) score -= 45;
        }
        // ZoneOrSort > 0 is a zone ID; negative values are quest category.
        if (quest->GetZoneOrSort() > 0 &&
            static_cast<uint32_t>(quest->GetZoneOrSort()) == human->GetZoneId())
            score += 40;

        QuestStatusData const* data = human->GetQuestStatusData(id);
        if (data && status == QUEST_STATUS_INCOMPLETE)
        {
            uint32_t incomplete = 0;
            uint32_t incompleteShared = 0;
            for (uint32_t o = 0; o < QUEST_OBJECTIVES_COUNT; ++o)
            {
                if (quest->ReqCreatureOrGOCount[o] &&
                    data->m_creatureOrGOcount[o] < quest->ReqCreatureOrGOCount[o])
                {
                    ++incomplete;
                    if (shared) ++incompleteShared;
                }
            }
            for (uint32_t o = 0; o < QUEST_ITEM_OBJECTIVES_COUNT; ++o)
                if (quest->ReqItemCount[o] && data->m_itemcount[o] < quest->ReqItemCount[o])
                    ++incomplete;
            score += std::min(15, static_cast<int>(incomplete) * 3);
            score += std::min(10, static_cast<int>(incompleteShared) * 2);
        }
        if (score > best.score) best = {quest, score, shared};
    }

    if (!best.quest)
    {
        answer = specificName.empty() ?
            "I can't find a suitable active quest in your log. We could check nearby quest givers." :
            "I can't find that quest in your active log.";
        return true;
    }
    Quest const* q = best.quest;
    uint32_t id = q->GetQuestId();
    if (human->GetQuestStatus(id) == QUEST_STATUS_COMPLETE)
    {
        answer = "You've finished " + q->GetTitle() +
            ". I'd hand it in before taking another task.";
        return true;
    }

    std::ostringstream out;
    out << (best.shared ? "We're both working on " : "You have ") << q->GetTitle() << ". ";
    QuestStatusData const* progress = human->GetQuestStatusData(id);
    bool foundObjective = false;
    if (progress)
    {
        for (uint32_t i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            uint32_t goal = q->ReqCreatureOrGOCount[i];
            if (!goal || progress->m_creatureOrGOcount[i] >= goal) continue;
            uint32_t remain = goal - progress->m_creatureOrGOcount[i];
            out << "You still need " << remain << " " << CreatureLabel(q->ReqCreatureOrGOId[i]) << ". ";
            foundObjective = true;
            break;
        }
        if (!foundObjective)
            for (uint32_t i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
            {
                uint32_t goal = q->ReqItemCount[i];
                if (!goal || progress->m_itemcount[i] >= goal) continue;
                out << "You need " << goal - progress->m_itemcount[i] << " more "
                    << ItemLabel(q->ReqItemId[i]) << ". ";
                foundObjective = true;
                break;
            }
    }
    if (!foundObjective)
        out << "Your objectives may be ready; check the quest log. ";
    if (q->GetZoneOrSort() > 0 &&
        static_cast<uint32_t>(q->GetZoneOrSort()) != human->GetZoneId())
        out << "It's listed for another zone; I can't verify the route from here.";
    answer = Limit(out.str(), 240);
    return true;
}

inline bool QuestByName(Player* human, std::string const& q, std::string& answer)
{
    // Resolves explicit active quest titles and reports exact live progress.
    if (!human) return false;
    for (uint8_t slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32_t id = GetPlayerQuestSlotQuestId(human, slot);
        Quest const* quest = id ? sObjectMgr.GetQuestTemplate(id) : nullptr;
        if (!quest || !Contains(q, Normalise(quest->GetTitle()))) continue;
        std::ostringstream out;
        out << quest->GetTitle() << ": ";
        QuestStatus const status = human->GetQuestStatus(id);
        if (status == QUEST_STATUS_COMPLETE)
            out << "ready to hand in";
        else
        {
            QuestStatusData const* p = human->GetQuestStatusData(id);
            if (!p) return false;
            uint32_t count = 0;
            for (uint32_t o = 0; o < QUEST_OBJECTIVES_COUNT; ++o)
            {
                if (!quest->ReqCreatureOrGOCount[o]) continue;
                if (count++) out << "; ";
                out << CreatureLabel(quest->ReqCreatureOrGOId[o]) << " "
                    << p->m_creatureOrGOcount[o] << "/" << quest->ReqCreatureOrGOCount[o];
            }
            for (uint32_t o = 0; o < QUEST_ITEM_OBJECTIVES_COUNT; ++o)
            {
                if (!quest->ReqItemCount[o]) continue;
                if (count++) out << "; ";
                out << ItemLabel(quest->ReqItemId[o]) << " "
                    << p->m_itemcount[o] << "/" << quest->ReqItemCount[o];
            }
            if (!count) out << "no tracked kill or item counters";
        }
        answer = Limit(out.str(), 240);
        return true;
    }
    return false;
}

struct Entry
{
    uint32_t id = 0;
    std::string name;
    std::string key;
};
struct Index
{
    bool built = false;
    std::vector<Entry> quests, creatures, items;
    uint64_t lookups = 0, hits = 0, misses = 0;

    void Build()
    {
        if (built) return;
        built = true;
        // One lazy pass, only upon a relevant player knowledge question.
        // There is no world-tick or bot-population-wide indexing.
        quests.reserve(sObjectMgr.GetQuestTemplates().size());
        for (auto const& entry : sObjectMgr.GetQuestTemplates())
            if (entry.second && !entry.second->GetTitle().empty())
                quests.push_back({entry.first, entry.second->GetTitle(),
                                 Normalise(entry.second->GetTitle())});
        creatures.reserve(sObjectMgr.GetCreatureInfoMap().size());
        for (auto const& entry : sObjectMgr.GetCreatureInfoMap())
            if (entry.second && !entry.second->name.empty())
                creatures.push_back({entry.first, entry.second->name,
                                    Normalise(entry.second->name)});
        items.reserve(sObjectMgr.GetItemPrototypeMap().size());
        for (auto const& entry : sObjectMgr.GetItemPrototypeMap())
            if (!entry.second.Name1.empty())
                items.push_back({entry.first, entry.second.Name1,
                                 Normalise(entry.second.Name1)});
    }

    static Entry const* Match(std::vector<Entry> const& entries, std::string const& question)
    {
        Entry const* found = nullptr;
        size_t length = 0;
        bool ambiguous = false;
        for (auto const& entry : entries)
        {
            // Single-word common names (e.g. "water") are ambiguous and
            // deliberately not used as automatic knowledge matches.
            if (entry.key.size() < 6 || entry.key.find(' ') == std::string::npos)
                continue;
            if (!Contains(question, entry.key)) continue;
            if (entry.key.size() > length)
            {
                found = &entry;
                length = entry.key.size();
                ambiguous = false;
            }
            else if (entry.key.size() == length && found && found->id != entry.id)
                ambiguous = true;
        }
        return ambiguous ? nullptr : found;
    }

    bool Search(std::string const& input, std::string& answer)
    {
        std::string question = Normalise(input);
        ++lookups;
        if (!Any(question, {"quest", "item", "who is", "who are", "what is",
                            "what does", "tell me about", "creature", "mob",
                            "what level", "where is", "where are", "drop"}))
        { ++misses; return false; }
        Build();

        bool questRequest = Any(question, {"quest", "mission", "objective", "complete"});
        bool itemRequest = Any(question, {"item", "weapon", "gear", "equipment", "drop"});
        bool creatureRequest = Any(question, {"creature", "mob", "enemy", "who is",
                                            "where is", "where are", "what level"});
        Entry const* match = nullptr;

        // Explicit type requests take priority; generic "tell me about"
        // resolves exact long-name matches, not arbitrary short tokens.
        if (questRequest && (match = Match(quests, question)))
        {
            Quest const* quest = sObjectMgr.GetQuestTemplate(match->id);
            if (!quest) return false;
            std::ostringstream out;
            out << quest->GetTitle() << " is a level " << quest->GetQuestLevel()
                << " quest (minimum level " << quest->GetMinLevel() << "). ";
            uint32_t objectives = 0;
            for (uint32_t i=0;i<QUEST_OBJECTIVES_COUNT && objectives<2;++i)
                if (quest->ReqCreatureOrGOCount[i])
                {
                    out << "Requires " << quest->ReqCreatureOrGOCount[i] << " "
                        << CreatureLabel(quest->ReqCreatureOrGOId[i]) << ". ";
                    ++objectives;
                }
            for (uint32_t i=0;i<QUEST_ITEM_OBJECTIVES_COUNT && objectives<2;++i)
                if (quest->ReqItemCount[i])
                {
                    out << "Collect " << quest->ReqItemCount[i] << " "
                        << ItemLabel(quest->ReqItemId[i]) << ". ";
                    ++objectives;
                }
            answer = Limit(out.str()); ++hits; return true;
        }

        if (itemRequest && (match = Match(items, question)))
        {
            ItemPrototype const* item = sObjectMgr.GetItemPrototype(match->id);
            if (!item) return false;
            std::ostringstream out;
            out << item->Name1 << " is an item (item level " << item->ItemLevel
                << ", required level " << item->RequiredLevel << ").";
            answer = Limit(out.str()); ++hits; return true;
        }
        if (creatureRequest && (match = Match(creatures, question)))
        {
            CreatureInfo const* creature = sObjectMgr.GetCreatureTemplate(match->id);
            if (!creature) return false;
            std::ostringstream out;
            out << creature->name << " is level " << creature->level_min;
            if (creature->level_max != creature->level_min)
                out << "-" << creature->level_max;
            out << ".";
            if (Any(question, {"where", "location", "find", "spawn"}))
                out << " I can't confirm its location from the template alone.";
            answer = Limit(out.str()); ++hits; return true;
        }

        if (!questRequest && (match = Match(quests, question)))
        {
            Quest const* quest = sObjectMgr.GetQuestTemplate(match->id);
            if (!quest) return false;
            answer = Limit(quest->GetTitle() + " is a level " +
                           std::to_string(quest->GetQuestLevel()) + " quest.");
            ++hits; return true;
        }
        ++misses; return false;
    }
};

inline Index& Knowledge()
{
    static Index index;
    return index;
}

// Checks spellbook and current *self* readiness. No cast or target claim.
// TortoiseBots remains authoritative for actual spells/actions.
inline bool SpellReadiness(Player* bot, std::string const& input, std::string& answer)
{
    if (!bot) return false;
    std::string const q = Normalise(input);
    if (!Any(q, {"can you cast", "do you know", "is your", "cooldown",
                 "can you use", "could you cast", "ready to cast", "spell ready"}))
        return false;
    SpellEntry const* picked = nullptr;
    uint32_t pickedId = 0;
    uint8_t pickedRank = 0;
    std::string pickedName;
    bool tied = false;
    for (auto const& learned : bot->GetSpellMap())
    {
        if (learned.second.state == PLAYERSPELL_REMOVED || learned.second.disabled)
            continue;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(learned.first);
        if (!spell || spell->IsPassiveSpell() || spell->SpellName[0].empty())
            continue;
        std::string name = Normalise(spell->SpellName[0]);
        if (name.size() < 5 || !Contains(q, name)) continue;
        uint8_t rank = sSpellMgr.GetSpellRank(learned.first);
        if (!picked || name.size() > pickedName.size() ||
            (name == pickedName && rank > pickedRank))
        {
            picked = spell;
            pickedId = learned.first;
            pickedRank = rank;
            pickedName = name;
            tied = false;
        }
        else if (name.size() == pickedName.size() && name != pickedName)
            tied = true;
    }
    if (!picked || tied) return false;
    std::ostringstream out;
    out << "I know " << picked->SpellName[0] << ". ";
    bool const cooldown = bot->HasSpellCooldown(pickedId) ||
                          bot->HasSpellCategoryCooldown(picked->Category);
    if (cooldown) out << "It's on cooldown. ";
    bool const enoughPower = bot->GetPower(static_cast<Powers>(picked->powerType)) >= picked->manaCost;
    if (!enoughPower) out << "I don't have enough resource for its base cost. ";
    bool reagents = true;
    for (uint32_t j=0; j<MAX_SPELL_REAGENTS; ++j)
    {
        if (picked->Reagent[j] > 0 && picked->ReagentCount[j] > 0 &&
            bot->GetItemCount(static_cast<uint32_t>(picked->Reagent[j])) < picked->ReagentCount[j])
        {
            reagents = false; break;
        }
    }
    if (!reagents) out << "I'm missing a required reagent. ";
    if (!cooldown && enoughPower && reagents)
        out << "My cooldown, basic resource and reagent checks pass. ";
    out << "Target, range and cast restrictions still need checking.";
    answer = Limit(out.str(), 240);
    return true;
}

inline bool HasSharedQuest(Player* a, Player* b, std::string& label)
{
    if (!a || !b) return false;
    for (uint8_t slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32_t id = GetPlayerQuestSlotQuestId(a, slot);
        if (!id || !HasInLog(b, id) ||
            a->GetQuestStatus(id) != QUEST_STATUS_INCOMPLETE ||
            b->GetQuestStatus(id) != QUEST_STATUS_INCOMPLETE)
            continue;
        Quest const* quest = sObjectMgr.GetQuestTemplate(id);
        if (!quest) continue;
        label = quest->GetTitle();
        return true;
    }
    return false;
}

}} // namespaces
#endif
