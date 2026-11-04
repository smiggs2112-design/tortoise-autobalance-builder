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
        Moment nextStats = Moment();
        uint64_t deterministicReplies = 0, ollamaFallbacks = 0, socialExchanges = 0;
        uint64_t questPlans = 0, spellChecks = 0, proactiveWarnings = 0;
        std::map<uint64_t, Moment> nextCareForPlayer;
        struct Scene
        {
            uint64_t botGuid = 0;
            Moment last = Moment();
            std::string subject;
        };
        std::map<uint64_t, Scene> conversations;
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
        Moment nextKnowledgePoll = Moment();
        std::time_t lastKnowledgeMtime = 0;
        long long lastKnowledgeSize = -1;

        void LoadKnowledge()
        {
            if (loaded)
                return;
            loaded = true;
            std::ifstream input("/opt/tortoise/data/humanlike-knowledge.tsv");
            std::string line;
            while (std::getline(input, line) && knowledge.size() < 3000)
            {
                if (line.empty() || line[0] == '#' || line.size() > 1200)
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

        static std::set<std::string> SignificantWords(std::string const& phrase)
        {
            static std::set<std::string> const stop = {
                "what", "which", "where", "when", "why", "how", "does", "are",
                "can", "you", "your", "the", "then", "that", "this", "for",
                "with", "from", "about", "some", "have", "has", "into",
                "there", "them", "they", "would", "could", "should",
                "and", "who", "its", "out"
            };
            std::set<std::string> terms;
            std::istringstream parts(Normalise(phrase));
            std::string word;
            while (parts >> word)
            {
                if (word.size() > 5 && word.back() == 's')
                    word.pop_back();
                if (word.size() >= 3 && !stop.count(word))
                    terms.insert(word);
            }
            return terms;
        }

        bool Lookup(std::string const& text, std::string& answer)
        {
            LoadKnowledge();
            // Literal phrases first, preferring the longest unambiguous match.
            size_t best = 0;
            size_t bestLength = 0;
            bool tied = false;
            for (size_t i = 0; i < knowledge.size(); ++i)
                for (std::string const& phrase : knowledge[i].phrases)
                {
                    if (!Has(text, phrase))
                        continue;
                    if (phrase.size() > bestLength)
                    {
                        bestLength = phrase.size();
                        best = i;
                        tied = false;
                    }
                    else if (phrase.size() == bestLength && best != i)
                        tied = true;
                }
            if (bestLength)
            {
                if (tied)
                    return false;
                answer = knowledge[best].answer;
                return true;
            }

            // Cheap morphology-aware matching for equivalent phrasings.
            // Require two distinctive words and >=80% alias coverage.
            // A tie across entries defers to Ollama instead of guessing.
            std::set<std::string> const words = SignificantWords(text);
            float bestScore = 0.0f;
            best = 0;
            tied = false;
            for (size_t i = 0; i < knowledge.size(); ++i)
                for (std::string const& alias : knowledge[i].phrases)
                {
                    std::set<std::string> const terms = SignificantWords(alias);
                    if (terms.size() < 2)
                        continue;
                    uint32_t matched = 0;
                    for (std::string const& term : terms)
                        if (words.count(term))
                            ++matched;
                    float coverage = static_cast<float>(matched) /
                        static_cast<float>(terms.size());
                    if (matched < 2 || coverage < 0.80f)
                        continue;
                    float score = matched * 10.0f + coverage;
                    if (score > bestScore)
                    {
                        bestScore = score;
                        best = i;
                        tied = false;
                    }
                    else if (score == bestScore && best != i)
                        tied = true;
                }
            if (bestScore == 0.0f || tied)
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

        bool Answer(Player* speaker, Player* bot, std::string const& message,
                    ChatScope scope, std::string& answer)
        {
            if (!IsPlayableBot(bot) || !speaker)
                return false;
            std::string const q = Normalise(message);
            if (IntelligenceV4::Any(q, {"what are you talking about",
                "what are you two discussing", "what was that about",
                "what were you talking about", "what are you discussing"}))
            {
                auto scene = conversations.find(speaker->GetObjectGuid().GetRawValue());
                if (scene != conversations.end() &&
                    Clock::now() - scene->second.last < std::chrono::seconds(210))
                {
                    answer = "We were talking about " + scene->second.subject + ".";
                    ++deterministicReplies;
                    return true;
                }
            }
            std::pair<uint64_t, uint64_t> const key(
                speaker->GetObjectGuid().GetRawValue(), bot->GetObjectGuid().GetRawValue());
            Topic previous = Topic::None;
            auto found = followUps.find(key);
            if (found != followUps.end() &&
                Clock::now() - found->second.last < std::chrono::seconds(90))
                previous = found->second.topic;
            bool const privateChannel = scope == ChatScope::Party ||
                scope == ChatScope::Raid || scope == ChatScope::Whisper;
            auto const state = IntelligenceV4::ReadState(speaker, bot, message, privateChannel, previous);
            Topic const topic = state.intent;
            answer = state.answer;
            if (answer.empty() && topic == Topic::Knowledge)
                Lookup(q, answer);
            if (answer.empty()) return false;
            answer = IntelligenceV4::Limit(answer, 240);
            if (topic == Topic::NextQuest || topic == Topic::Progress) ++questPlans;
            if (topic == Topic::SpellReady) ++spellChecks;
            if (followUps.size() > 512)
                followUps.clear();
            followUps[key] = {topic, Clock::now()};
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
            Player* bot = Resolve(player, scope, message, whisperTarget);
            // A player can join the ongoing two-bot discussion in /say
            // without needing to name a participant.
            if (!bot && scope == ChatScope::Say &&
                IntelligenceV4::Any(Normalise(message),
                    {"what are you talking about", "what was that about",
                     "what are you two discussing", "what are you discussing"}))
            {
                auto scene = conversations.find(player->GetObjectGuid().GetRawValue());
                if (scene != conversations.end() &&
                    Clock::now() - scene->second.last < std::chrono::seconds(210))
                {
                    Player* participant = ObjectAccessor::FindPlayer(ObjectGuid(scene->second.botGuid));
                    if (IsPlayableBot(participant) && Near(player, participant))
                        bot = participant;
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
                lastReplies[bot->GetObjectGuid().GetRawValue()] = Clock::now();
            return sent;
        }

        void Tick(uint32_t diff)
        {
            (void) diff;
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
            // Hot-reload the mounted, read-only TSV without rebuilding or
            // restarting mangosd. This is a bounded 60-second filesystem check.
            if (now >= nextKnowledgePoll)
            {
                nextKnowledgePoll = now + std::chrono::seconds(60);
                struct stat kb = {};
                if (::stat("/opt/tortoise/data/humanlike-knowledge.tsv", &kb) == 0 &&
                    (lastKnowledgeMtime != kb.st_mtime ||
                     lastKnowledgeSize != static_cast<long long>(kb.st_size)))
                {
                    knowledge.clear();
                    loaded = false;
                    LoadKnowledge();
                    lastKnowledgeMtime = kb.st_mtime;
                    lastKnowledgeSize = static_cast<long long>(kb.st_size);
                }
            }
            for (auto it = pending.begin(); it != pending.end(); )
            {
                if (now < it->due) { ++it; continue; }
                Player* first = ObjectAccessor::FindPlayer(ObjectGuid(it->first));
                Player* second = ObjectAccessor::FindPlayer(ObjectGuid(it->second));
                Player* witness = ObjectAccessor::FindPlayer(ObjectGuid(it->witness));
                if (IsPlayableBot(first) && IsPlayableBot(second) && witness &&
                    witness->IsInWorld() && !witness->IsInCombat() &&
                    witness->FindMap() && !witness->FindMap()->IsDungeon() &&
                    !witness->FindMap()->IsBattleGround() &&
                    first->GetMapId() == it->map &&
                    !first->IsInCombat() && !second->IsInCombat() &&
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

                std::string first;
                std::string second;
                // Only claim a shared quest if both bots truly have it.
                uint32_t sharedQuest = 0;
                for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE && !sharedQuest; ++slot)
                {
                    uint32_t const q = GetPlayerQuestSlotQuestId(one, slot);
                    if (q && one->GetQuestStatus(q) == QUEST_STATUS_INCOMPLETE &&
                        two->GetQuestStatus(q) == QUEST_STATUS_INCOMPLETE)
                        sharedQuest = q;
                }
                std::string topic = "our travels";
                if (sharedQuest && sObjectMgr.GetQuestTemplate(sharedQuest))
                {
                    std::string title = sObjectMgr.GetQuestTemplate(sharedQuest)->GetTitle();
                    if (title.size() > 65) title.resize(65);
                    topic = title;
                    first = (round++ % 2 == 0) ?
                        "How is " + title + " coming along?" :
                        "Still working on " + title + ".";
                    second = (round++ % 3 == 0) ?
                        "Slowly. Let's finish it properly." :
                        "I'm still working on it. We'll get there.";
                }
                else if (IntelligenceV4::IsHealer(one) &&
                         two->GetHealthPercent() < 70.0f)
                {
                    topic = "getting ready for the next fight";
                    first = "You're looking a bit worn down. Need a moment?";
                    second = "A short rest would help.";
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
                if (conversations.size() > 128) conversations.clear();
                conversations[id] = {one->GetObjectGuid().GetRawValue(), now, topic};
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
