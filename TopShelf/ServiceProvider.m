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

#import "ServiceProvider.h"

#if (defined HAVE_CONFIG_H)
#include "config.h"
#endif

@interface ServiceProvider ()

@end

@implementation ServiceProvider


- (instancetype)init {
    self = [super init];
    if (self) {
    }
    return self;
}

#pragma mark - TVTopShelfProvider protocol

- (TVTopShelfContentStyle)topShelfStyle
{
    // Return desired Top Shelf style.
    return TVTopShelfContentStyleSectioned;
}

- (NSArray *)topShelfItems
{
    NSString* groupid = [NSString stringWithUTF8String:"group.tv.mrmc.shared"];
    NSString* groupURL = [NSString stringWithUTF8String:"mrmc"];
    NSLog(@"TopShelf ID: %@", groupid);
    NSMutableArray *topShelfItems = [[NSMutableArray alloc] init];;
    NSUserDefaults *shared = [[NSUserDefaults alloc] initWithSuiteName:groupid];
  
    TVContentIdentifier *wrapperIdentifier = [[TVContentIdentifier alloc] initWithIdentifier:@"shelf-wrapper" container:nil];
  
    NSFileManager* fileManager = [NSFileManager defaultManager];
    NSURL* storeUrl = [fileManager containerURLForSecurityApplicationGroupIdentifier:groupid];
    if (!storeUrl)
      return (NSArray *)topShelfItems;

    storeUrl = [storeUrl URLByAppendingPathComponent:@"Library" isDirectory:TRUE];
    storeUrl = [storeUrl URLByAppendingPathComponent:@"Caches" isDirectory:TRUE];
    storeUrl = [storeUrl URLByAppendingPathComponent:@"RA" isDirectory:TRUE];

    NSMutableArray *itemOrder = [shared objectForKey:@"mrmc_itemOrder"];
    for (NSUInteger i = 0; i < [itemOrder count]; i++)
    {
      // {"MRA", "TVRA", "MIP", "TVIP"}
      // prefixed with mrmc_ mrmc_title_
      NSString * item = [itemOrder objectAtIndex:i];
      if ([item isEqualToString:@"MRA"])
      {
        NSMutableArray *movieArrayRA = [shared objectForKey:@"mrmc_MRA"];

        if ([movieArrayRA count] > 0)
        {
          TVContentItem *itemMovie = [[TVContentItem alloc] initWithContentIdentifier:wrapperIdentifier];
          NSMutableArray *ContentItems = [[NSMutableArray alloc] init];
          for (NSUInteger i = 0; i < [movieArrayRA count]; i++)
          {
            NSMutableDictionary * movieDict = [[NSMutableDictionary alloc] init];
            movieDict = [movieArrayRA objectAtIndex:i];

            TVContentIdentifier *identifier = [[TVContentIdentifier alloc] initWithIdentifier:@"VOD" container:wrapperIdentifier];
            TVContentItem *contentItem = [[TVContentItem alloc] initWithContentIdentifier:identifier];

            contentItem.imageURL = [storeUrl URLByAppendingPathComponent:[movieDict valueForKey:@"thumb"] isDirectory:FALSE];
            contentItem.imageShape = TVContentItemImageShapePoster;
            contentItem.title = [movieDict valueForKey:@"title"];
            NSString *url = [movieDict valueForKey:@"url"];
            contentItem.displayURL = [NSURL URLWithString:[NSString stringWithFormat:@"%@://display/%@",groupURL,url]];
            contentItem.playURL = [NSURL URLWithString:[NSString stringWithFormat:@"%@://play/%@",groupURL,url]];
            [ContentItems addObject:contentItem];
          }
          itemMovie.title = [shared stringForKey:@"mrmc_title_MRA"];
          itemMovie.topShelfItems = ContentItems;
          [topShelfItems addObject:itemMovie];
        }
      }
      else if ([item isEqualToString:@"TVRA"])
      {
        NSArray * tvArrayRA = [shared valueForKey:@"mrmc_TVRA"];

        if ([tvArrayRA count] > 0)
        {
          TVContentItem *itemTv = [[TVContentItem alloc] initWithContentIdentifier:wrapperIdentifier];
          NSMutableArray *ContentItemsTv = [[NSMutableArray alloc] init];
          for (NSUInteger i = 0; i < [tvArrayRA count]; i++)
          {
            NSMutableDictionary * tvDict = [[NSMutableDictionary alloc] init];
            tvDict = [tvArrayRA objectAtIndex:i];

            TVContentIdentifier *identifier = [[TVContentIdentifier alloc] initWithIdentifier:@"VOD" container:wrapperIdentifier];
            TVContentItem *contentItem = [[TVContentItem alloc] initWithContentIdentifier:identifier];

            contentItem.imageURL = [storeUrl URLByAppendingPathComponent:[tvDict valueForKey:@"thumb"] isDirectory:FALSE];
            contentItem.imageShape = TVContentItemImageShapePoster;
            contentItem.title = [tvDict valueForKey:@"title"];
            NSString *url = [tvDict valueForKey:@"url"];
            contentItem.displayURL = [NSURL URLWithString:[NSString stringWithFormat:@"%@://display/%@",groupURL,url]];
            contentItem.playURL = [NSURL URLWithString:[NSString stringWithFormat:@"%@://play/%@",groupURL,url]];
            [ContentItemsTv addObject:contentItem];
          }
          itemTv.title = [shared stringForKey:@"mrmc_title_TVRA"];
          itemTv.topShelfItems = ContentItemsTv;
          [topShelfItems addObject:itemTv];
        }
      }
      else if ([item isEqualToString:@"MIP"])
      {
        NSMutableArray *movieArrayPR = [shared objectForKey:@"mrmc_MIP"];

        if ([movieArrayPR count] > 0)
        {
          TVContentItem *itemMovie = [[TVContentItem alloc] initWithContentIdentifier:wrapperIdentifier];
          NSMutableArray *ContentItems = [[NSMutableArray alloc] init];
          for (NSUInteger i = 0; i < [movieArrayPR count]; i++)
          {
            NSMutableDictionary * movieDict = [[NSMutableDictionary alloc] init];
            movieDict = [movieArrayPR objectAtIndex:i];

            TVContentIdentifier *identifier = [[TVContentIdentifier alloc] initWithIdentifier:@"VOD" container:wrapperIdentifier];
            TVContentItem *contentItem = [[TVContentItem alloc] initWithContentIdentifier:identifier];

            contentItem.imageURL = [storeUrl URLByAppendingPathComponent:[movieDict valueForKey:@"thumb"] isDirectory:FALSE];
            contentItem.imageShape = TVContentItemImageShapePoster;
            contentItem.title = [movieDict valueForKey:@"title"];
            NSString *url = [movieDict valueForKey:@"url"];
            contentItem.displayURL = [NSURL URLWithString:[NSString stringWithFormat:@"%@://display/%@",groupURL,url]];
            contentItem.playURL = [NSURL URLWithString:[NSString stringWithFormat:@"%@://play/%@",groupURL,url]];
            [ContentItems addObject:contentItem];
          }
          itemMovie.title = [shared stringForKey:@"mrmc_title_MIP"];
          itemMovie.topShelfItems = ContentItems;
          [topShelfItems addObject:itemMovie];
        }
      }
      else if ([item isEqualToString:@"TVIP"])
      {
        NSArray * tvArrayPR = [shared valueForKey:@"mrmc_TVIP"];

        if ([tvArrayPR count] > 0)
        {
          TVContentItem *itemTv = [[TVContentItem alloc] initWithContentIdentifier:wrapperIdentifier];
          NSMutableArray *ContentItemsTv = [[NSMutableArray alloc] init];
          for (NSUInteger i = 0; i < [tvArrayPR count]; i++)
          {
            NSMutableDictionary * tvDict = [[NSMutableDictionary alloc] init];
            tvDict = [tvArrayPR objectAtIndex:i];

            TVContentIdentifier *identifier = [[TVContentIdentifier alloc] initWithIdentifier:@"VOD" container:wrapperIdentifier];
            TVContentItem *contentItem = [[TVContentItem alloc] initWithContentIdentifier:identifier];

            contentItem.imageURL = [storeUrl URLByAppendingPathComponent:[tvDict valueForKey:@"thumb"] isDirectory:FALSE];
            contentItem.imageShape = TVContentItemImageShapePoster;
            contentItem.title = [tvDict valueForKey:@"title"];
            NSString *url = [tvDict valueForKey:@"url"];
            contentItem.displayURL = [NSURL URLWithString:[NSString stringWithFormat:@"%@://display/%@",groupURL,url]];
            contentItem.playURL = [NSURL URLWithString:[NSString stringWithFormat:@"%@://play/%@",groupURL,url]];
            [ContentItemsTv addObject:contentItem];
          }
          itemTv.title = [shared stringForKey:@"mrmc_title_TVIP"];
          itemTv.topShelfItems = ContentItemsTv;
          [topShelfItems addObject:itemTv];
        }
      }
    }
    

  
    return (NSArray *)topShelfItems;
}

@end
