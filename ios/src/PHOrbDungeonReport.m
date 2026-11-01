#import "PHOrbDungeonReport.h"
#include <CommonCrypto/CommonDigest.h>

NSString *PHOrbDungeonRecordKey(NSDictionary *record) {
    if (![NSJSONSerialization isValidJSONObject:record]) return nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:record options:NSJSONWritingSortedKeys error:NULL];
    if (!data || data.length > UINT32_MAX) return nil;
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.bytes,(CC_LONG)data.length,digest);
    NSMutableString *key = [NSMutableString stringWithCapacity:64];
    for (unsigned i = 0; i < sizeof(digest); ++i) [key appendFormat:@"%02x",digest[i]];
    return key;
}

NSDictionary *PHOrbExportDungeonReport(NSDictionary *report, NSString *version, NSString *requestID, NSURL *temporaryRoot) {
    NSMutableDictionary *result = [@{@"requestID":requestID ?: @"", @"ok":@NO} mutableCopy];
    if (![report[@"available"] boolValue] || ![report[@"kind"] isEqual:@"PADCheaper-dungeon-report"] ||
        ![NSJSONSerialization isValidJSONObject:report]) {
        result[@"message"] = @"尚未收集關卡資料；進入關卡後按「收集全層資料」。"; return result;
    }
    NSMutableDictionary *document = [report mutableCopy];
    document[@"dylibVersion"] = version ?: @"unknown";
    document[@"exportedAt"] = [[NSISO8601DateFormatter new] stringFromDate:NSDate.date];
    NSData *data = [NSJSONSerialization dataWithJSONObject:document
        options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:NULL];
    if (!data || data.length > 32u * 1024u * 1024u) {
        result[@"message"] = @"關卡資料無法轉成 JSON，或已超過 32 MiB 匯出上限。"; return result;
    }
    if (!temporaryRoot.isFileURL) {
        result[@"message"] = @"暫存資料夾不可用，請稍後再試。"; return result;
    }
    NSURL *directory = [temporaryRoot URLByAppendingPathComponent:
        [@"PADCheaper-dungeon-" stringByAppendingString:NSUUID.UUID.UUIDString] isDirectory:YES];
    NSURL *file = [directory URLByAppendingPathComponent:[NSString stringWithFormat:@"PADCheaper-dungeon-%u-%u.json",
        [report[@"dungeon"] unsignedIntValue], [report[@"floor"] unsignedIntValue]]];
    NSFileManager *manager = NSFileManager.defaultManager;
    if (![manager createDirectoryAtURL:directory withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:NULL] ||
        ![data writeToURL:file options:NSDataWritingAtomic error:NULL] ||
        ![manager setAttributes:@{NSFilePosixPermissions:@0600} ofItemAtPath:file.path error:NULL]) {
        [manager removeItemAtURL:directory error:NULL];
        result[@"message"] = @"無法建立關卡檔案，請檢查儲存空間。"; return result;
    }
    result[@"ok"] = @YES; result[@"filePath"] = file.path; result[@"directoryPath"] = directory.path;
    result[@"message"] = [report[@"allDefinitionsCaptured"] boolValue] ? @"全層資料已匯出。" : @"已匯出目前資料；缺少的樓層或行動已在檔案中標記。";
    return result;
}
