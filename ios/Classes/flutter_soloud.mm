#import "FlutterSoloudPlugin.h"
#import <AVFoundation/AVFoundation.h>
#include "../../src/flutter_soloud.cpp"

@implementation FlutterSoloudPlugin {
    NSNotificationCenter *_notificationCenter;
}

+ (void)registerWithRegistrar:(NSObject<FlutterPluginRegistrar>*)registrar {
    NSLog(@"############## REGISTERING PLUGIN\n");
    FlutterMethodChannel* channel = [FlutterMethodChannel
        methodChannelWithName:@"flutter_soloud"
              binaryMessenger:[registrar messenger]];
    FlutterSoloudPlugin* instance = [[FlutterSoloudPlugin alloc] init];
    instance.channel = channel;
    [registrar addMethodCallDelegate:instance channel:channel];
    NSLog(@"############## PLUGIN REGISTERED\n");
}

- (void)handleMethodCall:(FlutterMethodCall*)call result:(FlutterResult)result {
    NSLog(@"############## METHOD CALLED: %@", call.method);
    if ([@"simulateInterruption" isEqualToString:call.method]) {
        NSString *eventType = [call.arguments objectForKey:@"type"];
        NSMutableDictionary *userInfo = [NSMutableDictionary dictionary];
        
        if ([@"began" isEqualToString:eventType]) {
            [userInfo setObject:@(AVAudioSessionInterruptionTypeBegan) 
                       forKey:AVAudioSessionInterruptionTypeKey];
        } 
        else if ([@"ended" isEqualToString:eventType]) {
            [userInfo setObject:@(AVAudioSessionInterruptionTypeEnded) 
                       forKey:AVAudioSessionInterruptionTypeKey];
        }
        else if ([@"endedResume" isEqualToString:eventType]) {
            [userInfo setObject:@(AVAudioSessionInterruptionTypeEnded) 
                       forKey:AVAudioSessionInterruptionTypeKey];
            [userInfo setObject:@(AVAudioSessionInterruptionOptionShouldResume) 
                       forKey:AVAudioSessionInterruptionOptionKey];
        }
        
        // Post the notification to the system
        [[NSNotificationCenter defaultCenter]
            postNotificationName:AVAudioSessionInterruptionNotification
                        object:[AVAudioSession sharedInstance]
                      userInfo:userInfo];
        result(@YES);
    } else {
        result(FlutterMethodNotImplemented);
    }
}

@end
