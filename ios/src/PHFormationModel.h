#import <Foundation/Foundation.h>
#include "PHBattlePreset.h"

FOUNDATION_EXPORT BOOL PHFormationReadTeam(id value, PHBattleLoadout loadouts[6], NSString **reason);
FOUNDATION_EXPORT BOOL PHFormationReadApply(id value, PHBattleLoadout loadouts[6], uint8_t *mask, NSString **reason);
FOUNDATION_EXPORT BOOL PHFormationReadRestoreSlot(id value, NSUInteger *slot);

enum { PHFormationLibraryCapacity = 64 };
FOUNDATION_EXPORT BOOL PHFormationReadLibrary(id value, NSDictionary **selectedTeam, NSString **reason);
FOUNDATION_EXPORT NSDictionary *PHFormationLibraryFromTeam(NSDictionary *team);
FOUNDATION_EXPORT NSDictionary *PHFormationLibraryUpdatingTeam(NSDictionary *library, NSDictionary *team);
