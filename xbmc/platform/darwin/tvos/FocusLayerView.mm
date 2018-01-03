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

#import "platform/darwin/tvos/FocusLayerView.h"
#import "platform/darwin/NSLogDebugHelpers.h"

#pragma mark -
@implementation FocusLayerView

- (id)initWithFrame:(CGRect)frame
{
	self = [super initWithFrame:frame];
	if (self)
	{
    self.opaque = NO;
    self.userInteractionEnabled = YES;
    self.bounds = frame;
    self.layer.backgroundColor = [[UIColor clearColor] CGColor];

    // set to false to remove debug frame drawing.
    self->debug = true;
    self->focusable = false;
    self->frameColor = [UIColor whiteColor];

    [self setNeedsLayout];
    [self layoutIfNeeded];
  }
	return self;
}

//--------------------------------------------------------------
- (BOOL)canBecomeFocused
{
  //PRINT_SIGNATURE();
  if (self->focusable)
    return YES;
  return NO;
}

- (void)drawRect:(CGRect)rect
{
  //PRINT_SIGNATURE();
  if (self->debug)
  {
    CGContextRef context = UIGraphicsGetCurrentContext();
    // make the view transparent
    //CGContextSetBlendMode(context, kCGBlendModeClear);
    //CGContextFillRect(context, rect);

    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextSetLineWidth(context, 4.0);
    if (self.focused)
      CGContextSetStrokeColorWithColor(context, [[UIColor orangeColor] CGColor]);
    else
      CGContextSetStrokeColorWithColor(context, [self->frameColor CGColor]);
    CGContextStrokeRect(context, rect);
  }
}

- (void) setFocusable:(bool)focusable
{
  // true == views frames
  // false == control frames
  self->focusable = focusable;
  self->frameColor = [UIColor whiteColor];
  if (self->focusable)
    self->frameColor = [UIColor greenColor];
}

- (void) SetSizeLocation:(CGRect)location
{
  self.frame = location;
  self.bounds = location;
  [self setNeedsDisplay];
}

@end
