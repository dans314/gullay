/*
 * Three's Company - the plugin window on macOS: a WKWebView inside the host's NSView.
 * Built with ARC off (manual retain / release), so it works whatever the host was built with.
 */
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include "gy_gui.h"

// Every class name gets a per-build suffix so two copies of the plugin can be loaded at once
// without their Objective-C classes clashing.
#ifndef GY_OBJC_SUFFIX
#define GY_OBJC_SUFFIX 0201
#endif
#define GY_CAT2(a, b) a##b
#define GY_CAT(a, b) GY_CAT2(a, b)
#define GYMessageHandler GY_CAT(GYMessageHandler_, GY_OBJC_SUFFIX)
#define GYTicker GY_CAT(GYTicker_, GY_OBJC_SUFFIX)
#define GYWebView GY_CAT(GYWebView_, GY_OBJC_SUFFIX)

@interface GYMessageHandler : NSObject <WKScriptMessageHandler>
{
  @public
    tcgui::Bridge *bridge;
}
@end

@implementation GYMessageHandler
- (void)userContentController:(WKUserContentController *)controller didReceiveScriptMessage:(WKScriptMessage *)message
{
    if (!bridge)
        return;
    id body = message.body;
    if ([body isKindOfClass:[NSString class]])
        bridge->onMessage([(NSString *)body UTF8String]);
}
@end

// Lets clicks reach the page even when the host window is not key, and keeps keyboard focus
// out of the way of the host's own shortcuts unless a text field is being edited.
@interface GYWebView : WKWebView
@end
@implementation GYWebView
- (BOOL)acceptsFirstMouse:(NSEvent *)event
{
    return YES;
}
@end

@interface GYTicker : NSObject
{
  @public
    tcgui::Bridge *bridge;
    WKWebView *web;
    NSTimer *timer;
}
- (void)tick:(NSTimer *)t;
@end

@implementation GYTicker
- (void)tick:(NSTimer *)t
{
    if (!bridge || !web)
        return;
    std::string js = bridge->poll();
    if (!js.empty())
    {
        NSString *s = [[NSString alloc] initWithBytes:js.data() length:js.size() encoding:NSUTF8StringEncoding];
        if (s)
        {
            [web evaluateJavaScript:s completionHandler:nil];
            [s release];
        }
    }
}
@end

namespace
{
struct MacGui
{
    GYWebView *web{nil};
    GYMessageHandler *handler{nil};
    GYTicker *ticker{nil};
};
} // namespace

namespace tcgui
{
void *create(Bridge *b, int w, int h)
{
    @autoreleasepool
    {
        auto *g = new MacGui();
        WKWebViewConfiguration *cfg = [[WKWebViewConfiguration alloc] init];
        g->handler = [[GYMessageHandler alloc] init];
        g->handler->bridge = b;
        [cfg.userContentController addScriptMessageHandler:g->handler name:@"tc"];
        g->web = [[GYWebView alloc] initWithFrame:NSMakeRect(0, 0, w, h) configuration:cfg];
        [cfg release];
        g->web.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        NSString *html = [NSString stringWithUTF8String:b->html()];
        [g->web loadHTMLString:html baseURL:nil];
        g->ticker = [[GYTicker alloc] init];
        g->ticker->bridge = b;
        g->ticker->web = g->web;
        g->ticker->timer = [NSTimer timerWithTimeInterval:1.0 / 30.0
                                                   target:g->ticker
                                                 selector:@selector(tick:)
                                                 userInfo:nil
                                                  repeats:YES];
        [[NSRunLoop mainRunLoop] addTimer:g->ticker->timer forMode:NSRunLoopCommonModes];
        return g;
    }
}

bool setParent(void *gui, void *parent)
{
    auto *g = (MacGui *)gui;
    if (!g || !parent)
        return false;
    NSView *p = (NSView *)parent;
    [g->web setFrame:p.bounds];
    [p addSubview:g->web];
    return true;
}

void setSize(void *gui, int w, int h)
{
    auto *g = (MacGui *)gui;
    if (g && g->web)
        [g->web setFrame:NSMakeRect(0, 0, w, h)];
}

void show(void *gui, bool on)
{
    auto *g = (MacGui *)gui;
    if (g && g->web)
        [g->web setHidden:on ? NO : YES];
}

void destroy(void *gui)
{
    auto *g = (MacGui *)gui;
    if (!g)
        return;
    @autoreleasepool
    {
        if (g->ticker)
        {
            [g->ticker->timer invalidate];
            g->ticker->bridge = nullptr;
            g->ticker->web = nil;
            [g->ticker release];
        }
        if (g->web)
        {
            [g->web.configuration.userContentController removeScriptMessageHandlerForName:@"tc"];
            [g->web removeFromSuperview];
            [g->web release];
        }
        if (g->handler)
        {
            g->handler->bridge = nullptr;
            [g->handler release];
        }
    }
    delete g;
}

std::string chooseFolder(void *, const std::string &start)
{
    @autoreleasepool
    {
        NSOpenPanel *p = [NSOpenPanel openPanel];
        p.canChooseFiles = NO;
        p.canChooseDirectories = YES;
        p.canCreateDirectories = YES;
        p.allowsMultipleSelection = NO;
        p.prompt = @"Use This Folder";
        p.message = @"Choose where Three's Company keeps your presets";
        if (!start.empty())
            p.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:start.c_str()] isDirectory:YES];
        if ([p runModal] == NSModalResponseOK && p.URL)
            return std::string(p.URL.path.UTF8String);
        return std::string();
    }
}

void reveal(const std::string &path)
{
    @autoreleasepool
    {
        if (!path.empty())
            [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()] isDirectory:YES]];
    }
}
void openWeb(const std::string &url)
{
    @autoreleasepool
    {
        if (url.rfind("https://", 0) == 0)
            if (NSURL *u = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]])
                [[NSWorkspace sharedWorkspace] openURL:u];
    }
}
} // namespace tcgui
