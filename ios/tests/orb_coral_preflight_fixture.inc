// Data construction only. Runtime checks below use the real import/session
// paths; changing a profile name or copying it cannot restore missing policy.
static NSMutableDictionary *CoralPreflightProfile(unsigned revision) {
    NSData *data=[NSData dataWithContentsOfFile:@"ios/templates/coral-fixed-gamewith-585607/fixed-plan.json"];
    NSMutableDictionary *package=data ? [NSJSONSerialization JSONObjectWithData:data
        options:NSJSONReadingMutableContainers error:NULL] : nil;
    NSMutableDictionary *profile=package[@"profile"];
    profile[@"id"]=[NSString stringWithFormat:@"coral-fixed-585607-v90-r%u",revision];
    if (revision<7) {
        NSMutableDictionary *stage=profile[@"fixedPlan"][@"floors"][4][@"variants"][0][@"stages"][3];
        [stage[@"board"] removeObjectForKey:@"requireSixColorFours"];
        [stage removeObjectForKey:@"requireEffectsClear"];
    }
    if (revision<6) for (NSMutableDictionary *stage in profile[@"fixedPlan"][@"floors"][3][@"variants"][0][@"stages"]) {
        [stage[@"board"] removeObjectForKey:@"requireSixColorFours"];
        [stage removeObjectForKey:@"requireEffectsClear"];
        [stage removeObjectForKey:@"requireEnemyState"];
        NSMutableArray *skills=[NSMutableArray array];
        for (NSDictionary *action in stage[@"actions"]) if ([action[@"kind"] isEqual:@"skill"]) [skills addObject:action];
        stage[@"actions"]=skills;
    }
    return profile;
}
