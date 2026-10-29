static void TestBattleScripts(void) {
    uint32_t events[PHBattleEventCount], at = 99;
    for (unsigned i = 0; i < PHBattleEventCount; ++i) events[i] = UINT32_MAX;
    const uint8_t ordinary[] = {0, 13, 'p','l','a','y',' ','1',10, 19, 5,8,0,1,1,'O','K',0, 0};
    events[156] = 1;
    Check(PHBattleCheckScript(ordinary, sizeof(ordinary), events, &at) == PHBattleScriptCompatible,
          "event 156 and a harmless music/text script can share empty parameters");
    events[0] = 9; events[1] = 1; events[2] = 18;
    Check(PHBattleCheckScript(ordinary, sizeof(ordinary), events, NULL) == PHBattleScriptCompatible,
          "unsorted, duplicate and terminal event roots remain bounded");
    events[0] = 15;
    Check(PHBattleCheckScript(ordinary, sizeof(ordinary), events, &at) == PHBattleScriptMalformed && at == 15,
          "an event cannot enter the middle of text or a command argument");
    for (unsigned i = 0; i < PHBattleEventCount; ++i) events[i] = UINT32_MAX;
    const uint8_t deferred[] = {0, 26,'p','a','r','a','m',' ','1',' ','1',' ','2','0','0',10,0};
    events[42] = 1;
    Check(PHBattleCheckScript(deferred, sizeof(deferred), events, &at) == PHBattleScriptPartyCommand && at == 1,
          "a future party param command blocks leasing even when records are currently empty");
    const uint8_t text[] = {5,7,0,1,1,26,0,0};
    events[42] = UINT32_MAX;
    Check(PHBattleCheckScript(text, sizeof(text), events, &at) == PHBattleScriptCompatible,
          "party opcode value inside opaque text is not executable");
    uint8_t broken[sizeof(text)]; memcpy(broken, text, sizeof(text));
    broken[1] = 6;
    Check(PHBattleCheckScript(broken, sizeof(broken), events, NULL) == PHBattleScriptMalformed,
          "text must end at its declared zero terminator");
    broken[1] = 255;
    Check(PHBattleCheckScript(broken, sizeof(broken), events, NULL) == PHBattleScriptMalformed,
          "oversized text length cannot skip beyond the native allocation");
    const uint8_t hidden[] = {13,'p','l','a','y',26,'p','a','r','a','m',10,0};
    Check(PHBattleCheckScript(hidden, sizeof(hidden), events, NULL) == PHBattleScriptMalformed,
          "binary controls cannot hide a party command in directive arguments");
    const uint8_t unterminated[] = {13,'p','l','a','y'};
    Check(PHBattleCheckScript(unterminated, sizeof(unterminated), events, NULL) == PHBattleScriptMalformed,
          "unterminated directive cannot read beyond the supplied buffer");
    uint8_t opcode[] = {0,10,0};
    for (unsigned value = 27; value <= 255; ++value) {
        opcode[0] = (uint8_t)value;
        Check(PHBattleCheckScript(opcode, sizeof(opcode), events, NULL) == PHBattleScriptMalformed,
              "all unknown bytecodes fail closed");
    }
    events[0] = sizeof(text);
    Check(PHBattleCheckScript(text, sizeof(text), events, &at) == PHBattleScriptMalformed && at == sizeof(text),
          "one-past-end event is rejected before parsing");
    Check(PHBattleCheckScript(NULL, 1, events, NULL) == PHBattleScriptMalformed &&
          PHBattleCheckScript(text, 0, events, NULL) == PHBattleScriptMalformed &&
          PHBattleCheckScript(text, 1, NULL, NULL) == PHBattleScriptMalformed &&
          PHBattleCheckScript(text, 1024u * 1024u + 1, events, NULL) == PHBattleScriptMalformed,
          "null, empty and oversized buffers never become compatible scripts");
}
