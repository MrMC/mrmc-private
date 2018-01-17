/*
 *      Copyright (C) 2018 Team MrMC
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

#import "platform/darwin/tvos/FocusLayerViewSlider.h"

#import "Application.h"
#import "FileItem.h"
#import "messaging/ApplicationMessenger.h"
#import "platform/darwin/NSLogDebugHelpers.h"
#import "platform/darwin/tvos/ProgressThumbNailer.h"
#import "guilib/GUISliderControl.h"
#import "utils/MathUtils.h"
#import "utils/log.h"

#pragma mark -
@implementation FocusLayerViewSlider

- (id)initWithFrame:(CGRect)frame
{
  displayRect = CGRectInset([UIScreen mainScreen].bounds, 0, 100);

  barRect = frame;
  // standard 16:9 video rect
  videoRect = CGRectMake(0, 0, 400, 225);

  CGRect screenRect = [UIScreen mainScreen].bounds;
  // see if we are in upper or lower screen area
  // we need to expand the view to include
  // drawing room for line going from thumb position
  // to thumbnail pict and thumbnail pict area
  if (barRect.origin.y > screenRect.size.height/2)
  {
    // if in lower area, expand up
    frame.origin.y -= videoRect.size.height + 2;
    frame.size.height += videoRect.size.height + 2;
    videoRectIsAboveBar = true;
  }
  else
  {
    // if in upper area, expand down
    frame.size.height += videoRect.size.height + 2;
    videoRectIsAboveBar = false;
  }

	self = [super initWithFrame:frame];
	if (self)
	{
    self._value = 0.0;

    self->min = 0.0;
    self->max = 100.0;
    self->thumb = 0.0;
    self->thumbConstant = 0.0;
    self->distance = 100;
    self->decelerationRate = 0.84;
    self->decelerationMaxVelocity = 1000;
    float percentage = 0.0;
    self->thumbImage = nullptr;
    self->thumbNailer = nullptr;
    if (g_application.m_pPlayer->IsPlayingVideo())
    {
      // get percentage from application, includes stacks
      double seekTime = g_application.GetTime();
      double totalTime = g_application.GetTotalTime();
      percentage = seekTime / totalTime;
      self->thumbNailer = new CProgressThumbNailer(g_application.CurrentFileItem());
    }
    // initial slider position
    [self set:percentage];

    auto pan = [[UIPanGestureRecognizer alloc]
      initWithTarget:self action:@selector(handlePanGesture:)];
    pan.delegate = self;
    [self addGestureRecognizer:pan];

    auto tapUpRecognizer = [[UITapGestureRecognizer alloc]
      initWithTarget: self action: @selector(handleUpTapGesture:)];
    tapUpRecognizer.allowedPressTypes  = @[[NSNumber numberWithInteger:UIPressTypeUpArrow]];
    tapUpRecognizer.delegate  = self;
    [self addGestureRecognizer:tapUpRecognizer];

    auto tapDownRecognizer = [[UITapGestureRecognizer alloc]
      initWithTarget: self action: @selector(handleDownTapGesture:)];
    tapDownRecognizer.allowedPressTypes  = @[[NSNumber numberWithInteger:UIPressTypeDownArrow]];
    tapDownRecognizer.delegate  = self;
    [self addGestureRecognizer:tapDownRecognizer];

    auto tapLeftRecognizer = [[UITapGestureRecognizer alloc]
      initWithTarget: self action: @selector(handleLeftTapGesture:)];
    tapLeftRecognizer.allowedPressTypes  = @[[NSNumber numberWithInteger:UIPressTypeLeftArrow]];
    tapLeftRecognizer.delegate  = self;
    [self addGestureRecognizer:tapLeftRecognizer];

    auto tapRightRecognizer = [[UITapGestureRecognizer alloc]
      initWithTarget: self action: @selector(handleRightTapGesture:)];
    tapRightRecognizer.allowedPressTypes  = @[[NSNumber numberWithInteger:UIPressTypeRightArrow]];
    tapRightRecognizer.delegate  = self;
    [self addGestureRecognizer:tapRightRecognizer];
  }
	return self;
}

- (void)dealloc
{
  SAFE_DELETE(self->thumbNailer);
  CGImageRelease(self->thumbImage);
}

- (double)value
{
  return self._value;
}

- (void)setValue:(double)newValue
{
  self._value = newValue;
  [self updateViews:nil];
}

- (void)set:(double)percentage
{
  self.value = distance * (double)(percentage > 1 ? 1 : (percentage < 0 ? 0 : percentage)) + min;
  CLog::Log(LOGDEBUG, "Slider::set percentage(%f), value(%f)", percentage, self.value);
  if (self->thumbNailer)
    self->thumbNailer->RequestThumbAsPercentage(100.0 * percentage);
}

- (double) getSeekTimeSeconds
{
  if (self->thumbNailer)
    return 1000.0 * self->thumbNailer->GetTimeMilliSeconds();

  return -1;
}

- (double) getSeekTimePercentage
{
  if (self->thumbNailer)
  {
    int seekTime = self->thumbNailer->GetTimeMilliSeconds();
    if (seekTime < 0) seekTime = 0;
    int totalTime = self->thumbNailer->GetTotalTimeMilliSeconds();
    double percentage = (double)seekTime / totalTime;
    CLog::Log(LOGDEBUG, "Slider::getSeekTimePercentage(%f), value(%f)", percentage, self.value);
    return 100.0 * percentage;
  }
  return -1;
}

- (void)drawRect:(CGRect)rect
{
  [super drawRect:rect];
  CGContextRef ctx = UIGraphicsGetCurrentContext();
#if 1
  CGContextSetLineWidth(ctx, 1.0);
  CGContextSetStrokeColorWithColor(ctx, [[UIColor whiteColor] CGColor]);
  CGContextStrokeRect(ctx, self.bounds);

  CGContextSetLineWidth(ctx, 1.0);
  CGContextSetStrokeColorWithColor(ctx, [[UIColor whiteColor] CGColor]);
  CGContextStrokeRect(ctx, barRect);

  CGContextSetLineWidth(ctx, 1.0);
  CGContextSetStrokeColorWithColor(ctx, [[UIColor orangeColor] CGColor]);
  CGContextStrokeRect(ctx, thumbRect);
#endif

  // draw the vertical tick mark in the bar to show current position
  CGContextSetStrokeColorWithColor(ctx, [[UIColor whiteColor] CGColor]);
  CGContextSetLineWidth(ctx, 2.0);
  CGPoint thumbPointerBGN = CGPointMake(CGRectGetMidX(thumbRect), CGRectGetMinY(thumbRect));
  CGPoint thumbPointerEND = CGPointMake(thumbPointerBGN.x, CGRectGetMaxY(thumbRect));
  CGContextMoveToPoint(ctx, thumbPointerBGN.x, thumbPointerBGN.y);
  CGContextAddLineToPoint(ctx, thumbPointerEND.x, thumbPointerEND.y);
  CGContextStrokePath(ctx);

  videoRect = CGRectMake(0, 0, 400, 225);
  videoRect.origin.x = CGRectGetMidX(thumbRect) - videoRect.size.width/2;
  if (videoRectIsAboveBar)
    videoRect.origin.y = thumbRect.origin.y - (videoRect.size.height + 2);
  else
    videoRect.origin.y = thumbRect.origin.y + (thumbRect.size.height + 4);
  // clamp left/right sides to left/right sides of bar
  if (CGRectGetMinX(videoRect) < CGRectGetMinX(self.bounds))
    videoRect.origin.x = self.bounds.origin.x;
  if (CGRectGetMaxX(videoRect) > CGRectGetMaxX(self.bounds))
    videoRect.origin.x = CGRectGetMaxX(self.bounds) - videoRect.size.width;

  if (self->thumbNailer)
  {
    CGImageRef newThumbImage = self->thumbNailer->GetThumb();
    if (newThumbImage)
    {
      CGImageRelease(self->thumbImage);
      self->thumbImage = newThumbImage;
      CLog::Log(LOGDEBUG, "Slider::drawRect:got newThumbImage");
    }
  }
  if (self->thumbImage)
  {
    // image will be scaled, if necessary, to fit into rect
    // but we need to keep the correct aspect ration
    size_t width = CGImageGetWidth(self->thumbImage);
    size_t height = CGImageGetHeight(self->thumbImage);
    float aspect = (float)width / height;
    CGRect videoBounds = videoRect;
    videoBounds.size.height = videoRect.size.width / aspect;
    if (videoRectIsAboveBar)
      videoBounds.origin.y += videoRect.size.height - videoBounds.size.height;
    else
      videoBounds.origin.y -= videoRect.size.height - videoBounds.size.height;
    // clear to black the under video area, might not need this
    CGContextSetFillColorWithColor(ctx, [[UIColor blackColor] CGColor]);
    CGContextFillRect(ctx, videoBounds);
    // now we can draw the video thumb image
    CGContextDrawImage(ctx, videoBounds, self->thumbImage);
    // draw a thin white frame around the video thumb image
    CGContextSetStrokeColorWithColor(ctx, [[UIColor whiteColor] CGColor]);
    CGContextSetLineWidth(ctx, 0.5);
    CGContextStrokeRect(ctx, videoBounds);
  }
}

- (void)updateViews:(id)arg
{
  if (distance == 0.0)
    return;
  thumb = barRect.size.width * (CGFloat)((self.value - min) / distance);
  CGPoint thumbPoint = CGPointMake(barRect.origin.x + thumb - barRect.size.height/2, barRect.origin.y);
  thumbRect = CGRectMake(thumbPoint.x, thumbPoint.y, barRect.size.height, barRect.size.height);
  if (CGRectGetMaxX(thumbRect) > CGRectGetMaxX(self.bounds))
    thumbRect.origin.x = CGRectGetMaxX(self.bounds) - thumbRect.size.width;

  dispatch_async(dispatch_get_main_queue(),^{
    [self setNeedsDisplay];
  });

  // call ourselves back in 100ms
  //SEL singleParamSelector = @selector(updateViews:);
  //[self performSelector:singleParamSelector withObject:nil afterDelay:0.100];
}

//--------------------------------------------------------------
- (BOOL)shouldUpdateFocusInContext:(UIFocusUpdateContext *)context
{
  return YES;
}

- (void)didUpdateFocusInContext:(UIFocusUpdateContext *)context
    withAnimationCoordinator:(UIFocusAnimationCoordinator *)coordinator
{
  CLog::Log(LOGDEBUG, "Slider::didUpdateFocusInContext");
}

//--------------------------------------------------------------
- (IBAction)handleUpTapGesture:(UITapGestureRecognizer *)sender
{
  CLog::Log(LOGDEBUG, "Slider::handleUpTapGesture");
  if (self->deceleratingTimer)
    [self stopDeceleratingTimer];
}

//--------------------------------------------------------------
- (IBAction)handleDownTapGesture:(UITapGestureRecognizer *)sender
{
  CLog::Log(LOGDEBUG, "Slider::handleDownTapGesture");
  if (self->deceleratingTimer)
    [self stopDeceleratingTimer];
  /*
  KODI::MESSAGING::CApplicationMessenger::GetInstance().PostMsg(
    TMSG_GUI_ACTION, WINDOW_INVALID, -1, static_cast<void*>(new CAction(ACTION_SHOW_OSD)));
  */
}

//--------------------------------------------------------------
- (IBAction)handleLeftTapGesture:(UITapGestureRecognizer *)sender
{
  CLog::Log(LOGDEBUG, "Slider::handleLeftTapGesture");
  if (self->deceleratingTimer)
    [self stopDeceleratingTimer];
  else
  {
    if (self->thumbNailer)
    {
      int seekTime =  self->thumbNailer->GetTimeMilliSeconds() - 10000;
      if (seekTime < 0) seekTime = 0;
      int totalTime =  self->thumbNailer->GetTotalTimeMilliSeconds();
      double percentage = (double)seekTime / totalTime;
      [self set:percentage];
      thumbConstant = thumb;
    }
  }
}

//--------------------------------------------------------------
- (IBAction)handleRightTapGesture:(UITapGestureRecognizer *)sender
{
  CLog::Log(LOGDEBUG, "Slider::handleRightTapGesture");
  if (self->deceleratingTimer)
    [self stopDeceleratingTimer];
  else
  {
    if (self->thumbNailer)
    {
      int seekTime =  self->thumbNailer->GetTimeMilliSeconds() + 10000;
      int totalTime =  self->thumbNailer->GetTotalTimeMilliSeconds();
      if (seekTime > totalTime) seekTime = totalTime;
      double percentage = (double)seekTime / totalTime;
      [self set:percentage];
      thumbConstant = thumb;
    }
  }
}

//--------------------------------------------------------------
- (IBAction)handlePanGesture:(UIPanGestureRecognizer *)sender
{
  CLog::Log(LOGDEBUG, "Slider::handlePanGesture");
  CGPoint translation = [sender translationInView:self];
  CGPoint velocity =  [sender velocityInView:self];
  switch (sender.state)
  {
    case UIGestureRecognizerStateBegan:
        [self stopDeceleratingTimer];
        thumbConstant = thumb;
      break;
    case UIGestureRecognizerStateChanged:
      {
        double swipesForFullRange = 9.0;
        double leading = thumbConstant + translation.x / swipesForFullRange;
        [self set:leading / barRect.size.width];
      }
      break;
    case UIGestureRecognizerStateEnded:
    case UIGestureRecognizerStateCancelled:
      {
        thumbConstant = thumb;
        int direction = velocity.x > 0 ? 1 : -1;
        deceleratingVelocity = fabs(velocity.x) > decelerationMaxVelocity ? decelerationMaxVelocity * direction : velocity.x;
        self->deceleratingTimer = [NSTimer scheduledTimerWithTimeInterval:0.01 target:self selector:@selector(handleDeceleratingTimer:) userInfo:nil repeats:YES];
      }
      break;
    default:
      break;
  }
}

//--------------------------------------------------------------
- (void)handleDeceleratingTimer:(id)obj
{
  double leading = thumbConstant + deceleratingVelocity * 0.01;
  [self set:(double)leading / barRect.size.width];
  thumbConstant = thumb;

  deceleratingVelocity *= decelerationRate;
  if (![self isFocused] || fabs(deceleratingVelocity) < 1.0)
    [self stopDeceleratingTimer];
}

- (void)stopDeceleratingTimer
{
  [self->deceleratingTimer invalidate];
  self->deceleratingTimer = nil;
  self->deceleratingVelocity = 0.0;
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer shouldReceiveTouch:(UITouch *)touch
{
  CLog::Log(LOGDEBUG, "Slider::gestureRecognizer:shouldReceiveTouch");
  return YES;
}
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer shouldReceivePress:(UIPress *)press
{
  CLog::Log(LOGDEBUG, "Slider::gestureRecognizer:shouldReceivePress");
  return YES;
}

- (BOOL)gestureRecognizerShouldBegin:(UIGestureRecognizer *)gestureRecognizer
{
  CLog::Log(LOGDEBUG, "Slider::gestureRecognizerShouldBegin");
  if ([gestureRecognizer isKindOfClass:[UIPanGestureRecognizer class]])
  {
    UIPanGestureRecognizer *panGestureRecognizer = (UIPanGestureRecognizer*)gestureRecognizer;
    CGPoint translation = [panGestureRecognizer translationInView:self];
    if (fabs(translation.x) > fabs(translation.y))
      return [self isFocused];
  }
  else if ([gestureRecognizer isKindOfClass:[UITapGestureRecognizer class]])
  {
    return [self isFocused];
  }
  return NO;
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer *)otherGestureRecognizer
{
  return YES;
}

@end
  
