//
// Copyright (c) 2013 Ford Motor Company
//

#import <Foundation/Foundation.h>
#import "SDLEnum.h"

@interface SDLUpdateMode : SDLEnum {}

+(SDLUpdateMode*) valueOf:(NSString*) value;
+(NSMutableArray*) values;

+(SDLUpdateMode*) COUNTUP;
+(SDLUpdateMode*) COUNTDOWN;
+(SDLUpdateMode*) PAUSE;
+(SDLUpdateMode*) RESUME;

@end
