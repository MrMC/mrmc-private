/*
 *      Copyright (C) 2010-2013 Team XBMC
 *      http://xbmc.org
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "Application.h"
#include "guilib/GUIWindowManager.h"
#include "messaging/ApplicationMessenger.h"
#include "settings/DisplaySettings.h"
#include "platform/darwin/osx/CocoaInterface.h"
#include "windowing/osx/WinEventsOSX.h"
#include "windowing/WindowingFactory.h"

#import "OSXGLView.h"
#import "OSXGLWindow.h"
#import "platform/darwin/DarwinUtils.h"

//------------------------------------------------------------------------------------------
@implementation OSXGLWindow

+(void) SetMenuBarVisible
{
  NSApplicationPresentationOptions options = NSApplicationPresentationDefault;
  [NSApp setPresentationOptions:options];
}

+(void) SetMenuBarInvisible
{
  NSApplicationPresentationOptions options = NSApplicationPresentationHideMenuBar | NSApplicationPresentationHideDock;
  [NSApp setPresentationOptions:options];
}

-(id) initWithContentRect:(NSRect)box styleMask:(uint)style
{
  self = [super initWithContentRect:box styleMask:style backing:NSBackingStoreBuffered defer:YES];
  [self setDelegate:self];
  [self setAcceptsMouseMovedEvents:YES];
  // autosave the window position/size
  [[self windowController] setShouldCascadeWindows:NO]; // Tell the controller to not cascade its windows.
  [self setFrameAutosaveName:@"OSXGLWindowPositionHeightWidth"];  // Specify the autosave name for the window.
  
  g_application.m_AppFocused = true;
  
  return self;
}

-(void) dealloc
{
  [self setDelegate:nil];
  [super dealloc];
}

- (BOOL)windowShouldClose:(id)sender
{
  //NSLog(@"windowShouldClose");
  
  if (!g_application.m_bStop)
    KODI::MESSAGING::CApplicationMessenger::GetInstance().PostMsg(TMSG_QUIT);
  
  return NO;
}

- (void)windowDidExpose:(NSNotification *)aNotification
{
  //NSLog(@"windowDidExpose");
  g_application.m_AppFocused = true;
}

- (void)windowDidMove:(NSNotification *)aNotification
{
  //NSLog(@"windowDidMove");
  NSOpenGLContext* context = [NSOpenGLContext currentContext];
  if (context)
  {
    if ([context view])
    {
      NSPoint window_origin = [[[context view] window] frame].origin;
      XBMC_Event newEvent;
      memset(&newEvent, 0, sizeof(newEvent));
      newEvent.type = XBMC_VIDEOMOVE;
      newEvent.move.x = window_origin.x;
      newEvent.move.y = window_origin.y;
      g_application.OnEvent(newEvent);
    }
  }
}

- (void)windowDidResize:(NSNotification *)aNotification
{
  //NSLog(@"windowDidResize");
  NSRect rect = [self contentRectForFrameRect:[self frame]];
  
  if(!g_Windowing.IsFullScreen())
  {
    int RES_SCREEN = g_Windowing.DesktopResolution(g_Windowing.GetCurrentScreen());
    if(((int)rect.size.width == CDisplaySettings::GetInstance().GetResolutionInfo(RES_SCREEN).iWidth) &&
       ((int)rect.size.height == CDisplaySettings::GetInstance().GetResolutionInfo(RES_SCREEN).iHeight))
      return;
  }
  XBMC_Event newEvent;
  newEvent.type = XBMC_VIDEORESIZE;
  newEvent.resize.w = (int)rect.size.width;
  newEvent.resize.h = (int)rect.size.height;
  
  // check for valid sizes cause in some cases
  // we are hit during fullscreen transition from osx
  // and might be technically "zero" sized
  if (newEvent.resize.w != 0 && newEvent.resize.h != 0)
    g_application.OnEvent(newEvent);
  g_windowManager.MarkDirty();
}

-(void)windowDidChangeScreen:(NSNotification *)notification
{
  // user has moved the window to a
  // different screen
  if (!g_Windowing.IsFullScreen())
    g_Windowing.SetMovedToOtherScreen(true);
}

-(NSSize)windowWillResize:(NSWindow *)sender toSize:(NSSize)frameSize
{
  //NSLog(@"windowWillResize");
  return frameSize;
}

-(void)windowWillStartLiveResize:(NSNotification *)aNotification
{
  //NSLog(@"windowWillStartLiveResize");
}

-(void)windowDidEndLiveResize:(NSNotification *)aNotification
{
  //NSLog(@"windowDidEndLiveResize");
}

-(void)windowDidEnterFullScreen: (NSNotification*)pNotification
{
}

-(void)windowWillEnterFullScreen: (NSNotification*)pNotification
{
  
  // if osx is the issuer of the toggle
  // call XBMCs toggle function
  if (!g_Windowing.GetFullscreenWillToggle())
  {
    // indicate that we are toggling
    // flag will be reset in SetFullscreen once its
    // called from XBMCs gui thread
    g_Windowing.SetFullscreenWillToggle(true);
    KODI::MESSAGING::CApplicationMessenger::GetInstance().PostMsg(TMSG_TOGGLEFULLSCREEN);
  }
  else
  {
    // in this case we are just called because
    // of xbmc did a toggle - just reset the flag
    // we don't need to do anything else
    g_Windowing.SetFullscreenWillToggle(false);
  }
}

-(void)windowDidExitFullScreen: (NSNotification*)pNotification
{
  // if osx is the issuer of the toggle
  // call XBMCs toggle function
  if (!g_Windowing.GetFullscreenWillToggle())
  {
    // indicate that we are toggling
    // flag will be reset in SetFullscreen once its
    // called from XBMCs gui thread
    g_Windowing.SetFullscreenWillToggle(true);
    KODI::MESSAGING::CApplicationMessenger::GetInstance().PostMsg(TMSG_TOGGLEFULLSCREEN);
    
  }
  else
  {
    // in this case we are just called because
    // of xbmc did a toggle - just reset the flag
    // we don't need to do anything else
    g_Windowing.SetFullscreenWillToggle(false);
  }
}

-(void)windowWillExitFullScreen: (NSNotification*)pNotification
{
  
}

- (NSApplicationPresentationOptions) window:(NSWindow *)window willUseFullScreenPresentationOptions:(NSApplicationPresentationOptions)proposedOptions
{
  return (proposedOptions| NSApplicationPresentationAutoHideToolbar);
}

- (void)windowDidMiniaturize:(NSNotification *)aNotification
{
  //NSLog(@"windowDidMiniaturize");
  g_application.m_AppFocused = false;
}

- (void)windowDidDeminiaturize:(NSNotification *)aNotification
{
  //NSLog(@"windowDidDeminiaturize");
  g_application.m_AppFocused = true;
}

- (void)windowDidBecomeKey:(NSNotification *)aNotification
{
  //NSLog(@"windowDidBecomeKey");
  g_application.m_AppFocused = true;
  CWinEventsOSXImp::EnableInput();
}

- (void)windowDidResignKey:(NSNotification *)aNotification
{
  //NSLog(@"windowDidResignKey");
  g_application.m_AppFocused = false;
  CWinEventsOSXImp::DisableInput();
}

-(void) mouseDown:(NSEvent *) theEvent
{
  //NSLog(@"mouseDown");
  // if it is hidden - mouse is belonging to us!
  if (Cocoa_IsMouseHidden())
    CWinEventsOSXImp::HandleInputEvent(theEvent);
}

-(void) rightMouseDown:(NSEvent *) theEvent
{
  //NSLog(@"rightMouseDown");
  // if it is hidden - mouse is belonging to us!
  if (Cocoa_IsMouseHidden())
    CWinEventsOSXImp::HandleInputEvent(theEvent);
}

-(void) otherMouseDown:(NSEvent *) theEvent
{
  //NSLog(@"otherMouseDown");
  // if it is hidden - mouse is belonging to us!
  if (Cocoa_IsMouseHidden())
    CWinEventsOSXImp::HandleInputEvent(theEvent);
}

-(void) mouseUp:(NSEvent *) theEvent
{
  //NSLog(@"mouseUp");
  // if it is hidden - mouse is belonging to us!
  if (Cocoa_IsMouseHidden())
    CWinEventsOSXImp::HandleInputEvent(theEvent);
}

-(void) rightMouseUp:(NSEvent *) theEvent
{
  //NSLog(@"rightMouseUp");
  // if it is hidden - mouse is belonging to us!
  if (Cocoa_IsMouseHidden())
    CWinEventsOSXImp::HandleInputEvent(theEvent);
}

-(void) otherMouseUp:(NSEvent *) theEvent
{
  //NSLog(@"otherMouseUp");
  // if it is hidden - mouse is belonging to us!
  if (Cocoa_IsMouseHidden())
    CWinEventsOSXImp::HandleInputEvent(theEvent);
}

-(void) mouseMoved:(NSEvent *) theEvent
{
  //NSLog(@"mouseMoved");
  // if it is hidden - mouse is belonging to us!
  if (Cocoa_IsMouseHidden())
    CWinEventsOSXImp::HandleInputEvent(theEvent);
}

-(void) mouseDragged:(NSEvent *) theEvent
{
  //NSLog(@"mouseDragged");
  // if it is hidden - mouse is belonging to us!
  if (Cocoa_IsMouseHidden())
    CWinEventsOSXImp::HandleInputEvent(theEvent);
}

-(void) rightMouseDragged:(NSEvent *) theEvent
{
  //NSLog(@"rightMouseDragged");
  // if it is hidden - mouse is belonging to us!
  if (Cocoa_IsMouseHidden())
    CWinEventsOSXImp::HandleInputEvent(theEvent);
}

-(void) otherMouseDragged:(NSEvent *) theEvent
{
  //NSLog(@"otherMouseDragged");
  // if it is hidden - mouse is belonging to us!
  if (Cocoa_IsMouseHidden())
    CWinEventsOSXImp::HandleInputEvent(theEvent);
}

-(void) scrollWheel:(NSEvent *) theEvent
{
  //NSLog(@"scrollWheel");
  // if it is hidden - mouse is belonging to us!
  if (Cocoa_IsMouseHidden())
    CWinEventsOSXImp::HandleInputEvent(theEvent);
}

- (BOOL) canBecomeKeyWindow
{
  return YES;
}
@end
