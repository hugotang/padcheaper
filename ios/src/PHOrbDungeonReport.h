#import <Foundation/Foundation.h>

// File creation only; presenting or sending the file remains a separate UI action.
FOUNDATION_EXPORT NSDictionary *PHOrbExportDungeonReport(NSDictionary *report, NSString *version,
    NSString *requestID, NSURL *temporaryRoot);
FOUNDATION_EXPORT NSString *PHOrbDungeonRecordKey(NSDictionary *record);
