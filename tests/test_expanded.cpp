#include "AzerothVoicesIntelligenceV4.h"
#include <cassert>
using namespace AzerothVoices::IntelligenceV4;

static NativeSources Sources(uint32_t item,int32_t entry)
{
    NativeSources facts;facts.available=true;
    if(item==900) {facts.drops={100};facts.vendors={300};}
    if(entry==100)facts.loot={900};
    return facts;
}
static SocialSeed Profile(Player*,Player*)
{
    SocialSeed seed;seed.traits={"cautious"};seed.memories={"We finished Wolf trouble together."};
    seed.hasSentiment=true;seed.sentiment=20;return seed;
}
int main()
{
    Group group;Player human,bot,stranger;human.guid.id=1;bot.guid.id=2;stranger.guid.id=3;
    human.group=&group;bot.group=&group;bot.bot=true;
    GroupReference member{&bot,nullptr};group.first=&member;
    Quest first,overlap,far,available,locked,rewarded,done;
    first.id=1;first.title="Wolf trouble";first.ReqCreatureOrGOId[0]=100;first.ReqCreatureOrGOCount[0]=10;
    overlap.id=2;overlap.title="Another wolf hunt";overlap.ReqCreatureOrGOId[0]=100;overlap.ReqCreatureOrGOCount[0]=5;
    far.id=3;far.title="Distant foe";far.ReqCreatureOrGOId[0]=200;far.ReqCreatureOrGOCount[0]=10;
    available.id=4;available.title="Fresh work";
    locked.id=5;locked.title="Locked work";locked.eligible=false;
    rewarded.id=6;rewarded.title="Old work";human.rewarded.insert(6);
    done.id=7;done.title="Turn in first";
    first.nextChain=2;overlap.nextChain=1;overlap.prev=1;
    sObjectMgr.quests={{1,&first},{2,&overlap},{3,&far},{4,&available},{5,&locked},{6,&rewarded},{7,&done}};
    human.log[0]=1;human.log[1]=2;human.log[2]=3;
    human.status={{1,QUEST_STATUS_INCOMPLETE},{2,QUEST_STATUS_INCOMPLETE},{3,QUEST_STATUS_INCOMPLETE}};
    bot.log[0]=1;bot.status[1]=QUEST_STATUS_INCOMPLETE;
    human.progress[1].m_creatureOrGOcount[0]=3;human.progress[2]={};human.progress[3]={};
    bot.progress[1].m_creatureOrGOcount[0]=8;
    for(uint32_t id:{100,200,300})sObjectMgr.creatures[id]=std::make_unique<CreatureInfo>();
    sObjectMgr.creatures[100]->name="Hogger";sObjectMgr.creatures[100]->npc_flags=UNIT_NPC_FLAG_QUESTGIVER;
    sObjectMgr.creatures[200]->name="Distant foe creature";
    sObjectMgr.creatures[300]->name="Quartermaster Copper";sObjectMgr.creatures[300]->npc_flags=UNIT_NPC_FLAG_VENDOR;
    sObjectMgr.spawns[1].creature_id={100,0,0,0};sObjectMgr.spawns[1].position={1,20,30};
    sObjectMgr.spawns[2].creature_id={200,0,0,0};sObjectMgr.spawns[2].position={1,1500,1500};
    sObjectMgr.starts={{100,4},{100,5},{100,6}};sObjectMgr.ends={{100,7}};
    sObjectMgr.objects[500]={"Ancient chest",3};sObjectMgr.objectSpawns[1]={500,{1,40,50}};
    sObjectMgr.items[900]={"Hunting Trophy",10,5};
    sObjectMgr.items[901]={"Old Sword",5,1};sObjectMgr.items[902]={"Old Sword",6,1};
    sSpellMgr.entries[11].SpellName[0]="Arcane Spark";sSpellMgr.entries[11].manaCost=5;
    sSpellMgr.entries[12].SpellName[0]="Arcane Spark";sSpellMgr.entries[12].manaCost=10;
    Native().sources=&Sources;Native().profile=&Profile;
    auto& world=Knowledge();world=Index{};world.Build();std::string answer;
    assert(world.Search("Who is Hogger?",answer,&human) && answer.find("Hogger")!=std::string::npos);
    assert(world.Search("Where is Hogger?",answer,&human) && answer.find("world X 20")!=std::string::npos);
    assert(world.Search("Where is object Ancient chest?",answer,&human) && answer.find("world X 40")!=std::string::npos);
    assert(world.Search("What does Hogger drop?",answer,&human) && answer.find("Hunting Trophy")!=std::string::npos);
    assert(world.Search("Where can I get item Hunting Trophy?",answer,&human) && answer.find("Hogger")!=std::string::npos && answer.find("Quartermaster Copper")!=std::string::npos);
    assert(world.Search("What is item Old Sword?",answer,&human) && answer.find("several")!=std::string::npos);
    assert(world.Search("What is spell Arcane Spark?",answer,&human) && answer.find("rank 2")!=std::string::npos);
    assert(world.Search("What is spell 11?",answer,&human) && answer.find("cost 5")!=std::string::npos);
    auto point=world.Nearest(&stranger,100);assert(point.found);
    stranger.map=2;assert(!world.Nearest(&stranger,100).found);
    auto plan=PlanQuests(&human,&bot);
    assert(!plan.steps.empty() && plan.steps[0].id==1 && plan.steps[0].overlap==1 && plan.steps[0].shared==1);
    assert(plan.detail.find("straight-line")!=std::string::npos);
    auto discovery=PlanQuests(&human,&bot,true);
    assert(discovery.steps.size()==1 && discovery.steps[0].id==4);
    assert(discovery.detail.find("giver conditions")!=std::string::npos);
    human.log[3]=7;human.status[7]=QUEST_STATUS_COMPLETE;
    assert(PlanQuests(&human,&bot).steps[0].id==7);
    auto chain=QuestChain(&human,&first);
    assert(chain.find("Another wolf hunt")!=std::string::npos && chain.size()<700);
    auto state=ReadState(&human,&bot,"What quests are nearby?",true);
    assert(state.intent==Topic::AvailableQuests && state.evidence.find("Fresh work")!=std::string::npos);
    assert(ReadState(&stranger,&bot,"What quests are nearby?",true).answer.empty());
    state=ReadState(&human,&bot,"How many for my Wolf trouble quest?",true);
    assert(state.answer.find("3/10")!=std::string::npos && state.questId==1);
    state=ReadState(&human,&bot,"How many more?",true);
    assert(state.answer.find("3/10")!=std::string::npos);
    state=ReadState(&human,&bot,"How many for your Wolf trouble quest?",true);
    assert(state.answer.find("8/10")!=std::string::npos);
    state=ReadState(&human,&bot,"How many more?",true);
    assert(state.answer.find("8/10")!=std::string::npos);

    Item potion;potion.proto.Name1="Healing Potion";potion.count=7;
    bot.inventory[{0,23}]=&potion;
    state=ReadState(&human,&bot,"What's in your backpack?",true);
    assert(state.evidence.find("Healing Potion x7")!=std::string::npos);
    assert(RequestContext(state).find("Healing Potion x7")!=std::string::npos);
    assert(ReadState(&stranger,&bot,"What's in your backpack?",true).evidence.empty());
    assert(ReadState(&human,&bot,"What's in your backpack?",false).evidence.empty());

    auto now=SocialClock::now();SocialLedger ledger;
    SocialSeed seed=Profile(&bot,&human);ledger.Seed(2,1,seed,now);
    assert(ledger.Answer(2,1,"Do you remember me?",true,answer,now) && answer.find("Wolf trouble")!=std::string::npos);
    assert(!ledger.Answer(2,1,"Do you remember me?",false,answer,now));
    assert(ledger.Observe(2,1,"thanks","quest plans",now)==1);
    assert(ledger.Observe(2,1,"thanks","quest plans",now+std::chrono::seconds(1))==0);
    assert(ledger.Context(2,1,false,now).find("Wolf trouble")==std::string::npos);
    auto serial=ledger.SceneFor(1,2,3,"our next quest",now);
    assert(ledger.CanContinue(1,serial,now));ledger.Join(1,2,now);assert(!ledger.CanContinue(1,serial,now));
    auto lines=ledger.Exchange(2,3,"a quest",seed,SocialSeed{},now);
    auto next=ledger.Exchange(2,3,"a quest",seed,SocialSeed{},now+std::chrono::seconds(300));
    assert(lines.size()==3 && lines!=next);
    ledger.Prune(now+std::chrono::hours(7));assert(ledger.pairs.empty() && ledger.scenes.empty() && ledger.friendships.empty());
    state=ReadState(&human,&bot,"Do you remember me?",true);
    assert(state.answer.find("Wolf trouble")!=std::string::npos);
    assert(RequestContext(state).find("Verified shared event")!=std::string::npos);
    assert(ReadState(&human,&bot,"Do you remember me?",false).answer.empty());
    assert(RequestContext(state).size()<=2400);
}
