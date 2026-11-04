#include "AzerothVoicesIntentV4.h"
#include <cassert>
using namespace AzerothVoices::IntelligenceV4;
int main()
{
    auto recognise = [](std::string const& s, Topic previous = Topic::None) {
        return Recognise(Normalise(s), s, previous);
    };
    assert(Normalise("  Gwenne, WHAT's in your bag?!") == "gwenne what s in your bag");
    assert(!Has("goldshire", "gold"));
    assert(recognise("Where should we go next?") == Topic::NextQuest);
    assert(recognise("What quests are you doing?") == Topic::Quests);
    assert(recognise("How many wolves do you still need?") == Topic::Progress);
    assert(recognise("Could you cast Greater Heal?") == Topic::SpellReady);
    assert(recognise("Is your Holy Light on cooldown?") == Topic::SpellReady);
    assert(recognise("What does spirit do?") == Topic::Knowledge);
    assert(recognise("What level is Hogger?") == Topic::Knowledge);
    assert(recognise("What level are you?") == Topic::Level);
    assert(recognise("Are you the tank?") == Topic::Role);
    assert(recognise("What's in your bag?") == Topic::Bags);
    assert(recognise("Is that a lot?", Topic::Money) == Topic::Money);
    assert(recognise("Is that a lot?") != Topic::Money);
    assert(recognise(std::string(221, 'a')) == Topic::None);
    assert(!PrivateScope(true, false)); // grouped but public /say
    assert(!PrivateScope(false, true)); // whispered by a stranger
    assert(PrivateScope(true, true));
    assert(PrivateTopic(Topic::SpellReady));
    assert(PrivateTopic(Topic::Progress));
    assert(!PrivateTopic(Topic::Role));
}
