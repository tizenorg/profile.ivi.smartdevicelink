//
// Copyright (c) 2013 Ford Motor Company
//

#import <Foundation/Foundation.h>
#import "SDLEnum.h"

@interface SDLDisplayType : SDLEnum {}

+(SDLDisplayType*) valueOf:(NSString*) value;
+(NSMutableArray*) values;

+(SDLDisplayType*) CID;
+(SDLDisplayType*) TYPE2;
+(SDLDisplayType*) TYPE5;
+(SDLDisplayType*) NGN;
+(SDLDisplayType*) GEN2_4_DMA;
+(SDLDisplayType*) GEN2_8_DMA;
+(SDLDisplayType*) GEN2_4_HUD;

@end
