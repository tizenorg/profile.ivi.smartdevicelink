//
// Copyright (c) 2013 Ford Motor Company
//

#import <Foundation/Foundation.h>
#import "SDLRPCResponse.h"

#import "SDLResult.h"

@interface SDLSubscribeButtonResponse : SDLRPCResponse {}

-(id) init;
-(id) initWithDictionary:(NSMutableDictionary*) dict;

@end
