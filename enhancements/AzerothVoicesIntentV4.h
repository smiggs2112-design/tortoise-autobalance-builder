#ifndef AZEROTH_VOICES_INTENT_V4_H
#define AZEROTH_VOICES_INTENT_V4_H
#include <cctype>
#include <initializer_list>
#include <string>
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

inline bool Has(std::string const& text, std::string const& phrase)
{
    return !phrase.empty() &&
        (" " + text + " ").find(" " + phrase + " ") != std::string::npos;
}
inline bool Any(std::string const& text, std::initializer_list<char const*> p)
{
    for (auto* word : p) if (Has(text, word)) return true;
    return false;
}


inline bool Contains(std::string const& a, std::string const& b) { return Has(a, b); }
    inline bool IsQuestion(std::string const& normalised, std::string const& original)
    {
        return original.find('?') != std::string::npos ||
            Any(normalised, {"what", "where", "who", "how", "when", "which", "why",
                             "can", "could", "do", "does", "got", "have", "are",
                             "is", "tell me", "show me"});
    }

    enum class Topic
    {
        None, Money, Weapon, Gear, Bags, Quests, Progress, Level,
        Health, Mana, Spells, Resurrection, NextQuest, Knowledge, Role, SpellReady, AvailableQuests, QuestChain, QuestOverlap, Social
    };

inline Topic Recognise(std::string const& q, std::string const& original,
                        Topic previous = Topic::None)
        {
            if (q.size() > 220) return Topic::None;
            if (Any(q,{"do you remember","remember our","met before","what do you think of me",
                "do you like me","are we friends","what do you like","what do you prefer","your preference",
                "your personality","what are you talking about","what were you talking about","what was that about",
                "what are you two discussing","what are you discussing","can i join","thank you","thanks",
                "good job","well done","hello","good morning","good evening"}) || q=="hi" || q=="hey" ||
                (!IsQuestion(q,original) && Any(q,{"i think","i prefer","let s","we should"}))) return Topic::Social;
            if (Any(q,{"quest chain","next in the chain","quest prerequisite","what comes after","what happens after","follow up quest"})) return Topic::QuestChain;
            if (Any(q,{"nearby quests","available quests","quests nearby","quests are nearby","quests around here","got any work around here","pick up quests","quests can i accept","quest givers nearby"})) return Topic::AvailableQuests;
            if (Any(q,{"overlapping quests","shared objectives","quests overlap","same targets"})) return Topic::QuestOverlap;
            if (Any(q,{"do you have","have any","carrying any"}) && !Any(q,{"quest","quests","spell","spells"}) &&
                (!Any(q,{"gold","money","coins","cash","silver","copper"}) || Any(q,{"ore","item","items","bars"}))) return Topic::Bags;
            if (Any(q, {"your role", "are you the tank", "are you a healer", "are you dps"}))
                return Topic::Role;
            if (Any(q, {"can you cast", "could you cast", "can you use", "ready to cast",
                        "spell ready", "cooldown", "do you know"}))
                return Topic::SpellReady;
            // General mechanics are not a question about this bot's own
            // health/mana/level. Prefer the offline knowledge catalogue.
            if ((Any(q, {"what is", "what does", "how does", "explain",
                         "what level is", "how to"}) &&
                 !Any(q, {"your", "you", "yours", "my", "mine"})) &&
                IsQuestion(q, original))
                return Topic::Knowledge;
            if (Any(q, {"what next", "where next", "what should we do",
                        "where should we go", "what quest next", "what do we do now"}))
                return Topic::NextQuest;
            if (Any(q, {"resurrect", "resurrection", "ress", "rez", "revive me", "bring me back"}))
                return Topic::Resurrection;
            if (Any(q, {"weapon", "main hand", "off hand", "what staff",
                        "what sword", "what axe", "what dagger", "what shield"}))
                return Topic::Weapon;
            if (Any(q, {"equipment", "what gear", "your gear", "you wearing",
                        "your armour", "your armor"}))
                return Topic::Gear;
            if (Any(q, {"inventory", "backpack", "what in your bags",
                        "what are you carrying", "whats in your bag", "what s in your bag", "what is in your bag"}))
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


inline bool PrivateScope(bool sameParty, bool privateChannel)
{
    return sameParty && privateChannel;
}
inline bool PrivateTopic(Topic t)
{
    return t == Topic::Money || t == Topic::Bags || t == Topic::Quests ||
        t == Topic::Progress || t == Topic::NextQuest || t == Topic::Spells ||
        t == Topic::Resurrection || t == Topic::SpellReady ||
        t == Topic::AvailableQuests || t == Topic::QuestOverlap;
}
inline char const* IntentName(Topic t)
{
    switch (t)
    {
        case Topic::NextQuest: case Topic::QuestOverlap: return "quest_plan";
        case Topic::AvailableQuests: return "quest_discovery";
        case Topic::QuestChain: return "quest_chain";
        case Topic::Social: return "conversation and shared memories";
        case Topic::Quests: case Topic::Progress: return "quest_progress";
        case Topic::SpellReady: return "spell_readiness";
        case Topic::Knowledge: return "game_knowledge";
        case Topic::None: return "conversation";
        default: return "character_fact";
    }
}
}} // namespaces
#endif
