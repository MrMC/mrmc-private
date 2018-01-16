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
#import "platform/darwin/NSLogDebugHelpers.h"
#import "platform/darwin/tvos/ProgressThumbNailer.h"
#import "guilib/GUISliderControl.h"
#import "utils/log.h"

#pragma mark -
@implementation FocusLayerViewSlider

- (id)initWithFrame:(CGRect)frame
{
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
    frame.size.height += videoRect.size.height + frame.size.height;
  }
  else
  {
    // if in upper area, expand down
    frame.origin.y += videoRect.size.height + frame.size.height + 2;
    frame.size.height += videoRect.size.height;
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
    //self->decelerationRate = 0.92;
    self->decelerationRate = 0.84;
    self->decelerationMaxVelocity = 1000;
    float percentage = 0.0;
    self->thumbImage = nullptr;
    self->thumbNailer = nullptr;
    if (g_application.m_pPlayer->IsPlayingVideo())
    {
      // get percentage from application, includes stacks
      percentage = g_application.GetPercentage() / 100.0;
      self->thumbNailer = new CProgressThumbNailer(g_application.CurrentFileItem());
    }
    // initial slider position
    [self set:percentage];

    auto pan = [[UIPanGestureRecognizer alloc]
      initWithTarget:self action:@selector(handlePanGesture:)];
    pan.delegate = self;
    [self addGestureRecognizer:pan];

    auto tapRecognizer = [[UITapGestureRecognizer alloc]
      initWithTarget: self action: @selector(handleTapGesture:)];
    tapRecognizer.allowedPressTypes  = @[[NSNumber numberWithInteger:UIPressTypeUpArrow],
                                         [NSNumber numberWithInteger:UIPressTypeDownArrow],
                                         [NSNumber numberWithInteger:UIPressTypeLeftArrow],
                                         [NSNumber numberWithInteger:UIPressTypeRightArrow]];
    tapRecognizer.delegate  = self;
    [self addGestureRecognizer:tapRecognizer];
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

- (void)drawRect:(CGRect)rect
{
  [super drawRect:rect];
  CGContextRef ctx = UIGraphicsGetCurrentContext();
#if 0
  CGContextSetLineWidth(ctx, 1.0);
  CGContextSetStrokeColorWithColor(ctx, [[UIColor whiteColor] CGColor]);
  CGContextStrokeRect(ctx, self.bounds);

  CGContextSetLineWidth(ctx, 1.0);
  CGContextSetStrokeColorWithColor(ctx, [[UIColor orangeColor] CGColor]);
  CGContextStrokeRect(ctx, thumbRect);
#endif

  CGContextSetStrokeColorWithColor(ctx, [[UIColor whiteColor] CGColor]);
  CGContextSetLineWidth(ctx, 2.0);
  CGPoint thumbPointerBGN = CGPointMake(CGRectGetMidX(thumbRect), CGRectGetMinY(thumbRect));
  CGPoint thumbPointerEND = CGPointMake(thumbPointerBGN.x, CGRectGetMaxY(thumbRect));
  CGContextMoveToPoint(ctx, thumbPointerBGN.x, thumbPointerBGN.y);
  CGContextAddLineToPoint(ctx, thumbPointerEND.x, thumbPointerEND.y);
  CGContextStrokePath(ctx);

  videoRect = CGRectMake(0, 0, 400, 225);
  videoRect.origin.x = CGRectGetMidX(thumbRect) - videoRect.size.width/2;
  videoRect.origin.y = thumbRect.origin.y - videoRect.size.height;
  videoRect.origin.y -= 2;
  if (CGRectGetMinX(videoRect) < CGRectGetMinX(self.bounds))
    videoRect.origin.x = self.bounds.origin.x;
  if (CGRectGetMaxX(videoRect) > CGRectGetMaxX(self.bounds))
    videoRect.origin.x = CGRectGetMaxX(self.bounds) - videoRect.size.width;
  CGContextSetFillColorWithColor(ctx, [[UIColor blackColor] CGColor]);
  CGContextFillRect(ctx, videoRect);

  CGImageRef newThumbImage = self->thumbNailer->GetThumb();
  if (newThumbImage)
  {
    CGImageRelease(self->thumbImage);
    self->thumbImage = newThumbImage;
    CLog::Log(LOGDEBUG, "Slider::drawRect:got newThumbImage");
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
    videoBounds.origin.y += videoRect.size.height - videoBounds.size.height;
    CGContextDrawImage(ctx, videoBounds, self->thumbImage);
  }

  CGContextSetStrokeColorWithColor(ctx, [[UIColor whiteColor] CGColor]);
  CGContextSetLineWidth(ctx, 0.5);
  CGContextStrokeRect(ctx, videoRect);
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
  [super didUpdateFocusInContext:context withAnimationCoordinator:coordinator];
  if (context.nextFocusedView == self)
  {
    /*
    coordinator.addCoordinatedAnimations({ () -> Void in
        self.seekerView.transform = CGAffineTransform(translationX: 0, y: -12)
        self.seekerLabelBackgroundInnerView.backgroundColor = .white
        self.seekerLabel.textColor = .black
        self.seekerLabelBackgroundView.layer.shadowOpacity = 0.5
        self.seekLineView.layer.shadowOpacity = 0.5
        }, completion: nil)
    */
  }
  else if (context.previouslyFocusedView == self)
  {
    /*
    coordinator.addCoordinatedAnimations({ () -> Void in
        self.seekerView.transform = .identity
        self.seekerLabelBackgroundInnerView.backgroundColor = .lightGray
        self.seekerLabel.textColor = .white
        self.seekerLabelBackgroundView.layer.shadowOpacity = 0
        self.seekLineView.layer.shadowOpacity = 0
        }, completion: nil)
    */
  }
}

//--------------------------------------------------------------
- (IBAction)handleTapGesture:(UITapGestureRecognizer *)sender
{
  CLog::Log(LOGDEBUG, "Slider::handleTapGesture");
  [self stopDeceleratingTimer];
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
        double swipesForFullRange = 8.0;
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

- (void)set:(double)percentage
{
  self.value = distance * (double)(percentage > 1 ? 1 : (percentage < 0 ? 0 : percentage)) + min;
  CLog::Log(LOGDEBUG, "Slider::set percentage(%f), value(%f)", percentage, self.value);
  if (self->thumbNailer)
    self->thumbNailer->RequestThumbAsPercentage(100.0 * percentage);
}

- (void)updateViews:(id)arg
{
  if (distance == 0.0)
    return;
  thumb = barRect.size.width * (CGFloat)((self.value - min) / distance);
  CGPoint thumbPoint = CGPointMake(barRect.origin.x + thumb, barRect.origin.y);
  thumbRect = CGRectMake(thumbPoint.x, thumbPoint.y, barRect.size.height, barRect.size.height);
  if (CGRectGetMaxX(thumbRect) > CGRectGetMaxX(self.bounds))
    thumbRect.origin.x = CGRectGetMaxX(self.bounds) - thumbRect.size.width;

  if (self->thumbNailer)
  {
    if (!self->thumbImage)
    {
      // no thumbImage yet, thumbNailer might still be busy
      SEL singleParamSelector = @selector(updateViews:);
      [self performSelector:singleParamSelector withObject:nil afterDelay:0.100];
    }
  }
  dispatch_async(dispatch_get_main_queue(),^{
    [self setNeedsDisplay];
  });
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
  
