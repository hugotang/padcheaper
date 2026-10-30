#import "PHUnlockRestoration.h"
#import "PHMemory.h"
#include <stdio.h>
#include <string.h>

static uint8_t bitmap[128], descriptors[104];
static const uintptr_t Bitmap=0x10000, Table=0x20000;
static unsigned failures, writes;
static BOOL failRead, failWrite, ignoreWrite, failReadback;
static void Check(BOOL ok,const char *why) { if (!ok) { fprintf(stderr,"FAIL: %s\n",why); ++failures; } }
static void *Resolve(uintptr_t address,size_t length) {
    if (address>=Bitmap && address-Bitmap<=sizeof(bitmap) && length<=sizeof(bitmap)-(address-Bitmap)) return bitmap+address-Bitmap;
    if (address>=Table && address-Table<=sizeof(descriptors) && length<=sizeof(descriptors)-(address-Table)) return descriptors+address-Table;
    return NULL;
}
BOOL PHReadMemory(uintptr_t address,void *output,size_t length) {
    void *p=Resolve(address,length); if (!p||failRead||(failReadback&&writes)) return NO;
    memcpy(output,p,length); return YES;
}
BOOL PHWriteData(uintptr_t address,const void *input,size_t length) {
    ++writes; void *p=Resolve(address,length); if (!p||failWrite) return NO;
    if (!ignoreWrite) memcpy(p,input,length); return YES;
}
static void Seed(void) {
    memset(bitmap,0,sizeof(bitmap)); memset(descriptors,0,sizeof(descriptors));
    failRead=failWrite=ignoreWrite=failReadback=NO; writes=0;
    uint16_t identifier=10001, flags=3;
    memcpy(descriptors,&identifier,2); memcpy(descriptors+14,&flags,2);
}
static NSArray *Patches(void) {
    return @[@{@"address":@(Table+14),@"table":@(Table),@"id":@10001,@"original":@2,@"feature":@(PHFeatureBGM)}];
}
int main(void) { @autoreleasepool {
    Seed(); uint8_t added[128]={1}; bitmap[0]=5;
    failWrite=YES; Check(!PHRestoreOwnershipBits(Bitmap,added)&&added[0]==1&&bitmap[0]==5,"failed bitmap write retains its restoration bits");
    failWrite=NO; Check(PHRestoreOwnershipBits(Bitmap,added)&&!added[0]&&bitmap[0]==4,"retry removes only the helper bit and preserves unrelated ownership");
    unsigned before=writes; Check(PHRestoreOwnershipBits(Bitmap,added)&&writes==before,"completed bitmap restoration performs no further writes");
    Seed(); added[0]=1; bitmap[0]=1; failRead=YES;
    Check(!PHRestoreOwnershipBits(Bitmap,added)&&added[0]&&!writes,"transient read failure cannot discard ownership bits");
    failRead=NO; ignoreWrite=YES;
    Check(!PHRestoreOwnershipBits(Bitmap,added)&&added[0],"acknowledged but ineffective bitmap write remains retryable");
    ignoreWrite=NO; failReadback=YES;
    Check(!PHRestoreOwnershipBits(Bitmap,added)&&added[0],"unconfirmed bitmap readback retains ownership");
    failReadback=NO; Check(PHRestoreOwnershipBits(Bitmap,added)&&!added[0],"later read can confirm native or previous removal without erasing other bits");
    Seed(); BOOL fault=NO; NSArray *patches=Patches();
    failWrite=YES; patches=PHRestoreDescriptorFlags(patches,Table,2,YES,0,&fault);
    Check(fault&&patches.count==1&&descriptors[14]==3,"failed descriptor restoration retains the lease");
    failWrite=NO; fault=NO; patches=PHRestoreDescriptorFlags(patches,Table,2,YES,0,&fault);
    Check(!fault&&!patches.count&&descriptors[14]==2,"descriptor retry restores the original flags");
    Seed(); fault=NO; patches=PHRestoreDescriptorFlags(Patches(),0,0,NO,0,&fault);
    Check(fault&&patches.count==1&&!writes,"loading descriptor table does not lose restoration work");
    failRead=YES; fault=NO; patches=PHRestoreDescriptorFlags(patches,Table,2,YES,PHFeatureBGM,&fault);
    Check(fault&&patches.count==1,"unreadable descriptor keeps a lease while enabled");
    failRead=NO; fault=NO; descriptors[14]=7;
    Check(!PHRestoreDescriptorFlags(patches,Table,2,YES,PHFeatureBGM,&fault).count&&!writes,"native flag changes revoke the old lease before the next disable");
    Seed(); descriptors[0]=9; fault=NO;
    Check(!PHRestoreDescriptorFlags(Patches(),Table,2,YES,PHFeatureBGM,&fault).count&&!writes,"same allocation with another descriptor cannot inherit a lease");
    Seed(); Check(!PHRestoreDescriptorFlags(Patches(),Table+52,1,YES,0,&fault).count&&!writes,"verified replacement table discards obsolete addresses without restoring them");
    Seed(); ignoreWrite=YES; fault=NO;
    Check(PHRestoreDescriptorFlags(Patches(),Table,2,YES,0,&fault).count==1&&fault,"descriptor readback is required before dropping work");
    printf("Unlock restoration tests: %u failures\n",failures);
} return failures?1:0; }
