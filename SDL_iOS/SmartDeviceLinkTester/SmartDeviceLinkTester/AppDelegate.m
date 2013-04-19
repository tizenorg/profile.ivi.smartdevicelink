//
// Copyright (c) 2013 Ford Motor Company
//

#import "AppDelegate.h"


@implementation AppDelegate

@synthesize window = _window;
@synthesize tabBarController = _tabBarController;


- (void)dealloc
{
    [_window release];
    [_tabBarController release];
    [super dealloc];
}

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    self.window = [[[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]] autorelease];
    // Override point for customization after application launch.
    userTestViewController = [[[UserTestViewController alloc] initWithNibName:@"UserTestViewController" bundle:nil] autorelease];
    UIViewController *rpcTestViewController = [[RPCTestViewController alloc] initWithStyle:UITableViewStyleGrouped];
    rpcTestViewController.view.backgroundColor=[UIColor colorWithRed:(255/255.) green:(195/255.) blue:(135/255.) alpha:1];
    autoTestViewController = [[[AutoTestViewController alloc] initWithNibName:@"AutoTestViewController" bundle:nil] autorelease];
    consoleViewController = [[[ConsoleViewController alloc] initWithNibName:@"ConsoleViewController" bundle:nil] autorelease];

    navController = nil;
    navController = [[UINavigationController alloc] initWithRootViewController:rpcTestViewController];

    [rpcTestViewController release];
    
    self.tabBarController = [[[UITabBarController alloc] init] autorelease];
    self.tabBarController.viewControllers = [NSArray arrayWithObjects:userTestViewController, navController, autoTestViewController,consoleViewController, nil];
    self.tabBarController.selectedIndex = 3;
    self.window.rootViewController = self.tabBarController;
    [self.window makeKeyAndVisible];
    
    [[SDLBrain getInstance] setupProxy];
    
    NSURL *url = (NSURL *)[launchOptions valueForKey:UIApplicationLaunchOptionsURLKey];
    if (url != nil && [url isFileURL]) {
        [autoTestViewController handleOpenURL:url];
    }
    
    [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback error:nil];
    
    
    return YES;
}

-(BOOL) application:(UIApplication *)application openURL:(NSURL *)url sourceApplication:(NSString *)sourceApplication annotation:(id)annotation {
    if (url!= nil && [url isFileURL]) {
            [autoTestViewController handleOpenURL:url];    
    }
    return YES;
}

- (void)applicationWillResignActive:(UIApplication *)application
{
    // Sent when the application is about to move from active to inactive state. This can occur for certain types of temporary interruptions (such as an incoming phone call or SMS message) or when the user quits the application and it begins the transition to the background state.
    // Use this method to pause ongoing tasks, disable timers, and throttle down OpenGL ES frame rates. Games should use this method to pause the game.
}

- (void)applicationDidEnterBackground:(UIApplication *)application
{
    [SDLDebugTool logInfo:@"App did enter background"];

    // Use this method to release shared resources, save user data, invalidate timers, and store enough application state information to restore your application to its current state in case it is terminated later. 
    // If your application supports background execution, this method is called instead of applicationWillTerminate: when the user quits.
}

- (void)applicationWillEnterForeground:(UIApplication *)application
{
    // Called as part of the transition from the background to the inactive state; here you can undo many of the changes made on entering the background.
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
    // Restart any tasks that were paused (or not yet started) while the application was inactive. If the application was previously in the background, optionally refresh the user interface.
}

- (void)applicationWillTerminate:(UIApplication *)application
{
    [SDLDebugTool logInfo:@"will terminate"];
    
    [navController release];
    navController = nil;

    NSLog(@"did terminate");

    // Called when the application is about to terminate. Save data if appropriate. See also applicationDidEnterBackground:.
}

/*
// Optional UITabBarControllerDelegate method.
- (void)tabBarController:(UITabBarController *)tabBarController didSelectViewController:(UIViewController *)viewController
{
}
*/

/*
// Optional UITabBarControllerDelegate method.
- (void)tabBarController:(UITabBarController *)tabBarController didEndCustomizingViewControllers:(NSArray *)viewControllers changed:(BOOL)changed
{
}
*/

@end
