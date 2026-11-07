#import <Foundation/Foundation.h>
#include "PHBattlePreset.h"

FOUNDATION_EXPORT BOOL PHFormationReadTeam(id value, PHBattleLoadout loadouts[6], NSString **reason);
// Stored PADDashFormation selection (including synthetic PAD Pass 129), not a
// native badge ID. Never pass this value directly to a native write. Missing
// retains the game selection; explicit 0 means no badge. Invalid inputs leave
// outputs untouched.
FOUNDATION_EXPORT BOOL PHFormationReadBadge(id team, BOOL *specified, uint8_t *badge);
FOUNDATION_EXPORT BOOL PHFormationReadApply(id value, PHBattleLoadout loadouts[6], uint8_t *mask, NSString **reason);
FOUNDATION_EXPORT BOOL PHFormationReadRestoreSlot(id value, NSUInteger *slot);

enum { PHFormationLibraryCapacity = 64 };
FOUNDATION_EXPORT BOOL PHFormationReadLibrary(id value, NSDictionary **selectedTeam, NSString **reason);
FOUNDATION_EXPORT NSDictionary *PHFormationLibraryFromTeam(NSDictionary *team);
FOUNDATION_EXPORT NSDictionary *PHFormationLibraryUpdatingTeam(NSDictionary *library, NSDictionary *team);
FOUNDATION_EXPORT NSDictionary *PHFormationLibraryTeam(NSDictionary *library, NSString *identifier);
FOUNDATION_EXPORT NSString *PHFormationLibraryTeamLabel(NSDictionary *library, NSString *identifier);
