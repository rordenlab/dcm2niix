//
//  AppDelegate.h
//  dcm2
//
//  Created by Chris Rorden on 4/7/14.
//  Copyright (c) 2014 Chris Rorden. All rights reserved.
//

//for NSTextView, turn off non-contiguous layout for background refresh

#import <Cocoa/Cocoa.h>
#include "./core/nii_dicom_batch.h"

@interface AppDelegate : NSObject <NSApplicationDelegate> {
    struct TDCMopts opts;
    dispatch_source_t source;
#if __has_feature(objc_arc_weak)
    //automatic
#else
    NSWindow *_window;
    NSTextView *_theTextView;
    NSButton *_compressCheck;
    NSTextField *_outputFilenameEdit;
    NSButton *_folderButton;
#endif
}
@property (assign) IBOutlet NSButton *compressCheck;
@property (assign) IBOutlet NSTextField *outputFilenameEdit;
@property (assign) IBOutlet NSButton *folderButton;
@property (assign) IBOutlet NSWindow *window;
@property (assign) IBOutlet NSTextView *theTextView;
- (void) processFile: (NSString*) fname;
- (IBAction)outputFolderClick:(id)sender;
- (IBAction)par2niiClick:(id)sender;
- (IBAction)dicom2niiClick:(id)sender;
//- (IBAction)compressCheckClick:(id)sender;
- (IBAction)compressCheckClick:(id)sender;



@end
