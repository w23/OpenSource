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
@end

@implementation BSPAppDelegate

- (void)dealloc
{
  [super dealloc];
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
  // \hack force likning KPView class from libkapusha
  [KPView class];
  
  KP_LOG_OPEN(0, new CocoaLog);
  [self.window setAcceptsMouseMovedEvents:YES];
  [self.viewport setViewport:new OpenSource("/Users/w23/tmp/hl1/", "c0a0", 128)];
}

- (BOOL)windowShouldClose:(id)sender
{
  [[NSApplication sharedApplication] terminate:self];
  return YES;
}

@end
