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

#import "cores/avfplayer/CustomURLProtocol.h"

#pragma mark - CCustomURLProtocol
@interface CCustomURLProtocol ()
@property (nonatomic, strong) NSURLConnection *connection;
@end

@implementation CCustomURLProtocol
// decide whether or not the call should be intercepted
+ (BOOL)canInitWithRequest:(NSURLRequest *)request
{
  static NSUInteger requestCount = 0;

  if ([NSURLProtocol propertyForKey:@"CCustomURLProtocolHandledKey" inRequest:request])
  {
    NSLog(@"CCustomURLProtocol canInitWithRequest2 #%lu: URL = %@", (unsigned long)requestCount++, request.URL.absoluteString);
    return NO;
  }
  return YES;
}

+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request
{
  NSLog(@"CCustomURLProtocol canonicalRequestForRequest");
  return request;
}

+ (BOOL)requestIsCacheEquivalent:(NSURLRequest *)a toRequest:(NSURLRequest *)b
{
  NSLog(@"CCustomURLProtocol requestIsCacheEquivalent");
  return [super requestIsCacheEquivalent:a toRequest:b];
}

// intercept the request and handle it yourself
- (id)initWithRequest:(NSURLRequest *)request
  cachedResponse:(NSCachedURLResponse *)cachedResponse client:(id<NSURLProtocolClient>)client
{
  NSLog(@"CCustomURLProtocol initWithRequest");
  return [super initWithRequest:request cachedResponse:cachedResponse client:client];
}

// load the request
- (void)startLoading
{
  NSLog(@"CCustomURLProtocol startLoading");

  NSMutableURLRequest *newRequest = [self.request mutableCopy];
  [NSURLProtocol setProperty:@YES forKey:@"CCustomURLProtocolHandledKey" inRequest:newRequest];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations" ///< 'NSURLConnection' is deprecated
  self.connection = [NSURLConnection connectionWithRequest:newRequest delegate:self];
#pragma clang diagnostic pop
}

// overload didReceive data
- (void)connection:(NSURLConnection *)connection didReceiveData:(NSData *)data
{
  NSLog(@"CCustomURLProtocol didReceiveData");

  [[self client] URLProtocol:self didLoadData:data];
}

// overload didReceiveResponse
- (void)connection:(NSURLConnection*)connection didReceiveResponse:(NSURLResponse *)response
{
  NSLog(@"CCustomURLProtocol didReceiveResponse");
  [[self client] URLProtocol:self didReceiveResponse:response cacheStoragePolicy:NSURLCacheStorageNotAllowed];
}

// overload didFinishLoading
- (void)connectionDidFinishLoading:(NSURLConnection *)connection
{
  NSLog(@"CCustomURLProtocol connectionDidFinishLoading");
  [[self client] URLProtocolDidFinishLoading:self];
  self.connection = nil;
}

// overload didFail
- (void)connection:(NSURLConnection *)connection didFailWithError:(NSError *)error
{
  NSLog(@"CCustomURLProtocol didFailWithError");
  [[self client] URLProtocol:self didFailWithError:error];
  self.connection = nil;
}

// handle load cancelation
- (void)stopLoading
{
  NSLog(@"CCustomURLProtocol stopLoading");
  [self.connection cancel];
}

@end
