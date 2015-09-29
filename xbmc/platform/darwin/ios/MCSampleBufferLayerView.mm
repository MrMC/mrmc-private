
#import "platform/darwin/ios/MCSampleBufferLayerView.h"

#pragma mark -
@implementation MCSampleBufferLayerView

- (id)initWithFrame:(CGRect)frame
{
	self = [super initWithFrame:frame];
	if (self)
	{
    [self setNeedsLayout];
    [self layoutIfNeeded];

    self.hidden = YES;
		self.videoLayer = [[AVSampleBufferDisplayLayer alloc] init];
		self.videoLayer.bounds = self.bounds;
		self.videoLayer.position = CGPointMake(CGRectGetMidX(self.bounds), CGRectGetMidY(self.bounds));
		self.videoLayer.videoGravity = AVLayerVideoGravityResizeAspect;
#if defined(TARGET_DARWIN_IOS)
		self.videoLayer.backgroundColor = [[UIColor blackColor] CGColor];
		//self.videoLayer.backgroundColor = [[UIColor clearColor] CGColor];
#else
    self.videoLayer.backgroundColor = CGColorGetConstantColor(kCGColorBlue);
#endif
    [[self layer] addSublayer:self.videoLayer];

    // Set Timebase
    CMTimebaseRef controlTimebase;
    CMTimebaseCreateWithMasterClock( CFAllocatorGetDefault(), CMClockGetHostTimeClock(), &controlTimebase );
    
    self.videoLayer.controlTimebase = controlTimebase;
    CMTimebaseSetTime(self.videoLayer.controlTimebase, kCMTimeZero);
    CMTimebaseSetRate(self.videoLayer.controlTimebase, 1.0);

    //[self.videoLayer addObserver:self forKeyPath:@"error" options:NSKeyValueObservingOptionNew context:nullptr];
    //[self.videoLayer addObserver:self forKeyPath:@"outputObscuredDueToInsufficientExternalProtection" options:NSKeyValueObservingOptionNew context:nullptr];

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(layerFailedToDecode:) name:AVSampleBufferDisplayLayerFailedToDecodeNotification object:self.videoLayer];
  }

	return self;
}

- (void)dealloc
{
  //[self.videoLayer removeObserver:self forKeyPath:@"error"];
  //[self.videoLayer removeObserver:self forKeyPath:@"outputObscuredDueToInsufficientExternalProtection"];

  [[NSNotificationCenter defaultCenter] removeObserver:self name:AVSampleBufferDisplayLayerFailedToDecodeNotification object:self.videoLayer];
	self.videoLayer = nullptr;
  [super dealloc];
}

- (void)layerFailedToDecode:(NSNotification*)note
{
  static int toggle = 0;
  //AVSampleBufferDisplayLayer *layer = (AVSampleBufferDisplayLayer *)[note object];
  NSError *error = [[note userInfo] valueForKey:AVSampleBufferDisplayLayerFailedToDecodeNotificationErrorKey];
  NSLog(@"Error: %@", error);
  if (toggle & 0x01)
    self.videoLayer.backgroundColor = [[UIColor redColor] CGColor];
  else
    self.videoLayer.backgroundColor = [[UIColor greenColor] CGColor];
  toggle++;

}

- (void)setHiddenAnimated:(BOOL)hide
  delay:(NSTimeInterval)delay duration:(NSTimeInterval)duration
{
    [UIView animateWithDuration:duration delay:delay
        options:UIViewAnimationOptionAllowAnimatedContent animations:^{
         if (hide) {
             self.alpha = 0;
         } else {
             self.alpha = 0;
             self.hidden = NO; // We need this to see the animation 0 -> 1
             self.alpha = 1;
         }
      } completion:^(BOOL finished) {
          self.hidden = hide;
    }];
}

@end
