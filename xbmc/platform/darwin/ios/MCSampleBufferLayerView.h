#if !defined(TARGET_DARWIN_TVOS)

#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>

@interface MCSampleBufferLayerView : UIView

@property (nonatomic, strong) AVSampleBufferDisplayLayer *videoLayer;

- (void)setHiddenAnimated:(BOOL)hide
                    delay:(NSTimeInterval)delay
                 duration:(NSTimeInterval)duration;
@end
#endif