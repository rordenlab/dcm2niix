//
//  AppDelegate.m
//  dcm2
//
//  Created by Chris Rorden on 4/7/14.
//  Copyright (c) 2014 Chris Rorden. All rights reserved.
//

#import "AppDelegate.h"
#include "./core/nii_dicom_batch.h"

@implementation AppDelegate
#if __has_feature(objc_arc_weak)
//automatic
#else
@synthesize window = _window;
@synthesize theTextView = _theTextView;
@synthesize compressCheck = _compressCheck;
@synthesize outputFilenameEdit = _outputFilenameEdit;
@synthesize folderButton = _folderButton;
#endif

- (void) processFile: (NSString*) fname
{
    //convert folder (DICOM) or file (PAR/REC) to NIFTI format
    [_theTextView setString: @""];//clear display
    struct TDCMopts optsTemp;
    optsTemp = opts; //conversion may change values like the outdir (if not specified)
    strcpy(optsTemp.indir, [fname cStringUsingEncoding:1]);
    optsTemp.isVerbose = true;
    clock_t start = clock();
    nii_loadDir (&(optsTemp));
    printf("required %fms\n", ((double)(clock()-start))/1000);
    fflush(stdout); //GUI buffers printf, display all results
}

- (BOOL)application:(NSApplication *)theApplication openFile:(NSString *)filename { //convert images
    [self processFile: filename];
    return true;
}

-(void) showExampleFilename {
    char niiFilename[1024];
    nii_createDummyFilename(niiFilename, opts);
    [self.theTextView setString: [NSString stringWithFormat:@"%s\nVersion %s\n", niiFilename,kDCMvers ]];//clear display
    //printf("Version %s\n",kDCMvers);
    //[self.theTextView setString: [NSString stringWithFormat:@"Output images will have names like %s", niiFilename ]];//clear display
    [self.theTextView setNeedsDisplay:YES];
}

- (void)controlTextDidChange:(NSNotification *)notification {
    //user has changed text field, 1: get desired file name mask
    NSTextField *textField = [notification object];
    //next: display example of what the provided filename mask will generate
    strcpy(opts.filename, [[textField stringValue] cStringUsingEncoding:1]);
    [self showExampleFilename];
}

-(void)showPrefs {
    _compressCheck.state = opts.isGz;
    //NSString *title = [NSString stringWithCString:opts.filename encoding:NSASCIIStringEncoding];
    //[_outputFilenameEdit setStringValue:title];
    [_outputFilenameEdit setStringValue:[NSString stringWithCString:opts.filename encoding:NSASCIIStringEncoding]];
    NSString *outdir = [NSString stringWithCString:opts.outdir encoding:NSASCIIStringEncoding];
    if ([outdir length] < 1)
        [_folderButton setTitle:@"input folder"];
    else if ([outdir length] > 40)
        [_folderButton setTitle:[NSString stringWithFormat:@"%@%@", @"...",[outdir substringFromIndex:[outdir length]-36]]];
    else
        [_folderButton setTitle:outdir];
    [self showExampleFilename];
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    //re-direct printf statements to appear in the text view component
    //Here we use Grand Central Dispatch, alternatively we could use NSNotification as described here:
    //   http://stackoverflow.com/questions/2406204/what-is-the-best-way-to-redirect-stdout-to-nstextview-in-cocoa
    NSPipe* pipe = [NSPipe pipe];
    NSFileHandle* pipeReadHandle = [pipe fileHandleForReading];
    dup2([[pipe fileHandleForWriting] fileDescriptor], fileno(stdout));
    source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, [pipeReadHandle fileDescriptor], 0, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0));
    dispatch_source_set_event_handler(source, ^{
        void* data = malloc(4096);
        ssize_t readResult = 0;
        do
        {
            errno = 0;
            readResult = read([pipeReadHandle fileDescriptor], data, 4096);
        } while (readResult == -1 && errno == EINTR);
        if (readResult > 0) {
            //AppKit UI should only be updated from the main thread
            NSString* stdOutString = [[NSString alloc] initWithBytesNoCopy:data length:readResult encoding:NSUTF8StringEncoding freeWhenDone:YES];
            dispatch_async(dispatch_get_main_queue(),^{
                [[[_theTextView textStorage] mutableString] appendString:stdOutString];
                [_theTextView setNeedsDisplay:YES];
            });
            [stdOutString release];
        }
        else{free(data);}
    });
    dispatch_resume(source);
    //read and display preferences
    const char *appPath = [[[NSBundle mainBundle] bundlePath] UTF8String];
   readIniFile (&opts, &appPath);
    [self showPrefs];
    fflush(stdout); //GUI buffers printf, display all results
    //finally, remove any bizarre options that XCode appends to Edit menu
    NSMenu* edit = [[[[NSApplication sharedApplication] mainMenu] itemWithTitle: @"Edit"] submenu];
    if ([[edit itemAtIndex: [edit numberOfItems] - 1] action] == NSSelectorFromString(@"orderFrontCharacterPalette:"))
        [edit removeItemAtIndex: [edit numberOfItems] - 1];
    if ([[edit itemAtIndex: [edit numberOfItems] - 1] action] == NSSelectorFromString(@"startDictation:"))
        [edit removeItemAtIndex: [edit numberOfItems] - 1];
    if ([[edit itemAtIndex: [edit numberOfItems] - 1] isSeparatorItem])
        [edit removeItemAtIndex: [edit numberOfItems] - 1];
    #if MAC_OS_X_VERSION_MIN_REQUIRED <= MAC_OS_X_VERSION_10_6
        //NSLog(@"yyyy");
    #endif
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)theApplication {
    return YES;
}

- (IBAction)compressCheckClick:(id)sender {
    opts.isGz = _compressCheck.state;
    [self showExampleFilename];
}

- (IBAction)outputFolderClick:(id)sender {
    //http://stackoverflow.com/questions/5621513/cocoa-select-choose-file-panel
    NSOpenPanel* openDlg = [NSOpenPanel openPanel];
    [openDlg setTitle: @"Select output folder (cancel to use input folder)"];
    [openDlg setCanChooseFiles:NO];
    [openDlg setCanChooseDirectories:YES];
    [openDlg setPrompt:@"Select"];
    NSInteger isOKButton = [openDlg runModal];
    NSArray* files = [openDlg URLs];
    NSString* outdir = @"";
    if ((isOKButton == NSOKButton)  && ([files count] > 0))
        outdir = [[files objectAtIndex:0] path];
    strcpy(opts.outdir, [outdir cStringUsingEncoding:1]);
    [self showPrefs];
}

- (IBAction)dicom2niiClick:(id)sender {
    NSOpenPanel* openDlg = [NSOpenPanel openPanel];
    [openDlg setTitle: @"Select folder that contains DICOM images"];
    [openDlg setCanChooseFiles:NO];
    [openDlg setCanChooseDirectories:YES];
    [openDlg setPrompt:@"Select"];
    if ([openDlg runModal] != NSOKButton ) return;
    NSArray* files = [openDlg URLs];
    if ([files count] < 1) return;
    [self processFile: [[files objectAtIndex:0] path] ];
}

- (IBAction)par2niiClick:(id)sender {
    #if __has_feature(objc_arc_weak)
    //NSOpenPanel *panel;
    #else
    //NSOpenPanel *panel = [[NSOpenPanel alloc] init];
    #endif
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    NSArray* fileTypes = [[NSArray alloc] initWithObjects:@"par", @"PAR", nil];
    [panel setTitle: @"Select Philips PAR images"];
    panel = [NSOpenPanel openPanel];
    [panel setFloatingPanel:YES];
    [panel setCanChooseDirectories:NO];
    [panel setCanChooseFiles:YES];
    [panel setAllowsMultipleSelection:YES];
    [panel setAllowedFileTypes:fileTypes];
    if ([panel runModal] != NSOKButton) {
        [fileTypes release];
        return;
    }
    [fileTypes release];
    NSArray* files = [panel URLs];
    NSUInteger n = [files count];
    if (n < 1) return;
    for (int i = 0; i < n; i++)
        [self processFile: [[files objectAtIndex:i] path] ];

}

-(void) dealloc {
#if __has_feature(objc_arc_weak)
    //automatic
#else
    [super dealloc];
#endif
    
}

-(NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
    saveIniFile (opts); //save preferences
    return NSTerminateNow;
}


@end
