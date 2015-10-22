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

#import "system.h"
#import "filesystem/File.h"
#import "utils/log.h"
#import "cores/avfplayer/FileResourceLoader.h"

#define CFILERESOURCELOADER_DEBUG 0
#define CFILERESOURCELOADER_BUFFERSIZE (2048*1024)

//static NSString *kResourceLoaderErrorDomain = @"ResourceLoaderError";
typedef enum {
    RLErrorCancel = -1,
    RLErrorWrongURLScheme = -2,
    RLErrorFileReadingFailure = -3,
    RLErrorFileReadingNoData = -4,
    RLErrorFileReadingZeroLengthData = -5,
    RLErrorFileWritingError = -6
}RLErrorCode;

#pragma mark - CFileResourceLoader
@interface CFileResourceLoader ()
@property (nonatomic, assign) BOOL isCancelled;
@property (nonatomic) XFILE::CFile *cfile;
@property (nonatomic) uint8_t *buffer;

@end

@implementation CFileResourceLoader
- (id)initWithCFile:(XFILE::CFile *) cfile
{
	self = [super init];
	if (self)
  {
    self.isCancelled = NO;
    self.cfile = cfile;
  }
	return self;
}

- (void)dealloc
{
#if CFILERESOURCELOADER_DEBUG
  CLog::Log(LOGNOTICE, "CFileResourceLoader dealloc");
#endif
  if (self.cfile)
    SAFE_DELETE(self.cfile);
  if (self.buffer)
    SAFE_DELETE_ARRAY(self.buffer);
  [super dealloc];
}

- (void)cancel
{
  self.isCancelled = YES;
}

- (void)reportError:(AVAssetResourceLoadingRequest *)loadingRequest
  withErrorCode:(int) error
{
  [loadingRequest finishLoadingWithError:
    [NSError errorWithDomain: NSURLErrorDomain code:error userInfo: nil]];
}

- (void)fillInContentInformation:(AVAssetResourceLoadingRequest *)loadingRequest
{
#if CFILERESOURCELOADER_DEBUG
  CLog::Log(LOGNOTICE, "resourceLoader contentRequest");
#endif
  AVAssetResourceLoadingContentInformationRequest *contentInformationRequest;
  contentInformationRequest = loadingRequest.contentInformationRequest;

  self.buffer = new uint8_t[CFILERESOURCELOADER_BUFFERSIZE];
  //provide information about the content
  // https://developer.apple.com/library/ios/documentation/Miscellaneous/Reference/UTIRef/Articles/System-DeclaredUniformTypeIdentifiers.html
  //NSString *mimeType = @"public.mpeg-4";                    // xxx.mp4
  NSString *mimeType = @"com.apple.quicktime-movie";        // xxx.mov
  //NSString *mimeType = @"video/mp2t";                       // xxx.ts
  //NSString *mimeType = @"public.mpeg-2-transport-stream";
  //NSString *mimeType = @"application/x-mpegurl";            // xxx.m3u8

  contentInformationRequest.contentType = mimeType;
  contentInformationRequest.contentLength = self.cfile->GetLength();
  //contentInformationRequest.byteRangeAccessSupported = YES;
}

- (BOOL)resourceLoader:(AVAssetResourceLoader *)resourceLoader
  shouldWaitForLoadingOfRequestedResource:(AVAssetResourceLoadingRequest *)loadingRequest
{
#if CFILERESOURCELOADER_DEBUG
  CLog::Log(LOGNOTICE, "resourceLoader shouldWaitForLoadingOfRequestedResource");
#endif
  BOOL canHandle = NO;

  NSURL *resourceURL = [loadingRequest.request URL];
  if ([resourceURL.scheme isEqualToString:MrMCScheme])
  {
    canHandle = YES;
    
    if (self.isCancelled == YES)
    {
      [self reportError:loadingRequest withErrorCode:RLErrorCancel];
      return canHandle;
    }

    std::string path = [resourceURL.path UTF8String];
#if CFILERESOURCELOADER_DEBUG
    CLog::Log(LOGNOTICE, "resourceLoader resourceURL.path(%s)", path.c_str());
#endif
    if (loadingRequest.contentInformationRequest != nil)
      [self fillInContentInformation:loadingRequest];

    if (loadingRequest.dataRequest != nil)
    {
#if CFILERESOURCELOADER_DEBUG
      CLog::Log(LOGNOTICE, "resourceLoader dataRequest");
#endif
      AVAssetResourceLoadingDataRequest *dataRequest = loadingRequest.dataRequest;

      NSURLResponse* response = [[NSURLResponse alloc] initWithURL:resourceURL MIMEType:@"video/quicktime" expectedContentLength:[dataRequest requestedLength] textEncodingName:nil];
      [loadingRequest setResponse:response];

#if CFILERESOURCELOADER_DEBUG
      CLog::Log(LOGNOTICE, "resourceLoader1 currentOffset(%lld), requestedOffset(%lld), requestedLength(%ld)",
        dataRequest.currentOffset, dataRequest.requestedOffset, dataRequest.requestedLength);
#endif
      NSUInteger remainingLength =
        [dataRequest requestedLength] - static_cast<NSUInteger>([dataRequest currentOffset] - [dataRequest requestedOffset]);

      self.cfile->Seek(dataRequest.currentOffset, SEEK_SET);
      do {
        NSUInteger receivedLength = dataRequest.requestedLength > CFILERESOURCELOADER_BUFFERSIZE ? CFILERESOURCELOADER_BUFFERSIZE : dataRequest.requestedLength;
        // Read returns -1 if fails, watch signed vs unsigned vars
        ssize_t receivedSize = self.cfile->Read(self.buffer, receivedLength);
        if (receivedSize > 0)
        {
          receivedLength = receivedSize;

  #if CFILERESOURCELOADER_DEBUG
          CLog::Log(LOGNOTICE, "resourceLoader2 currentOffset(%lld), requestedOffset(%lld), requestedLength(%ld)",
            dataRequest.currentOffset, dataRequest.requestedOffset, dataRequest.requestedLength);
  #endif
          NSUInteger length = MIN(receivedLength, remainingLength);
          NSData* decodedData = [NSData dataWithBytes:self.buffer length:length];
  #if CFILERESOURCELOADER_DEBUG
          CLog::Log(LOGNOTICE, "resourceLoader [dataRequest respondWithData] length(%ld)", length);
  #endif
          //NSURLResponse *response = [[NSURLResponse alloc] initWithURL:loadingRequest.request.URL MIMETYPE:@"video/mp4" expetedContentLength:length textEncodingName:nil]
          [dataRequest respondWithData:decodedData];
          remainingLength -= length;
        }
        else
        {
          [self reportError:loadingRequest withErrorCode:RLErrorFileReadingFailure];
          break;
        }

      } while (self.isCancelled == NO && remainingLength);
      
      if (self.isCancelled == YES)
      {
        [self reportError:loadingRequest withErrorCode:RLErrorCancel];
      }
      else
      {
        if ([dataRequest currentOffset] + [dataRequest requestedLength] >= [dataRequest requestedOffset])
          [loadingRequest finishLoading];
      }
    }
  }

  return canHandle;
}

- (void)resourceLoader:(AVAssetResourceLoader *)resourceLoader
didCancelLoadingRequest:(AVAssetResourceLoadingRequest *)loadingRequest
{
#if CFILERESOURCELOADER_DEBUG
  CLog::Log(LOGNOTICE, "resourceLoader didCancelLoadingRequest");
#endif
}

@end

