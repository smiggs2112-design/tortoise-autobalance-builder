#include "AzerothVoicesIntelligenceV4.h"
#include <cassert>
using namespace AzerothVoices::IntelligenceV4;
int main()
{
    Group group; Player human, bot, stranger;
    human.group=&group; bot.group=&group; bot.bot=true; bot.roles=2;
    human.guid.id=1; bot.guid.id=2; stranger.guid.id=3;
    human.money=9999999; bot.money=12345;
    auto state=ReadState(&human,&bot,"How much gold have you got?",true);
    assert(state.valid && state.privateAllowed && state.intent==Topic::Money);
    assert(state.answer.find("1g 23s")!=std::string::npos);
    assert(RequestContext(state).find(state.answer)!=std::string::npos);
    assert(ReadState(&human,&bot,"How much gold have you got?",false).answer.empty());
    assert(ReadState(&stranger,&bot,"How much gold have you got?",true).answer.empty());
    assert(ReadState(&human,&bot,"What is my health?",true).answer.empty());
    assert(ReadState(&human,&bot,"Is that a lot?",true).intent==Topic::Money);
    RecentIntents().begin()->second.seen-=std::chrono::seconds(91);
    assert(ReadState(&human,&bot,"Is that a lot?",true).intent!=Topic::Money);
    assert(ReadState(&human,&bot,std::string(221,'x'),true).valid==false);
    assert(ReadState(&human,&bot,"Are you a healer?",false).answer.find("healer")!=std::string::npos);
    assert(!ReadState(nullptr,&bot,"Hello?",true).valid);
    bot.online=false; assert(!ReadState(&human,&bot,"Hello?",true).valid); bot.online=true;

    Quest done, active;
    done.id=1; done.title="Finished work";
    active.id=2; active.title="Wolf trouble"; active.ReqCreatureOrGOCount[0]=10;
    active.ReqCreatureOrGOId[0]=100;
    sObjectMgr.quests={{1,&done},{2,&active}};
    human.log[0]=1; human.log[1]=2; bot.log[0]=2;
    human.status={{1,QUEST_STATUS_COMPLETE},{2,QUEST_STATUS_INCOMPLETE}};
    bot.status[2]=QUEST_STATUS_INCOMPLETE;
    human.progress[2].m_creatureOrGOcount[0]=3;
    bot.progress[2].m_creatureOrGOcount[0]=8;
    std::string answer;
    assert(QuestPlan(&human,&bot,answer) && answer.find("Finished work")!=std::string::npos);
    state=ReadState(&human,&bot,"How many for my Wolf trouble quest?",true);
    assert(state.answer.find("3/10")!=std::string::npos);
    state=ReadState(&human,&bot,"How many for your Wolf trouble quest?",true);
    assert(state.answer.find("8/10")!=std::string::npos);
    assert(ReadState(&stranger,&bot,"How many for my Wolf trouble quest?",true).answer.empty());
    human.status[2]=QUEST_STATUS_FAILED;
    assert(!QuestByName(&human,"wolf trouble",answer));
    human.status[2]=QUEST_STATUS_INCOMPLETE;

    sSpellMgr.entries[1].SpellName[0]="Greater Heal";
    sSpellMgr.entries[1].manaCost=50; bot.spells[1]={};
    bot.cooldown=true;
    state=ReadState(&human,&bot,"Can you cast Greater Heal?",true);
    assert(state.answer.find("cooldown")!=std::string::npos);
    assert(state.answer.find("unverified")!=std::string::npos);
    bot.cooldown=false; bot.power=1;
    assert(ReadState(&human,&bot,"Can you cast Greater Heal?",true).answer.find("base cost")!=std::string::npos);
    sSpellMgr.entries[1].Reagent[0]=123; sSpellMgr.entries[1].ReagentCount[0]=1;
    assert(ReadState(&human,&bot,"Can you cast Greater Heal?",true).answer.find("reagent")!=std::string::npos);
    assert(ReadState(&stranger,&bot,"Can you cast Greater Heal?",true).answer.empty());

    Catalogue catalogue;
    catalogue.loaded=true;
    catalogue.knowledge.push_back({"mechanics",{"how does threat work"},"Threat attracts enemies.","test"});
    assert(catalogue.Lookup("how does threat work",answer));
    catalogue.knowledge.push_back({"ambiguous",{"how does threat work"},"Other answer.","test"});
    assert(!catalogue.Lookup("how does threat work",answer));
    assert(RequestContext(state).size()<=1200);
}
