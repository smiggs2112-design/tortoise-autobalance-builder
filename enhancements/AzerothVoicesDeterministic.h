#ifndef AZEROTH_VOICES_DETERMINISTIC_H
#define AZEROTH_VOICES_DETERMINISTIC_H

// Local, bounded deterministic conversation. Never invokes Ollama or TortoiseBots
// command handlers. This runs only on the world thread via the scripts module.
#include "AzerothVoicesBotSeam.h"
#include "Group.h"
#include "Item.h"
#include "Bag.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "SpellMgr.h"
#include "Map.h"
#include "Log.h"
#include "AzerothVoicesIntelligenceV4.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <map>
#include <set>
#include <sstream>
#include <sys/stat.h>
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
            a->GetTeam() == b->GetTeam() && a->IsWithinDistInMap(b, 25.0f);
    }

    using Topic = IntelligenceV4::Topic;

    struct Engine
    {
        std::map<uint64_t, Moment> lastReplies;
        std::map<uint64_t, Moment> lastSocialByPlayer;
        std::map<std::string, Moment> lastSocialByMap;
        Moment nextScan = Moment();
        Moment nextStats = Moment();
        uint64_t deterministicReplies = 0, ollamaFallbacks = 0, socialExchanges = 0;
        uint64_t questPlans = 0, spellChecks = 0, proactiveWarnings = 0;
        std::map<uint64_t, Moment> nextCareForPlayer;
        uint32_t round = 0;
        struct Pending
        {
            uint64_t first = 0;
            uint64_t second = 0;
            uint64_t witness = 0, scene = 0, partner = 0;
            uint32_t map = 0;
            Moment due = Moment();
            std::string reply;
        };
        std::vector<Pending> pending;

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
                        if (IsPlayableBot(bot) && bot != speaker &&
                            (scope != ChatScope::Party || group->GetMemberGroup(bot->GetObjectGuid()) ==
                                group->GetMemberGroup(speaker->GetObjectGuid())))
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

        bool Answer(Player* speaker, Player* bot, std::string const& message,
                    ChatScope scope, std::string& answer)
        {
            if (!IsPlayableBot(bot) || !speaker)
                return false;
            bool const privateChannel = scope == ChatScope::Party ||
                scope == ChatScope::Raid || scope == ChatScope::Whisper;
            auto const state = IntelligenceV4::ReadState(speaker, bot, message, privateChannel);
            Topic const topic = state.intent;
            answer = state.answer;
            if (answer.empty()) return false;
            answer = IntelligenceV4::Limit(answer, 240);
            if (topic == Topic::NextQuest || topic == Topic::Progress) ++questPlans;
            if (topic == Topic::SpellReady) ++spellChecks;
            ++deterministicReplies;
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
            // A nearby human turn interrupts queued ambient lines immediately,
            // including statements which subsequently use the normal LLM path.
            auto activeScene=IntelligenceV4::Social().scenes.find(player->GetObjectGuid().GetRawValue());
            if(activeScene!=IntelligenceV4::Social().scenes.end() && Clock::now()<activeScene->second.expires)
            {
                Player* participant=ObjectAccessor::FindPlayer(ObjectGuid(activeScene->second.first));
                if(IsPlayableBot(participant) && Near(player,participant))
                    IntelligenceV4::Social().Join(player->GetObjectGuid().GetRawValue(),activeScene->second.first,Clock::now());
            }
            if (!IsQuestion(Normalise(message), message) &&
                !Any(Normalise(message), {"really", "not much then", "that all"}) &&
                IntelligenceV4::Recognise(Normalise(message),message)!=Topic::Social) return false;
            Player* bot = Resolve(player, scope, message, whisperTarget);
            // A player can join the ongoing two-bot discussion in /say
            // without needing to name a participant.
            if (!bot && scope == ChatScope::Say &&
                IntelligenceV4::Any(Normalise(message),{"what are you talking about","what was that about",
                    "what are you two discussing","what are you discussing","can i join"}))
            {
                auto scene=IntelligenceV4::Social().scenes.find(player->GetObjectGuid().GetRawValue());
                if(scene!=IntelligenceV4::Social().scenes.end() && Clock::now()<scene->second.expires)
                {
                    Player* participant=ObjectAccessor::FindPlayer(ObjectGuid(scene->second.first));
                    if(IsPlayableBot(participant) && Near(player,participant))bot=participant;
                }
            }
            if (!bot)
                return false;
            std::string answer;
            if (!Answer(player, bot, message, scope, answer))
            {
                ++ollamaFallbacks;
                return false;
            }
            if (lastReplies.size() > 256)
                lastReplies.clear();
            auto i = lastReplies.find(bot->GetObjectGuid().GetRawValue());
            if (i != lastReplies.end() &&
                Clock::now() - i->second < std::chrono::seconds(2))
                return false;
            bool sent = Deliver(player, bot, scope, answer);
            if (sent)
            {
                lastReplies[bot->GetObjectGuid().GetRawValue()] = Clock::now();
                auto topic=IntelligenceV4::Recognise(Normalise(message),message);
                IntelligenceV4::RecordInteraction(player,bot,message,topic);
                IntelligenceV4::Social().Join(player->GetObjectGuid().GetRawValue(),bot->GetObjectGuid().GetRawValue(),Clock::now());
            }
            return sent;
        }

        void Tick(uint32_t diff)
        {
            (void) diff;
            if(!IntelligenceV4::Limits().enabled) {pending.clear();return;}
            // Late reply: no LLM and no recursion from bot-originated /say.
            auto now = Clock::now();
            if (now >= nextStats)
            {
                nextStats = now + std::chrono::minutes(10);
                sLog.outString("[VoicesV4] local_answers=%llu fallback=%llu quests=%llu "
                    "spell_checks=%llu social=%llu proactive=%llu world_index_queries=%llu hits=%llu",
                    static_cast<unsigned long long>(deterministicReplies),
                    static_cast<unsigned long long>(ollamaFallbacks),
                    static_cast<unsigned long long>(questPlans),
                    static_cast<unsigned long long>(spellChecks),
                    static_cast<unsigned long long>(socialExchanges),
                    static_cast<unsigned long long>(proactiveWarnings),
                    static_cast<unsigned long long>(IntelligenceV4::Knowledge().lookups),
                    static_cast<unsigned long long>(IntelligenceV4::Knowledge().hits));
            }
            IntelligenceV4::Mechanics().Poll();
            for (auto it = pending.begin(); it != pending.end(); )
            {
                if (now < it->due) { ++it; continue; }
                Player* first = ObjectAccessor::FindPlayer(ObjectGuid(it->first));
                Player* second = ObjectAccessor::FindPlayer(ObjectGuid(it->second));
                Player* witness = ObjectAccessor::FindPlayer(ObjectGuid(it->witness));
                Player* partner = ObjectAccessor::FindPlayer(ObjectGuid(it->partner));
                if (IntelligenceV4::Social().CanContinue(it->witness,it->scene,now) &&
                    IsPlayableBot(first) && IsPlayableBot(second) && IsPlayableBot(partner) && witness &&
                    witness->IsInWorld() && !witness->IsInCombat() &&
                    witness->FindMap() && !witness->FindMap()->IsDungeon() &&
                    !witness->FindMap()->IsBattleGround() &&
                    first->GetMapId() == it->map &&
                    !first->IsInCombat() && !second->IsInCombat() && !partner->IsInCombat() && Near(partner,witness) &&
                    Near(first, witness) && Near(first, second) && Near(second, witness))
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

            // Party wellbeing prompts are advisory only. The existing
            // TortoiseBots heal, rebuff and ready-check systems own gameplay.
            // Run even in dungeons, but never during combat or battlegrounds.
            bool advised = false;
            if (nextCareForPlayer.size() > 256) nextCareForPlayer.clear();
            for (Player* player : realPlayers)
            {
                if (advised || !player->GetGroup() || !player->FindMap() ||
                    player->FindMap()->IsBattleGround()) continue;
                uint64_t id = player->GetObjectGuid().GetRawValue();
                auto last = nextCareForPlayer.find(id);
                if (last != nextCareForPlayer.end() && now < last->second) continue;
                Player* healer = nullptr;
                Player* tank = nullptr;
                for (GroupReference* ref = player->GetGroup()->GetFirstMember(); ref; ref = ref->next())
                {
                    Player* member = ref->getSource();
                    if (!IsPlayableBot(member) || member->IsInCombat() || !Near(player, member))
                        continue;
                    if (!healer && IntelligenceV4::IsHealer(member)) healer = member;
                    if (!tank && IntelligenceV4::IsTank(member)) tank = member;
                }
                Player* speaker = healer ? healer : tank;
                if (!speaker) continue;
                std::string advice;
                if (healer && healer->GetMaxPower(POWER_MANA) > 0 &&
                    healer->GetPowerPercent(POWER_MANA) < 25.0f)
                    advice = "I'm low on mana. Give me a moment before the next pull.";
                else if (player->GetMaxPower(POWER_MANA) > 0 &&
                         player->GetPowerPercent(POWER_MANA) < 20.0f)
                    advice = "You're short on mana. We can wait a moment.";
                else if (player->GetHealthPercent() < 35.0f && healer)
                    advice = "Your health is low. We should recover before moving on.";
                if (advice.empty()) continue;
                if (Deliver(player, speaker, ChatScope::Party, advice))
                {
                    nextCareForPlayer[id] = now + std::chrono::minutes(5);
                    ++proactiveWarnings;
                    advised = true;
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

                std::string topic="how to spend the next stretch of our travels";
                std::string shared;
                if(IntelligenceV4::HasSharedQuest(one,two,shared))topic="finishing "+IntelligenceV4::Limit(shared,55);
                else if(player->GetGroup() && player->GetGroup()==one->GetGroup() &&
                        player->GetGroup()==two->GetGroup())
                {
                    auto advice=IntelligenceV4::PlanQuests(player,one);
                    if(!advice.steps.empty())topic="our plans for "+IntelligenceV4::Limit(advice.steps.front().title,50);
                }
                auto seedA=IntelligenceV4::CompanionSeed(one,player);
                auto seedB=IntelligenceV4::CompanionSeed(two,player);
                auto lines=IntelligenceV4::Social().Exchange(one->GetObjectGuid().GetRawValue(),
                    two->GetObjectGuid().GetRawValue(),topic,seedA,seedB,now);
                uint64_t scene=IntelligenceV4::Social().SceneFor(id,one->GetObjectGuid().GetRawValue(),
                    two->GetObjectGuid().GetRawValue(),topic,now);
                one->Say(IntelligenceV4::Limit(lines.front(),220),LANG_UNIVERSAL);
                for(size_t turn=1;turn<lines.size() && pending.size()<8;++turn)
                {
                    Pending p;
                    p.first=one->GetObjectGuid().GetRawValue();
                    p.partner=two->GetObjectGuid().GetRawValue();
                    p.second=turn%2?two->GetObjectGuid().GetRawValue():one->GetObjectGuid().GetRawValue();
                    p.witness=id;p.scene=scene;p.map=one->GetMapId();
                    p.due=now+std::chrono::seconds(6*turn);
                    p.reply=IntelligenceV4::Limit(lines[turn],220);
                    pending.push_back(std::move(p));
                }
                lastSocialByPlayer[id]=now;
                ++socialExchanges;
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
