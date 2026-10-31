#pragma once
#import <Foundation/Foundation.h>
#include "PHOrbModel.h"

static inline NSString *PHOrbSkillWindowDescription(const PHOrbAutomationState *state) {
    if (!state || !state->valid || !state->windowBlocksKnown) return @"技能窗口尚未確認";
    if (state->window) return @"技能窗口已開放";
    NSMutableArray<NSString *> *parts = [NSMutableArray array];
#define PH_WINDOW_REASON(flag, title) if (state->windowBlocks & (flag)) [parts addObject:title]
    PH_WINDOW_REASON(PHOrbSkillWindowScene, @"場景或操作尚未就緒");
    PH_WINDOW_REASON(PHOrbSkillWindowHP, @"目前 HP 為零");
    PH_WINDOW_REASON(PHOrbSkillWindowMemberAnimation, @"隊員動畫尚未完成");
    PH_WINDOW_REASON(PHOrbSkillWindowSeal, @"技能封印");
    PH_WINDOW_REASON(PHOrbSkillWindowSpecial, @"特殊技能操作尚未結束");
    PH_WINDOW_REASON(PHOrbSkillWindowMovement, @"仍在移動珠子");
    PH_WINDOW_REASON(PHOrbSkillWindowEnemyTurn, @"敵方回合尚未結束");
    PH_WINDOW_REASON(PHOrbSkillWindowInput, @"原生操作尚未結束");
    PH_WINDOW_REASON(PHOrbSkillWindowSpecialMode, @"特殊演出尚未結束");
    PH_WINDOW_REASON(PHOrbSkillWindowLimit, @"本層技能次數已用盡");
    PH_WINDOW_REASON(PHOrbSkillWindowTimedAnimation, @"計時演出尚未完成");
    PH_WINDOW_REASON(PHOrbSkillWindowTutorial, @"教學限制施放位置");
    PH_WINDOW_REASON(PHOrbSkillWindowDungeon, @"關卡禁止使用技能");
    PH_WINDOW_REASON(PHOrbSkillWindowEnemyParts, @"敵人部位資料尚未支援");
    PH_WINDOW_REASON(PHOrbSkillWindowEnemyAction, @"敵人行動尚未完成");
    PH_WINDOW_REASON(PHOrbSkillWindowNoEnemy, @"沒有可確認的存活敵人");
    PH_WINDOW_REASON(PHOrbSkillWindowResize, @"等待改盤動畫完成");
#undef PH_WINDOW_REASON
    return parts.count ? [NSString stringWithFormat:@"技能窗口關閉：%@", [parts componentsJoinedByString:@"、"]] :
        @"技能窗口關閉，具體條件尚未確認";
}
