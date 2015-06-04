//
//  myWindow.m
//  dcm2
//
//  Created by Chris Rorden on 4/7/14.
//  Copyright (c) 2014 Chris Rorden. All rights reserved.
//

#import "myWindow.h"
#import "AppDelegate.h"

@implementation myWindow

- (void)awakeFromNib {
    //http://stackoverflow.com/questions/8567348/accepting-dragged-files-on-a-cocoa-application
    [self registerForDraggedTypes:[NSArray arrayWithObject:NSFilenamesPboardType]];
}


- (NSDragOperation)draggingEntered:(id < NSDraggingInfo >)sender {
    return NSDragOperationCopy;
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    return NSDragOperationCopy;
}

-(BOOL)canBecomeKeyWindow
{
    return YES;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSPasteboard *pboard = [sender draggingPasteboard];
    NSArray *filenames = [pboard propertyListForType:NSFilenamesPboardType];
    if (1 == filenames.count)
        if ([[NSApp delegate] respondsToSelector:@selector(application:openFile:)]) {
            //return [[NSApp delegate] application:NSApp openFile:[filenames lastObject]];
            [(AppDelegate *)[NSApp  delegate] processFile:[filenames lastObject]];
            return TRUE;
        }
    
    return FALSE;
}

@end
