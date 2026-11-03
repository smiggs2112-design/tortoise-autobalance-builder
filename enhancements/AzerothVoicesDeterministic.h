#ifndef AZEROTH_VOICES_DETERMINISTIC_H
#define AZEROTH_VOICES_DETERMINISTIC_H

// Local, bounded deterministic conversation. Never invokes Ollama or TortoiseBots
// command handlers. This runs only on the world thread via the scripts module.
#include "AzerothVoicesBotSeam.h"
#include "Group.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "SpellMgr.h"
#include "Map.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace AzerothVoices
{
namespace Deterministic
{
    using Clock = std::chrono::steady_clock;
    using Moment = Clock::time_point;

    inline std::string Normalise(std::string text)
    {
        std::string out;
        out.reserve(text.size());
        bool space = true;
        for (unsigned char c : text)
        {
            if (std::isalnum(c))
            {
                out.push_back(static_cast<char>(std::tolower(c)));
                space = false;
            }
            else if (!space)
            {
                out.push_back(' ');
                space = true;
            }
        }
        while (!out.empty() && out.back() == ' ')
            out.pop_back();
        return out;
    }

    inline bool Has(std::string const& line, std::string const& term)
    {
        // Whole-word/phrase only. "gold" will not match "goldshire".
        return (" " + line + " ").find(" " + term + " ") != std::string::npos;
    }

    inline bool Any(std::string const& line, std::initializer_list<char const*> words)
    {
        for (char const* word : words)
            if (Has(line, word))
                return true;
        return false;
    }

    inline bool IsQuestion(std::string const& normalised, std::string const& original)
    {
        return original.find('?') != std::string::npos ||
            Any(normalised, {"what", "where", "who", "how", "when", "which", "why",
                             "can", "could", "do", "does", "got", "have", "are",
                             "is", "tell me", "show me"});
    }

    inline bool IsPlayableBot(Player* bot)
    {
        return bot && bot->IsInWorld() && Script_IsAIControlled(bot) && bot->IsAlive();
    }

    inline bool Near(Player* a, Player* b)
    {
        return a && b && a->IsInWorld() && b->IsInWorld() &&
            a->IsWithinDistInMap(b, 25.0f);
    }

    enum class Topic
    {
        None, Money, Weapon, Gear, Bags, Quests, Progress, Level,
        Health, Mana, Spells, Resurrection, NextQuest, Knowledge
    };

    struct FollowUp
    {
        Topic topic = Topic::None;
        Moment last = Moment();
    };

    struct Engine
    {
        std::map<std::pair<uint64_t, uint64_t>, FollowUp> followUps;
        std::map<uint64_t, Moment> lastReplies;
        std::map<uint64_t, Moment> lastSocialByPlayer;
        std::map<std::string, Moment> lastSocialByMap;
        Moment nextScan = Moment();
        uint32_t round = 0;
        struct Pending
        {
            uint64_t first = 0;
            uint64_t second = 0;
            uint64_t witness = 0;
            uint32_t map = 0;
            Moment due = Moment();
            std::string reply;
        };
        std::vector<Pending> pending;

        struct Knowledge
        {
            std::string id;
            std::vector<std::string> phrases;
            std::string answer;
            std::string source;
        };
        std::vector<Knowledge> knowledge;
        bool loaded = false;

        void LoadKnowledge()
        {
            if (loaded)
                return;
            loaded = true;
            std::ifstream input("/opt/tortoise/data/humanlike-knowledge.tsv");
            std::string line;
            while (std::getline(input, line) && knowledge.size() < 3000)
            {
                if (line.empty() || line[0] == '#')
                    continue;
                std::vector<std::string> fields;
                std::stringstream row(line);
                std::string part;
                while (std::getline(row, part, '\t'))
                    fields.push_back(part);
                if (fields.size() != 4 || fields[0].empty() || fields[2].empty())
                    continue;
                Knowledge entry;
                entry.id = fields[0];
                std::stringstream aliases(fields[1]);
                while (std::getline(aliases, part, '|'))
                    if (!Normalise(part).empty())
                        entry.phrases.push_back(Normalise(part));
                entry.answer = fields[2];
                entry.source = fields[3];
                if (!entry.phrases.empty())
                    knowledge.push_back(std::move(entry));
            }
        }

        bool Lookup(std::string const& text, std::string& answer)
        {
            LoadKnowledge();
            // High precision: phrase aliases must appear in the player's question.
            // If multiple records score equally, defer to Ollama rather than guess.
            size_t best = 0;
            size_t bestLength = 0;
            bool tied = false;
            for (size_t i = 0; i < knowledge.size(); ++i)
            {
                for (std::string const& phrase : knowledge[i].phrases)
                {
                    if (phrase.size() <= bestLength || !Has(text, phrase))
                        continue;
                    best = i;
                    bestLength = phrase.size();
                    tied = false;
                }
                for (std::string const& phrase : knowledge[i].phrases)
                    if (phrase.size() == bestLength && bestLength > 0 &&
                        Has(text, phrase) && best != i)
                        tied = true;
            }
            if (!bestLength || tied)
                return false;
            answer = knowledge[best].answer;
            return true;
        }

        Player* Resolve(Player* speaker, ChatScope scope, std::string const& original,
                        std::string const& whisperTarget)
        {
            if (!speaker || !speaker->IsInWorld() || Script_IsAIControlled(speaker))
                return nullptr;
            std::string const text = Normalise(original);
            if (scope == ChatScope::Whisper)
            {
                HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
                for (auto const& item : sObjectAccessor.GetPlayers())
                {
                    Player* bot = item.second;
                    if (IsPlayableBot(bot) && Normalise(bot->GetName()) == Normalise(whisperTarget))
                        return bot;
                }
                return nullptr;
            }

            // Addressing a bot explicitly takes priority over a player's selection.
            // If no bot is named, party chat rotates ONE responder.
            std::vector<Player*> candidates;
            if (scope == ChatScope::Party || scope == ChatScope::Raid)
            {
                if (Group* group = speaker->GetGroup())
                {
                    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    {
                        Player* bot = ref->getSource();
                        if (IsPlayableBot(bot) && bot != speaker)
                            candidates.push_back(bot);
                    }
                }
            }
            else if (scope == ChatScope::Say)
            {
                HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
                for (auto const& item : sObjectAccessor.GetPlayers())
                {
                    Player* bot = item.second;
                    if (IsPlayableBot(bot) && Near(speaker, bot) && bot != speaker)
                    {
                        candidates.push_back(bot);
                        if (candidates.size() >= 24)
                            break;
                    }
                }
            }
            if (candidates.empty())
                return nullptr;
            for (Player* bot : candidates)
                if (Has(text, Normalise(bot->GetName())))
                    return bot;
            for (Player* bot : candidates)
                if (bot->GetObjectGuid() == speaker->GetSelectionGuid())
                    return bot;
            if (scope == ChatScope::Say)
                return nullptr; // No unsolicited answers from random nearby bots.
            return candidates[round++ % candidates.size()];
        }

        static std::string Coins(uint32_t c)
        {
            std::ostringstream out;
            uint32_t const gold = c / 10000;
            uint32_t const silver = (c / 100) % 100;
            uint32_t const copper = c % 100;
            out << gold << "g " << silver << "s";
            if (gold == 0 && silver == 0)
                out << " " << copper << "c";
            return out.str();
        }

        static std::string QuestLine(Player* bot, bool progress)
        {
            std::ostringstream out;
            uint32_t seen = 0;
            for (uint32_t slot = 0; slot < MAX_QUEST_LOG_SIZE && seen < 2; ++slot)
            {
                uint32_t id = bot->GetQuestSlotQuestId(slot);
                Quest const* quest = id ? sObjectMgr.GetQuestTemplate(id) : nullptr;
                if (!quest)
                    continue;
                QuestStatus const status = bot->GetQuestStatus(id);
                if (status == QUEST_STATUS_FAILED)
                    continue;
                if (seen++)
                    out << "; ";
                out << quest->GetTitle();
                if (status == QUEST_STATUS_COMPLETE)
                {
                    out << " (ready to hand in)";
                    continue;
                }
                if (!progress)
                    continue;
                QuestStatusData const* state = bot->GetQuestStatusData(id);
                if (!state)
                    continue;
                uint32_t counted = 0;
                for (uint32_t i = 0; i < QUEST_OBJECTIVES_COUNT && counted < 2; ++i)
                {
                    if (quest->ReqCreatureOrGOCount[i])
                    {
                        out << " " << state->m_creatureOrGOcount[i] << "/"
                            << quest->ReqCreatureOrGOCount[i];
                        ++counted;
                    }
                    if (quest->ReqItemCount[i] && counted < 2)
                    {
                        out << " items " << state->m_itemcount[i] << "/"
                            << quest->ReqItemCount[i];
                        ++counted;
                    }
                }
            }
            return seen ? out.str() : "";
        }

        static std::string SpellList(Player* bot, bool resurrectionOnly)
        {
            std::set<std::string> seen;
            for (auto const& learned : bot->GetSpellMap())
            {
                if (learned.second.state == PLAYERSPELL_REMOVED || learned.second.disabled)
                    continue;
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(learned.first);
                if (!spell || spell->IsPassiveSpell() || spell->SpellName[0].empty())
                    continue;
                std::string const name = spell->SpellName[0];
                std::string const lower = Normalise(name);
                if (resurrectionOnly &&
                    lower != "resurrection" && lower != "redemption" &&
                    lower != "ancestral spirit" && lower != "rebirth")
                    continue;
                if (!resurrectionOnly && (lower == "attack" || lower == "opening"))
                    continue;
                seen.insert(name);
            }
            if (seen.empty())
                return "";
            std::ostringstream out;
            uint32_t n = 0;
            for (std::string const& name : seen)
            {
                if (n++)
                    out << ", ";
                out << name;
                if (n == 5)
                    break;
            }
            return out.str();
        }

        Topic Recognise(std::string const& q, std::string const& original,
                        Topic previous) const
        {
            if (Any(q, {"what next", "where next", "what should we do",
                        "where should we go", "what quest next", "what do we do now"}))
                return Topic::NextQuest;
            if (Any(q, {"resurrect", "ress", "rez", "revive me", "bring me back"}))
                return Topic::Resurrection;
            if (Any(q, {"weapon", "main hand", "off hand", "what staff",
                        "what sword", "what axe", "what dagger", "what shield"}))
                return Topic::Weapon;
            if (Any(q, {"equipment", "what gear", "your gear", "you wearing",
                        "your armour", "your armor"}))
                return Topic::Gear;
            if (Any(q, {"inventory", "backpack", "what in your bags",
                        "what are you carrying", "whats in your bag"}))
                return Topic::Bags;
            if (Any(q, {"gold", "money", "coins", "cash", "wealthy", "rich", "broke",
                        "copper", "silver", "afford", "loaded"}))
                return Topic::Money;
            if (Any(q, {"quest progress", "how many", "how far along",
                        "how much longer", "objectives", "still need"}))
            {
                if (Any(q, {"quest", "wolves", "kills", "need", "left", "more"}))
                    return Topic::Progress;
            }
            if (Any(q, {"quests", "questing", "what quest", "doing for quests"}))
                return Topic::Quests;
            if (Any(q, {"your level", "what level", "level are you", "level you"}))
                return Topic::Level;
            if (Any(q, {"health", "hp", "how hurt", "are you hurt", "are you dying"}))
                return Topic::Health;
            if (Any(q, {"mana", "mp", "how much power", "running low on mana"}))
                return Topic::Mana;
            if (Any(q, {"spells", "abilities", "what can you cast",
                        "what buffs", "know any spells"}))
                return Topic::Spells;
            if (previous != Topic::None && Any(q, {"is that good", "is that a lot",
                                                  "really", "not much then", "that all"}))
                return previous;
            if (!IsQuestion(q, original))
                return Topic::None;
            return Topic::Knowledge;
        }

        bool Answer(Player* speaker, Player* bot, std::string const& message,
                    ChatScope scope, std::string& answer)
        {
            if (!IsPlayableBot(bot) || !speaker)
                return false;
            std::string const q = Normalise(message);
            std::pair<uint64_t, uint64_t> const key(
                speaker->GetObjectGuid().GetRawValue(), bot->GetObjectGuid().GetRawValue());
            Topic previous = Topic::None;
            auto found = followUps.find(key);
            if (found != followUps.end() &&
                Clock::now() - found->second.last < std::chrono::seconds(90))
                previous = found->second.topic;
            Topic topic = Recognise(q, message, previous);
            if (topic == Topic::None)
                return false;

            bool const sameParty = speaker->GetGroup() &&
                speaker->GetGroup() == bot->GetGroup();
            // Private character finances, bags and quest logs are never
            // exposed to ungrouped passers-by.
            if (!sameParty &&
                (topic == Topic::Money || topic == Topic::Bags ||
                 topic == Topic::Quests || topic == Topic::Progress ||
                 topic == Topic::NextQuest || topic == Topic::Spells))
                return false;

            switch (topic)
            {
                case Topic::Money:
                    answer = "I've got " + Coins(bot->GetMoney()) + ".";
                    if (Any(q, {"rich", "wealthy", "loaded", "lots of gold"}))
                        answer += " Depends what we're buying.";
                    break;
                case Topic::Weapon:
                {
                    Item* main = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
                    Item* off = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
                    answer = "Main hand: " +
                        std::string(main && main->GetProto() ? main->GetProto()->Name1 : "nothing");
                    answer += ". Off hand: " +
                        std::string(off && off->GetProto() ? off->GetProto()->Name1 : "nothing") + ".";
                    break;
                }
                case Topic::Gear:
                {
                    answer = "I'm wearing ";
                    uint32_t n = 0;
                    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END && n < 4; ++slot)
                    {
                        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                        {
                            if (!item->GetProto())
                                continue;
                            if (n++)
                                answer += ", ";
                            answer += item->GetProto()->Name1;
                        }
                    }
                    answer += n ? ". Ask about a slot for more." : "nothing equipped.";
                    break;
                }
                case Topic::Bags:
                {
                    uint32_t count = 0;
                    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
                        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                            ++count;
                    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
                    {
                        Item* bagItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
                        if (!bagItem)
                            continue;
                        Bag* inventory = static_cast<Bag*>(bagItem);
                        for (uint32_t slot = 0; slot < inventory->GetBagSize(); ++slot)
                            if (bot->GetItemByPos(bag, static_cast<uint8>(slot)))
                                ++count;
                    }
                    answer = "I've got " + std::to_string(count) +
                        " occupied slots across my bags. Ask about an item if you need details.";
                    break;
                }
                case Topic::Quests:
                case Topic::Progress:
                case Topic::NextQuest:
                {
                    std::string quests = QuestLine(bot, topic != Topic::Quests);
                    if (quests.empty())
                        answer = "I haven't got an active quest to suggest.";
                    else if (topic == Topic::NextQuest)
                        answer = "We could work on " + quests + ".";
                    else
                        answer = "My quests: " + quests + ".";
                    break;
                }
                case Topic::Level:
                    answer = "I'm level " + std::to_string(bot->GetLevel()) + ".";
                    break;
                case Topic::Health:
                    answer = "I'm at " +
                        std::to_string(static_cast<uint32_t>(bot->GetHealthPercent())) + "% health.";
                    break;
                case Topic::Mana:
                {
                    if (bot->GetMaxPower(POWER_MANA) == 0)
                        answer = "I don't use mana.";
                    else
                        answer = "I'm at " +
                            std::to_string(static_cast<uint32_t>(bot->GetPowerPercent(POWER_MANA))) +
                            "% mana.";
                    break;
                }
                case Topic::Resurrection:
                {
                    std::string spells = SpellList(bot, true);
                    if (spells.empty())
                        answer = "I don't know a resurrection spell.";
                    else
                        answer = "I know " + spells +
                            ", but I haven't checked whether I can cast it right now.";
                    break;
                }
                case Topic::Spells:
                {
                    std::string spells = SpellList(bot, false);
                    answer = spells.empty() ? "I can't find any usable spells in my spellbook."
                        : "Some spells I know: " + spells + ".";
                    break;
                }
                case Topic::Knowledge:
                    if (!Lookup(q, answer))
                        return false; // leave it to the LLM
                    break;
                default:
                    return false;
            }
            if (answer.empty())
                return false;
            if (answer.size() > 240)
                answer = answer.substr(0, 237) + "...";
            if (followUps.size() > 512)
                followUps.clear();
            followUps[key] = {topic, Clock::now()};
            return true;
        }

        bool Deliver(Player* player, Player* bot, ChatScope scope,
                     std::string const& answer)
        {
            if (!bot || !player || !bot->IsInWorld())
                return false;
            if (scope == ChatScope::Whisper)
            {
                BotWhisperPlayer(bot, player, answer);
                return true;
            }
            if (scope == ChatScope::Party || scope == ChatScope::Raid)
            {
                Group* group = bot->GetGroup();
                if (!group || group != player->GetGroup())
                    return false;
                WorldPacket packet;
                ChatHandler::BuildChatPacket(packet, scope == ChatScope::Party ?
                    CHAT_MSG_PARTY : CHAT_MSG_RAID, answer, LANG_UNIVERSAL,
                    bot->GetChatTag(), bot->GetObjectGuid(), bot->GetName());
                int subgroup = scope == ChatScope::Party ?
                    group->GetMemberGroup(bot->GetObjectGuid()) : -1;
                group->BroadcastPacket(&packet, false, subgroup);
                return true;
            }
            if (scope == ChatScope::Say && Near(player, bot))
            {
                bot->Say(answer, LANG_UNIVERSAL);
                return true;
            }
            return false;
        }

        bool TryQuestion(Player* player, ChatScope scope, std::string const& message,
                         std::string const& whisperTarget = "")
        {
            if (!player || !player->IsInWorld() || Script_IsAIControlled(player) ||
                message.empty() || message.size() > 220)
                return false;
            if (scope != ChatScope::Say && scope != ChatScope::Party &&
                scope != ChatScope::Raid && scope != ChatScope::Whisper)
                return false;
            if (!IsQuestion(Normalise(message), message))
                return false;
            Player* bot = Resolve(player, scope, message, whisperTarget);
            if (!bot)
                return false;
            std::string answer;
            if (!Answer(player, bot, message, scope, answer))
                return false;
            if (lastReplies.size() > 256)
                lastReplies.clear();
            auto i = lastReplies.find(bot->GetObjectGuid().GetRawValue());
            if (i != lastReplies.end() &&
                Clock::now() - i->second < std::chrono::seconds(2))
                return false;
            bool sent = Deliver(player, bot, scope, answer);
            if (sent)
                lastReplies[bot->GetObjectGuid().GetRawValue()] = Clock::now();
            return sent;
        }

        void Tick(uint32_t diff)
        {
            (void) diff;
            // Late reply: no LLM and no recursion from bot-originated /say.
            auto now = Clock::now();
            for (auto it = pending.begin(); it != pending.end(); )
            {
                if (now < it->due) { ++it; continue; }
                Player* first = ObjectAccessor::FindPlayer(ObjectGuid(it->first));
                Player* second = ObjectAccessor::FindPlayer(ObjectGuid(it->second));
                Player* witness = ObjectAccessor::FindPlayer(ObjectGuid(it->witness));
                if (IsPlayableBot(first) && IsPlayableBot(second) && witness &&
                    witness->IsInWorld() && !witness->IsInCombat() &&
                    first->GetMapId() == it->map &&
                    !first->IsInCombat() && !second->IsInCombat() &&
                    Near(first, second) && Near(second, witness))
                    second->Say(it->reply, LANG_UNIVERSAL);
                it = pending.erase(it);
            }

            if (now < nextScan)
                return;
            nextScan = now + std::chrono::seconds(45);
            if (lastSocialByPlayer.size() > 256)
                lastSocialByPlayer.clear();

            // Observe only real player locations. Never produce 1,200-bot
            // world-wide chats or background scans near empty zones.
            std::vector<Player*> realPlayers;
            std::vector<Player*> bots;
            {
                HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
                for (auto const& kv : sObjectAccessor.GetPlayers())
                {
                    Player* p = kv.second;
                    if (!p || !p->IsInWorld() || !p->IsAlive())
                        continue;
                    if (Script_IsAIControlled(p))
                    {
                        bots.push_back(p);
                        continue;
                    }
                    if (!p->IsInCombat() && realPlayers.size() < 12)
                        realPlayers.push_back(p);
                }
            }

            for (Player* player : realPlayers)
            {
                Map const* map = player->FindMap();
                if (!map || map->IsDungeon() || map->IsBattleGround())
                    continue;
                uint64_t const id = player->GetObjectGuid().GetRawValue();
                auto last = lastSocialByPlayer.find(id);
                if (last != lastSocialByPlayer.end() &&
                    now - last->second < std::chrono::seconds(240))
                    continue;

                Player* one = nullptr;
                Player* two = nullptr;
                for (Player* bot : bots)
                {
                    if (!IsPlayableBot(bot) || bot->IsInCombat() ||
                        !Near(player, bot))
                        continue;
                    if (!one) one = bot;
                    else if (bot != one && Near(one, bot)) { two = bot; break; }
                }
                if (!two)
                    continue;

                std::string first;
                std::string second;
                // Only claim a shared quest if both bots truly have it.
                uint32_t sharedQuest = 0;
                for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE && !sharedQuest; ++slot)
                {
                    uint32_t const q = one->GetQuestSlotQuestId(slot);
                    if (q && one->GetQuestStatus(q) == QUEST_STATUS_INCOMPLETE &&
                        two->GetQuestStatus(q) == QUEST_STATUS_INCOMPLETE)
                        sharedQuest = q;
                }
                if (sharedQuest && sObjectMgr.GetQuestTemplate(sharedQuest))
                {
                    std::string title = sObjectMgr.GetQuestTemplate(sharedQuest)->GetTitle();
                    if (title.size() > 65) title.resize(65);
                    first = "Still working on " + title + ".";
                    second = "Same here. Let's get on with it.";
                }
                else
                {
                    static char const* const openings[] = {
                        "You seen much happening around here?",
                        "Feels quieter than usual out here.",
                        "Think we're heading the right way?",
                        "I've been meaning to sort my bags out."
                    };
                    static char const* const replies[] = {
                        "Not much. Probably for the best.",
                        "Give it a minute. Something always turns up.",
                        "Hopefully. I'd rather not double back.",
                        "Same, but I'll worry about it later."
                    };
                    size_t index = round++ % 4;
                    first = openings[index];
                    second = replies[index];
                }
                if (first.size() > 140 || second.size() > 140)
                    continue;
                one->Say(first, LANG_UNIVERSAL);
                Pending p;
                p.first = one->GetObjectGuid().GetRawValue();
                p.second = two->GetObjectGuid().GetRawValue();
                p.witness = id;
                p.map = one->GetMapId();
                p.due = now + std::chrono::seconds(4);
                p.reply = second;
                if (pending.size() >= 8)
                    pending.erase(pending.begin());
                pending.push_back(std::move(p));
                lastSocialByPlayer[id] = now;
                // Global cap: one exchange per 45 seconds even with many players.
                break;
            }
        }
    };

    inline Engine& Instance()
    {
        static Engine instance;
        return instance;
    }
}
}
#endif
