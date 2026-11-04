#ifndef AZEROTH_VOICES_SOCIAL_V4_H
#define AZEROTH_VOICES_SOCIAL_V4_H

#include "AzerothVoicesIntentV4.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace AzerothVoices { namespace IntelligenceV4 {
using SocialClock = std::chrono::steady_clock;
using SocialTime = SocialClock::time_point;

// Native Voices supplies these from its existing persistent personality,
// sentiment and verified-event memory stores. Never generated chat as facts.
struct SocialSeed
{
    std::vector<std::string> traits, memories;
    int sentiment = 0;
    bool hasSentiment = false;
};
inline std::string SocialText(std::string value, size_t maximum)
{
    for (char& c : value) if (static_cast<unsigned char>(c) < 32 || c == '|') c = ' ';
    if (value.size() > maximum) value.resize(maximum);
    return value;
}
inline char const* Preference(uint64_t bot, SocialSeed const& seed)
{
    std::string traits;
    for (auto const& t : seed.traits) traits += " " + Normalise(t);
    if (Any(traits, {"cautious", "careful", "patient", "protective"})) return "preparing carefully";
    if (Any(traits, {"curious", "adventurous", "bold", "explorer"})) return "exploring new places";
    if (Any(traits, {"helpful", "kind", "loyal", "friendly"})) return "helping the group finish together";
    static char const* choices[] = {"finishing one task before starting another",
        "exploring new places", "preparing carefully", "helping the group finish together"};
    return choices[bot % 4];
}

struct SocialLedger
{
    struct Pair
    {
        SocialTime last{}, reaction{}, seeded{};
        uint32_t encounters = 0;
        int rapport = 0;
        SocialSeed seed;
        std::deque<std::string> topics;
    };
    struct Scene
    {
        uint64_t first = 0, second = 0, serial = 0;
        std::string subject;
        SocialTime expires{};
        bool joined = false;
    };
    struct Friendship { uint32_t exchanges = 0; SocialTime last{}; std::string subject; };
    std::map<std::pair<uint64_t,uint64_t>, Pair> pairs;
    std::map<uint64_t, Scene> scenes;
    std::map<std::pair<uint64_t,uint64_t>, Friendship> friendships;
    uint64_t nextScene = 1;

    void Prune(SocialTime now)
    {
        for (auto i=pairs.begin(); i!=pairs.end(); )
            if (now-i->second.last > std::chrono::hours(6)) i=pairs.erase(i); else ++i;
        for (auto i=scenes.begin(); i!=scenes.end(); )
            if (now >= i->second.expires) i=scenes.erase(i); else ++i;
        for (auto i=friendships.begin(); i!=friendships.end(); )
            if (now-i->second.last > std::chrono::hours(6)) i=friendships.erase(i); else ++i;
    }
    Pair& Get(uint64_t bot, uint64_t human, SocialTime now)
    {
        Prune(now);
        auto key=std::make_pair(bot,human);
        if (!pairs.count(key) && pairs.size() >= 512)
        {
            auto oldest=std::min_element(pairs.begin(),pairs.end(),
                [](auto const& a,auto const& b){return a.second.last<b.second.last;});
            pairs.erase(oldest);
        }
        Pair& p=pairs[key]; p.last=now; return p;
    }
    void Seed(uint64_t bot,uint64_t human,SocialSeed seed,SocialTime now)
    {
        Pair& p=Get(bot,human,now);
        if (seed.traits.size()>3) seed.traits.resize(3);
        if (seed.memories.size()>2) seed.memories.resize(2);
        for (auto& t:seed.traits) t=SocialText(t,64);
        for (auto& m:seed.memories) m=SocialText(m,180);
        seed.sentiment=std::max(-100,std::min(100,seed.sentiment));
        p.seed=std::move(seed); p.seeded=now;
    }
    // Called only after delivery. Store canonical topics, never raw private chat.
    int Observe(uint64_t bot,uint64_t human,std::string const& query,
                std::string const& topic,SocialTime now)
    {
        Pair& p=Get(bot,human,now); ++p.encounters;
        if (!topic.empty() && (p.topics.empty() || p.topics.back()!=topic))
        { p.topics.push_back(SocialText(topic,80)); if(p.topics.size()>3) p.topics.pop_front(); }
        int delta=0;
        if (p.reaction==SocialTime{} || now-p.reaction>=std::chrono::seconds(60))
        {
            auto q=Normalise(query);
            if (Any(q,{"thank you","thanks","good job","well done"})) delta=1;
            else if (Any(q,{"you are useless","you re useless","i hate you"})) delta=-1;
            if (delta) {p.reaction=now; p.rapport=std::max(-5,std::min(5,p.rapport+delta));}
        }
        return delta;
    }
    Scene const* FindScene(uint64_t human,uint64_t bot,SocialTime now) const
    {
        auto i=scenes.find(human);
        if(i==scenes.end() || now>=i->second.expires ||
            (bot!=i->second.first && bot!=i->second.second)) return nullptr;
        return &i->second;
    }
    uint64_t SceneFor(uint64_t human,uint64_t first,uint64_t second,
                      std::string subject,SocialTime now)
    {
        Prune(now);
        if(scenes.size()>=128) scenes.erase(scenes.begin());
        Scene s; s.first=first; s.second=second; s.serial=nextScene++;
        s.subject=SocialText(subject,80); s.expires=now+std::chrono::seconds(210);
        scenes[human]=s; return s.serial;
    }
    void Join(uint64_t human,uint64_t bot,SocialTime now)
    {
        if (FindScene(human,bot,now)) scenes[human].joined=true;
    }
    bool CanContinue(uint64_t human,uint64_t serial,SocialTime now) const
    {
        auto i=scenes.find(human);
        return i!=scenes.end() && i->second.serial==serial && !i->second.joined && now<i->second.expires;
    }
    std::vector<std::string> Exchange(uint64_t first,uint64_t second,
            std::string const& subject,SocialSeed const& a,SocialSeed const& b,SocialTime now)
    {
        auto key=std::minmax(first,second);
        std::pair<uint64_t,uint64_t> owned{key.first,key.second};
        Prune(now);
        if(!friendships.count(owned) && friendships.size()>=256) friendships.erase(friendships.begin());
        auto& f=friendships[owned]; bool familiar=f.exchanges>0;
        bool repeated=f.subject==subject; uint32_t turn=f.exchanges++;
        f.last=now; f.subject=SocialText(subject,80);
        std::string topic=SocialText(subject,65);
        std::string pa=Preference(first,a), pb=Preference(second,b);
        if (turn%3==0)
            return {(familiar?"Back to ":"I've been thinking about ")+topic+". I'd favour "+pa+".",
                pa!=pb?"I'd lean towards "+pb+", myself. We can compromise.":"That suits me. Let's hear what the others prefer.",
                "Fair enough. What do you think we should do?"};
        if (turn%3==1)
            return {repeated?"We keep coming back to "+topic+". Perhaps we should choose together.":"What do you make of "+topic+"?",
                "My preference is "+pb+". Yours?",
                "I'd pick "+pa+". No rush; let's ask the group."};
        return {"Regarding "+topic+", I'd rather hear everyone's view.",
                familiar?"We've had this sort of debate before. I still favour "+pb+".":"For me, "+pb+" matters most.",
                "A little disagreement is healthy. What would you choose?"};
    }
    std::string Context(uint64_t bot,uint64_t human,bool privateAllowed,SocialTime now)
    {
        Pair& p=Get(bot,human,now);
        std::string out="Companion preference: "+std::string(Preference(bot,p.seed))+".";
        if (privateAllowed)
        {
            out+=" Prior delivered exchanges this session: "+std::to_string(p.encounters)+".";
            if(p.seed.hasSentiment) out+=" Stored relationship score: "+std::to_string(p.seed.sentiment)+".";
            for(auto const& m:p.seed.memories) out+=" Verified shared event: "+m;
            if(!p.topics.empty()) out+=" Recent topic: "+p.topics.back()+".";
        }
        if(auto const* scene=FindScene(human,bot,now)) out+=" Current nearby discussion: "+scene->subject+".";
        return SocialText(out,650);
    }
    bool Answer(uint64_t bot,uint64_t human,std::string const& query,
                bool privateAllowed,std::string& answer,SocialTime now)
    {
        Pair& p=Get(bot,human,now); auto q=Normalise(query);
        if(Any(q,{"what are you talking about","what were you talking about","what was that about",
            "what are you two discussing","what are you discussing","can i join"}))
        {
            auto const* scene=FindScene(human,bot,now);
            if(!scene) return false;
            answer="We were discussing "+scene->subject+". I favour "+Preference(bot,p.seed)+". What do you think?";
            return true;
        }
        if(Any(q,{"what do you like","your preference","what do you prefer","your personality"}))
        { answer="I tend to favour "+std::string(Preference(bot,p.seed))+"."; return true; }
        if(FindScene(human,bot,now) && Any(q,{"i think","i prefer","let s","we should"}))
        { answer="That's a fair suggestion. I tend to favour "+std::string(Preference(bot,p.seed))+". The choice is yours."; return true; }
        if(!privateAllowed)
        {
            if(Any(q,{"thank you","thanks","good job","well done"})) {answer="You're welcome. Glad to help.";return true;}
            if(Any(q,{"hello","hi","hey","good morning","good evening"})) {answer="Hello. Good to meet you.";return true;}
            return false;
        }
        if(Any(q,{"do you remember me","what do you remember","remember our","met before"}))
        {
            if(!p.seed.memories.empty()) answer="I remember this: "+p.seed.memories.front();
            else if(!p.topics.empty()) answer="We've spoken recently about "+p.topics.back()+".";
            else answer="I don't have a recorded shared memory to recall yet.";
            return true;
        }
        if(Any(q,{"what do you think of me","do you like me","are we friends"}))
        {
            int score=p.seed.hasSentiment?p.seed.sentiment:p.rapport;
            answer=score>0?"I've enjoyed our company so far. Let's keep looking out for each other.":
                score<0?"We've had a few rough words. I'm happy to make a fresh start.":
                "We're still getting to know each other. Shared adventures should help.";
            return true;
        }
        if(Any(q,{"thank you","thanks","good job","well done"}))
        { answer=p.encounters>2?"You're welcome. It's good travelling with a familiar companion.":"You're welcome. Glad to help."; return true; }
        if(Any(q,{"hello","hi","hey","good morning","good evening"}))
        { answer=p.encounters?"Good to see you again. Ready for another stretch together?":"Hello. Good to meet you."; return true; }
        return false;
    }
};
inline SocialLedger& Social() { static SocialLedger ledger; return ledger; }
}}
#endif
