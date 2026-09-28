// Prints the frontmost process and the window its Accessibility focus is on.
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>

extern AXError _AXUIElementGetWindow(AXUIElementRef element, uint32_t *wid);

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

int main(void)
{
    ProcessSerialNumber psn;
    pid_t pid = 0;
    if (GetFrontProcess(&psn) != noErr || GetProcessPID(&psn, &pid) != noErr) return 1;

    AXUIElementRef application = AXUIElementCreateApplication(pid);
    CFTypeRef window = NULL;
    uint32_t wid = 0;

    if (AXUIElementCopyAttributeValue(application, kAXFocusedWindowAttribute, &window) == kAXErrorSuccess && window) {
        _AXUIElementGetWindow(window, &wid);
        CFRelease(window);
    }

    CFRelease(application);
    printf("%d %u\n", pid, wid);
    return 0;
}
