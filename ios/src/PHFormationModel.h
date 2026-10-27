#import <Foundation/Foundation.h>
#include "PHBattlePreset.h"

FOUNDATION_EXPORT BOOL PHFormationReadTeam(id value, PHBattleLoadout loadouts[6], NSString **reason);
FOUNDATION_EXPORT BOOL PHFormationReadApply(id value, PHBattleLoadout loadouts[6], uint8_t *mask, NSString **reason);
FOUNDATION_EXPORT BOOL PHFormationReadRestoreSlot(id value, NSUInteger *slot);
