#import <Flutter/Flutter.h>

@interface FlutterSoloudPlugin : NSObject<FlutterPlugin>
@property (nonatomic, strong) FlutterMethodChannel* channel;
@end

