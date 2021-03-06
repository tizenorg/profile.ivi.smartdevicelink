//
// Copyright (c) 2013 Ford Motor Company
//

#import "SDLEnum.h"

@implementation SDLEnum

@synthesize value;

-(id) initWithValue:(NSString*) aValue {
	if (self = [super init]) {
		value = aValue;
	}
	return self;
}

-(NSString*) description {
	return value;
}

@end
