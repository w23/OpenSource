#import <kapusha/core/Core.h>
#import <kapusha/sys/osx/KPView.h>
#import "OpenSource.h"
#import "BSPAppDelegate.h"

class CocoaLog : public kapusha::Log::ISystemLog
{
public:
  virtual void write(const char* msg)
  {
    NSLog(@"%s", msg);
  }
};

@interface BSPAppDelegate ()
@property (assign) IBOutlet KPView *viewport;
@property (strong) NSString *bspPath;
@property (strong) NSString *bspFile;
@end

@implementation BSPAppDelegate

- (void)dealloc
{
  [super dealloc];
}

- (BOOL)application:(NSApplication *)theApplication
           openFile:(NSString *)filename
{
  self.bspFile = [[filename lastPathComponent] stringByDeletingPathExtension];
  self.bspPath = [[[filename stringByDeletingLastPathComponent]
                  stringByDeletingLastPathComponent] stringByAppendingString:@"/"];
  return YES;
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
  //! \hack force likning KPView class from libkapusha
  [KPView class];
  
  KP_LOG_OPEN(0, new CocoaLog);
  [self.window setAcceptsMouseMovedEvents:YES];
  [self.viewport setViewport:new OpenSource(
    [self.bspPath cStringUsingEncoding:NSUTF8StringEncoding],
    [self.bspFile cStringUsingEncoding:NSUTF8StringEncoding], 128)];
}

- (BOOL)windowShouldClose:(id)sender
{
  [[NSApplication sharedApplication] terminate:self];
  return YES;
}

@end
