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

#import <UIKit/UIKit.h>
#import <AVFoundation/AVAssetResourceLoader.h>

#import "cores/avfplayer/MrMCCustomURLDelegate.h"

namespace XFILE
{
  class CFile;
}

static NSString *httpScheme = @"http";
static NSString *customPlaylistScheme = @"cplp";

static NSString *customPlayListFormatPrefix = @"#EXTM3U\n"
"#EXT-X-PLAYLIST-TYPE:EVENT\n"
"#EXT-X-TARGETDURATION:10\n"
"#EXT-X-VERSION:3\n"
"#EXT-X-MEDIA-SEQUENCE:0\n";

static NSString *customPlayListFormatElementInfo = @"#EXTINF:10, no desc\n";
static NSString *customPlaylistFormatElementSegment = @"%@/fileSequence%d.ts\n";

static NSString *customPlayListFormatEnd = @"#EXT-X-ENDLIST";
static int badRequestErrorCode = 400;


@interface CMrMCCustomURLDelegate ()
- (BOOL) schemeSupported:(NSString*) scheme;
- (void) reportError:(AVAssetResourceLoadingRequest *)loadingRequest withErrorCode:(int) error;
@end

@interface CMrMCCustomURLDelegate (CustomPlaylist)
- (BOOL) isCustomPlaylistSchemeValid:(NSString*) scheme;
- (NSString*) getCustomPlaylist:(NSString *)urlPrefix totalElements:(NSInteger) elements;
- (BOOL) handleCustomPlaylistRequest:(AVAssetResourceLoadingRequest*) loadingRequest;
@end

#pragma mark - CMrMCCustomURLDelegate
@implementation CMrMCCustomURLDelegate
- (BOOL) schemeSupported:(NSString *)scheme
{
  if ([self isCustomPlaylistSchemeValid:scheme])
    return YES;
  return NO;
}

-(CMrMCCustomURLDelegate *) init
{
  self = [super init];
  return self;
}

- (void) reportError:(AVAssetResourceLoadingRequest *)loadingRequest withErrorCode:(int) error
{
  [loadingRequest finishLoadingWithError:
    [NSError errorWithDomain: NSURLErrorDomain code:error userInfo: nil]];
}

- (BOOL) resourceLoader:(AVAssetResourceLoader *)resourceLoader
  shouldWaitForLoadingOfRequestedResource:(AVAssetResourceLoadingRequest *)loadingRequest
{
    NSString *scheme = [[[loadingRequest request] URL] scheme];
    
    if ([self isCustomPlaylistSchemeValid:scheme])
    {
        dispatch_async(dispatch_get_main_queue(),
        ^{
          [self handleCustomPlaylistRequest:loadingRequest];
        });
        return YES;
    }

    return NO;
}

@end

#pragma mark - CMrMCCustomURLDelegate CustomPlaylist

@implementation CMrMCCustomURLDelegate (CustomPlaylist)

- (BOOL) isCustomPlaylistSchemeValid:(NSString *)scheme
{
  return ([customPlaylistScheme isEqualToString:scheme]);
}

- (NSString*) getCustomPlaylist:(NSString *)urlPrefix totalElements:(NSInteger) elements
{
  static NSMutableString  *customPlaylist = nil;
  
  if (customPlaylist)
    return customPlaylist;
  
  customPlaylist = [[NSMutableString alloc] init];
  [customPlaylist appendString:customPlayListFormatPrefix];
  for (int i = 0; i < elements; ++i)
  {
    [customPlaylist appendString:customPlayListFormatElementInfo];
    [customPlaylist appendFormat:customPlaylistFormatElementSegment, urlPrefix, i];
  }
  [customPlaylist appendString:customPlayListFormatEnd];

  return customPlaylist;
}

- (BOOL) handleCustomPlaylistRequest:(AVAssetResourceLoadingRequest *)loadingRequest
{
  //Prepare the playlist with redirect scheme.
	NSString *prefix = [[[[loadingRequest request] URL] absoluteString]
    stringByReplacingOccurrencesOfString:customPlaylistScheme withString:httpScheme];

  NSRange range = [prefix rangeOfString:@"/" options:NSBackwardsSearch];
  prefix = [prefix substringToIndex:range.location];

  NSData *data = [[self getCustomPlaylist:prefix totalElements:150]
    dataUsingEncoding:NSUTF8StringEncoding];
  if (data)
  {
    [loadingRequest.dataRequest respondWithData:data];
    [loadingRequest finishLoading];
  }
  else
  {
    [self reportError:loadingRequest withErrorCode:badRequestErrorCode];
  }
  
  return YES;
}

@end
