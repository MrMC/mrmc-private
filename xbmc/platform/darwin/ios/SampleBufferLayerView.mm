/*
 *      Copyright (C) 2015 Team MrMC
 *      https://github.com/MrMC
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with MrMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#import "platform/darwin/ios/SampleBufferLayerView.h"

#define MCSAMPLEBUFFER_DEBUG_MESSAGES 0

#pragma mark -
@implementation SampleBufferLayerView

- (id)initWithFrame:(CGRect)frame
{
	self = [super initWithFrame:frame];
	if (self)
	{
    [self setNeedsLayout];
    [self layoutIfNeeded];

    self.hidden = YES;
		self.videoLayer = [[[AVSampleBufferDisplayLayer alloc] init] autorelease];
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

    // create a time base
    CMTimebaseRef controlTimebase;
    CMTimebaseCreateWithMasterClock( CFAllocatorGetDefault(), CMClockGetHostTimeClock(), &controlTimebase );

    // setup the time base clock stopped with a zero initial time.
    self.videoLayer.controlTimebase = controlTimebase;
    CMTimebaseSetTime(self.videoLayer.controlTimebase, kCMTimeZero);
    CMTimebaseSetRate(self.videoLayer.controlTimebase, 0);

#if MCSAMPLEBUFFER_DEBUG_MESSAGES
    [self.videoLayer addObserver:self forKeyPath:@"error" options:NSKeyValueObservingOptionNew context:nullptr];
    [self.videoLayer addObserver:self forKeyPath:@"outputObscuredDueToInsufficientExternalProtection" options:NSKeyValueObservingOptionNew context:nullptr];

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(layerFailedToDecode:) name:AVSampleBufferDisplayLayerFailedToDecodeNotification object:self.videoLayer];
#endif
  }

	return self;
}

- (void)dealloc
{
#if MCSAMPLEBUFFER_DEBUG_MESSAGES
  [self.videoLayer removeObserver:self forKeyPath:@"error"];
  [self.videoLayer removeObserver:self forKeyPath:@"outputObscuredDueToInsufficientExternalProtection"];

  [[NSNotificationCenter defaultCenter] removeObserver:self name:AVSampleBufferDisplayLayerFailedToDecodeNotification object:self.videoLayer];
#endif
  // humm, why do I need to do these releases ?
  [self.videoLayer removeFromSuperlayer];
  [self.videoLayer release];
  [super dealloc];
}

#if MCSAMPLEBUFFER_DEBUG_MESSAGES
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
#endif

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
