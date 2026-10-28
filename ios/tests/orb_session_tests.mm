#import "PHOrbSession.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

static unsigned failures;
static void Check(bool ok, const char *message) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; } }

@interface FakeOrbEngine : NSObject <PHOrbEngine> {
@public
    PHOrbSnapshot live;
    NSUInteger stages, releases, cell;
    BOOL down, beginning, staged, ignoreStart, ignoreMove, failRestore, corruptSwap;
}
- (void)nativeFrame;
@end
@implementation FakeOrbEngine
- (instancetype)init {
    if ((self = [super init])) {
        live = {}; live.identity = {1,2,3,42,1,1,2,{1},0,0,0};
        live.inputManager = 4; live.present = live.valid = live.settled = live.ordinary = true;
        live.phase = 2; live.input.valid = true; live.minErase = 3; live.freeMovement = 1;
        live.remainingFrames = 300; live.fps = 60; live.board.width = 6; live.board.height = 5;
        uint32_t random = 731;
        for (unsigned i = 0; i < 30; ++i) {
            random = random * 1664525u + 1013904223u;
            live.board.types[i] = uint8_t((random >> 12) % 6); live.board.tokens[i] = 100 + i;
            live.board.x[i] = int16_t(50 + i % 6 * 100); live.board.y[i] = int16_t(400 + i / 6 * 100);
        }
    }
    return self;
}
- (BOOL)read:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (!frame.ready || staged) return NO;
    *state = live; return YES;
}
- (BOOL)stageCell:(NSUInteger)target down:(BOOL)hold beginning:(BOOL)begin
    expected:(const PHOrbSnapshot *)expected frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (staged || !frame.ready || (hold && !frame.enabled) || live.multiplayerDisabled ||
        !PHOrbSameTurn(&live, expected) || !PHOrbSameBoard(&live.board, &expected->board) ||
        (hold && !PHInputIsQuiet(&live.input)) || target >= 30) return NO;
    ++stages; cell = target; down = hold; beginning = begin; staged = YES;
    if (!down) ++releases;
    return YES;
}
- (BOOL)finishInput:(NSString **)reason {
    if (!staged) return YES;
    if (failRestore) { failRestore = NO; if (reason) *reason = @"fixture restoration failure"; return NO; }
    staged = NO; return YES;
}
- (void)nativeFrame {
    if (!staged) return;
    if (!down) { live.dragged = 0; live.settled = true; live.phase = 3; return; }
    if (beginning) {
        if (!ignoreStart) { live.dragged = live.board.tokens[cell]; live.settled = false; }
        return;
    }
    if (ignoreMove) return;
    unsigned from = 0;
    while (from < 30 && live.board.tokens[from] != live.dragged) ++from;
    if (from == cell || from == 30) return;
    Check(PHOrbAdjacent(from, unsigned(cell), 6, 5), "executor never skips to a nonadjacent cell");
    std::swap(live.board.tokens[from], live.board.tokens[cell]); std::swap(live.board.types[from], live.board.types[cell]);
    std::swap(live.board.flags[from], live.board.flags[cell]); std::swap(live.board.enhancement[from], live.board.enhancement[cell]);
    ++live.moves;
    if (corruptSwap) live.board.types[29] = uint8_t((live.board.types[29] + 1) % 6);
}
@end

static double now = 100;
static PHBattleFrame Frame() { PHBattleFrame frame{}; frame.header = 1; frame.player = 42; frame.epoch = 1; frame.ready = frame.enabled = true; return frame; }
static void Tick(PHOrbSession *session, FakeOrbEngine *engine, PHBattleFrame frame) {
    now += 0.02;
    [session beforeFrame:frame time:now];
    @try { [engine nativeFrame]; } @finally { [session finishNativeFrame]; }
    [session afterFrame:frame time:now];
    [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.001]];
}
static bool Preview(PHOrbSession *session, FakeOrbEngine *engine) {
    Tick(session, engine, Frame()); [session queueRead];
    for (unsigned i = 0; i < 1000; ++i) {
        Tick(session, engine, Frame());
        if ([[session snapshot][@"phase"] isEqual:@"preview"]) return true;
        if ([[session snapshot][@"phase"] isEqual:@"blocked"] || [[session snapshot][@"phase"] isEqual:@"fault"]) return false;
    }
    return false;
}
static void Execute(PHOrbSession *session) { [session queueExecute:[[session snapshot][@"generation"] unsignedIntegerValue]]; }
static void UntilDragging(PHOrbSession *session, FakeOrbEngine *engine) {
    for (unsigned i = 0; i < 100 && !engine->live.dragged; ++i) Tick(session, engine, Frame());
    Check(engine->live.dragged != 0, "native accepted start fixture");
}

int main() {
    @autoreleasepool {
        FakeOrbEngine *engine = [FakeOrbEngine new]; PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "read -> background solve -> preview");
        Check(engine->stages == 0, "preview never stages input");
        auto generation = [[session snapshot][@"generation"] unsignedIntegerValue];
        [session queueExecute:generation + 1];
        Check([[session snapshot][@"phase"] isEqual:@"preview"], "stale UI generation cannot execute a different preview");
        Execute(session);
        for (unsigned i = 0; i < 250 && [[session snapshot][@"busy"] boolValue]; ++i) Tick(session, engine, Frame());
        Check([[session snapshot][@"phase"] isEqual:@"complete"] && engine->releases == 1 && !engine->live.dragged,
              "each swap acknowledged, explicit release and completed-once");
        NSUInteger stages = engine->stages;
        for (unsigned i = 0; i < 20; ++i) Tick(session, engine, Frame());
        Check(engine->stages == stages && [[session snapshot][@"completed"] unsignedIntegerValue] == 1, "no silent next-turn autoplay");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "manual-interruption setup"); Execute(session); UntilDragging(session, engine);
        engine->live.input.rawActiveMask = 1;
        for (unsigned i = 0; i < 10; ++i) Tick(session, engine, Frame());
        Check(!engine->live.dragged && engine->releases == 1 && engine->live.input.rawActiveMask == 1 &&
            [[session snapshot][@"completed"] unsignedIntegerValue] == 0, "manual input cancels and remains intact after own release");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "stale-board setup");
        std::swap(engine->live.board.tokens[0], engine->live.board.tokens[1]);
        for (unsigned i = 0; i < 8; ++i) Tick(session, engine, Frame());
        Execute(session); Check(engine->stages == 0 && ![[session snapshot][@"canExecute"] boolValue], "changed token ordering invalidates preview");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "multi transition setup"); Execute(session); UntilDragging(session, engine);
        stages = engine->stages; engine->live.multiplayerDisabled = true; engine->live.identity.multiplayer = 1;
        Tick(session, engine, Frame());
        Check(engine->stages == stages && ![[session snapshot][@"busy"] boolValue], "multiplayer transition never receives cleanup input");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "ignored native move setup"); Execute(session); UntilDragging(session, engine); engine->ignoreMove = YES;
        for (unsigned i = 0; i < 100 && [[session snapshot][@"busy"] boolValue]; ++i) Tick(session, engine, Frame());
        Check(engine->releases == 1 && engine->live.moves == 0 && [[session snapshot][@"completed"] unsignedIntegerValue] == 0,
              "unacknowledged movement times out instead of skipping steps");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "corrupted native swap setup"); Execute(session); UntilDragging(session, engine); engine->corruptSwap = YES;
        for (unsigned i = 0; i < 100 && [[session snapshot][@"busy"] boolValue]; ++i) Tick(session, engine, Frame());
        Check(engine->releases == 1 && [[session snapshot][@"completed"] unsignedIntegerValue] == 0, "mismatched board stops and releases at actual cell");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "background setup"); Execute(session); UntilDragging(session, engine);
        stages = engine->stages; [session queueStop:@"background"]; PHBattleFrame background = Frame(); background.ready = false;
        Tick(session, engine, background); engine->live.dragged = 0;
        for (unsigned i = 0; i < 20; ++i) Tick(session, engine, Frame());
        Check(engine->stages == stages, "background cancellation does not resume path");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "restore failure setup"); Execute(session); engine->failRestore = YES;
        for (unsigned i = 0; i < 35; ++i) Tick(session, engine, Frame());
        stages = engine->stages;
        Check([[session snapshot][@"inputFault"] boolValue], "input restoration failure is visible");
        [session queueRead]; for (unsigned i = 0; i < 10; ++i) Tick(session, engine, Frame());
        Check(engine->stages == stages && ![[session snapshot][@"canExecute"] boolValue], "restoration fault prevents further injection");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame()); [session queueRead];
        for (unsigned i = 0; i < 20; ++i) Tick(session, engine, Frame());
        [session queueStop:@"cancel worker"];
        for (unsigned i = 0; i < 400; ++i) Tick(session, engine, Frame());
        Check([[session snapshot][@"phase"] isEqual:@"stopped"] && engine->stages == 0, "late solver completion cannot revive canceled intent");
        std::printf("Orb session tests: %s (preview, execute once, manual/background/multi, stale results, acknowledgment and faults)\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
