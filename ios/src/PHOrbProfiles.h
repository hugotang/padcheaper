#import <Foundation/Foundation.h>
#include "PHOrbModel.h"

enum { PHOrbMaxProfiles = 64 };

FOUNDATION_EXPORT NSDictionary *PHOrbDefaultProfiles(void);
FOUNDATION_EXPORT NSDictionary *PHOrbCanonicalSettings(id settings);
FOUNDATION_EXPORT BOOL PHOrbProfilesValid(id library);
FOUNDATION_EXPORT NSDictionary *PHOrbProfileNamed(NSDictionary *library, NSString *identifier);
FOUNDATION_EXPORT NSDictionary *PHOrbProfilesUpdatingSettings(NSDictionary *library, NSDictionary *settings);
// Resolve once at continuous start. The returned record is held for this run;
// transformed members and later floors must not silently select a new profile.
FOUNDATION_EXPORT NSDictionary *PHOrbResolveProfile(NSDictionary *library, const PHOrbSnapshot *state);

// A portable, versioned team + ordered opening + loop + board-goal package.
FOUNDATION_EXPORT BOOL PHOrbOpeningValid(id opening, NSDictionary *formation);
FOUNDATION_EXPORT NSDictionary *PHOrbProfileFromPlan(id plan, NSString **reason);
FOUNDATION_EXPORT NSDictionary *PHOrbPlanFromProfile(NSDictionary *profile);
FOUNDATION_EXPORT NSDictionary *PHOrbProfilesImportingPlan(NSDictionary *library, id plan, NSString **reason);
// Only training/title edits are compatible with an existing full plan. Bodies,
// assists and the ordered opening must retain their reviewed identities.
FOUNDATION_EXPORT NSDictionary *PHOrbProfileUpdatingFormation(NSDictionary *profile, NSDictionary *formation);
FOUNDATION_EXPORT NSDictionary *PHOrbFormationsSelectingProfile(NSDictionary *formations, NSDictionary *profile);
FOUNDATION_EXPORT BOOL PHOrbProfileMatchesOpeningTeam(NSDictionary *profile, const PHOrbSnapshot *state, NSUInteger completed);
FOUNDATION_EXPORT BOOL PHOrbProfileMatchesRunningTeam(NSDictionary *profile, const PHOrbSnapshot *state);
// First opening cursor k > 0 whose step k carries an afterMonster form that the
// current team already shows (a manual transform), or 0 when none applies.
FOUNDATION_EXPORT NSUInteger PHOrbProfileManualOpeningProgress(NSDictionary *profile, const PHOrbSnapshot *state);
FOUNDATION_EXPORT NSString *PHOrbPlanDescription(NSDictionary *plan);
// Single selected profile only; never account state, endpoint or master switches.
FOUNDATION_EXPORT NSData *PHOrbExportProfileData(NSDictionary *profile, NSString **reason);
FOUNDATION_EXPORT NSDictionary *PHOrbProfilesImportingData(NSDictionary *library, NSData *data, NSString **reason);
