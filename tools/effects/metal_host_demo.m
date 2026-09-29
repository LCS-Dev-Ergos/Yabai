// Synthetic CAMetalLayer hosting demonstration. It opens an ordinary NSWindow
// only when explicitly run; it does not capture the screen or integrate with
// Yabai's SkyLight auxiliary Space/ordering path.
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#import <CoreVideo/CoreVideo.h>
#import <IOSurface/IOSurface.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
    @autoreleasepool {
        const size_t width = 320, height = 200;
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!device || !queue) return 1;
        NSDictionary *attributes = @{
            (id)kCVPixelBufferMetalCompatibilityKey: @YES,
            (id)kCVPixelBufferIOSurfacePropertiesKey: @{}
        };
        CVPixelBufferRef pixels = NULL;
        if (CVPixelBufferCreate(kCFAllocatorDefault, width, height, kCVPixelFormatType_32BGRA,
                                (__bridge CFDictionaryRef)attributes, &pixels) != kCVReturnSuccess) return 1;
        if (CVPixelBufferLockBaseAddress(pixels, 0) != kCVReturnSuccess) {
            CVPixelBufferRelease(pixels);
            return 1;
        }
        uint8_t *base = CVPixelBufferGetBaseAddress(pixels);
        size_t row = CVPixelBufferGetBytesPerRow(pixels);
        for (size_t y = 0; y < height; ++y) for (size_t x = 0; x < width; ++x) {
            uint8_t *pixel = base + y * row + x * 4;
            pixel[0] = (uint8_t)(x * 255 / width);
            pixel[1] = (uint8_t)(y * 255 / height);
            pixel[2] = 0x60;
            pixel[3] = 0xff;
        }
        CVPixelBufferUnlockBaseAddress(pixels, 0);

        CVMetalTextureCacheRef cache = NULL;
        CVMetalTextureRef wrapped = NULL;
        if (CVMetalTextureCacheCreate(kCFAllocatorDefault, NULL, device, NULL, &cache) != kCVReturnSuccess ||
            CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, cache, pixels, NULL,
                MTLPixelFormatBGRA8Unorm, width, height, 0, &wrapped) != kCVReturnSuccess) {
            if (wrapped) CFRelease(wrapped);
            if (cache) CFRelease(cache);
            CVPixelBufferRelease(pixels);
            return 1;
        }

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, width, height)
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
            backing:NSBackingStoreBuffered defer:NO];
        window.title = @"Yabai Metal hosting probe — synthetic pixels";
        window.releasedWhenClosed = NO;
        CAMetalLayer *layer = [CAMetalLayer layer];
        layer.device = device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = NO; // A blit encoder must write the drawable.
        layer.drawableSize = CGSizeMake(width, height);
        CGColorSpaceRef sRGB = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        layer.colorspace = sRGB;
        CGColorSpaceRelease(sRGB);
        window.contentView.wantsLayer = YES;
        window.contentView.layer = layer;
        [window orderFront:nil];

        id<CAMetalDrawable> drawable = [layer nextDrawable];
        id<MTLTexture> source = CVMetalTextureGetTexture(wrapped);
        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
        if (!drawable || !source || !command || !blit) {
            if (blit) [blit endEncoding];
            [window close];
            CFRelease(wrapped); CFRelease(cache); CVPixelBufferRelease(pixels);
            return 1;
        }
        MTLOrigin zero = {0, 0, 0};
        MTLSize size = {width, height, 1};
        [blit copyFromTexture:source sourceSlice:0 sourceLevel:0 sourceOrigin:zero sourceSize:size
                   toTexture:drawable.texture destinationSlice:0 destinationLevel:0 destinationOrigin:zero];
        [blit endEncoding];
        dispatch_semaphore_t completed = dispatch_semaphore_create(0);
        [command addCompletedHandler:^(id<MTLCommandBuffer> finished) {
            (void)finished;
            dispatch_semaphore_signal(completed);
        }];
        [command presentDrawable:drawable];
        [command commit];
        bool finished = dispatch_semaphore_wait(completed, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC)) == 0;
        // A timed-out command may still read the source IOSurface. End this
        // one-shot process without releasing its resources underneath it.
        if (!finished) exit(2);
        bool gpu_ok = finished && command.status == MTLCommandBufferStatusCompleted;
        printf("{\"gpu_complete\":%s,\"hosting\":\"ordinary-nswindow-only\",\"visible_presentation_verified\":false}\n",
               gpu_ok ? "true" : "false");
        if (gpu_ok) [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:2.0]];
        [window close];
        // Keep the source IOSurface alive through the GPU completion boundary.
        CFRelease(wrapped);
        CFRelease(cache);
        CVPixelBufferRelease(pixels);
        return gpu_ok ? 0 : 1;
    }
}
