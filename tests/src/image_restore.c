static CGImageRef test_image_restore_source(CGColorSpaceRef space, const uint32_t *pixels, size_t count)
{
    CGContextRef context = CGBitmapContextCreate(NULL, count, 1, 8, count * 4, space, kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
    if (!context) return NULL;

    memcpy(CGBitmapContextGetData(context), pixels, count * 4);
    CGImageRef image = CGBitmapContextCreateImage(context);
    CGContextRelease(context);
    return image;
}

// The restored image keeps the capture's colour space and pixel layout, so that
// neither its own draw nor the proxy's converts colours.
TEST_FUNC(cgimage_restore_alpha_colour_space,
{
    const uint32_t pixels[5] = { 0x80402010, 0xff336699, 0x00000000, 0x80402010, 0xff000000 };
    const uint32_t expected[5] = { 0xff804020, 0xff336699, 0x00000000, 0xff804020, 0xff000000 };

    CGColorSpaceRef p3 = CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);
    CGImageRef image = p3 ? test_image_restore_source(p3, pixels, array_count(pixels)) : NULL;
    CGImageRef restored = image ? cgimage_restore_alpha(image) : NULL;
    TEST_CHECK(restored != NULL, true);

    if (restored) {
        TEST_CHECK(CFEqual(CGImageGetColorSpace(restored), p3), true);
        TEST_CHECK(CGImageGetBitmapInfo(restored), kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);

        CFDataRef data = CGDataProviderCopyData(CGImageGetDataProvider(restored));
        TEST_CHECK(data && CFDataGetLength(data) >= (CFIndex) sizeof(expected), true);
        if (data && CFDataGetLength(data) >= (CFIndex) sizeof(expected)) {
            const uint32_t *restored_pixels = (const uint32_t *) CFDataGetBytePtr(data);
            for (size_t i = 0; i < array_count(expected); ++i) TEST_CHECK(restored_pixels[i], expected[i]);
        }
        if (data) CFRelease(data);
        CGImageRelease(restored);
    }

    if (image) CGImageRelease(image);
    if (p3) CGColorSpaceRelease(p3);

    // A space a bitmap cannot be drawn in falls back to device RGB.
    const uint8_t gray[2] = { 0x40, 0xc0 };
    CGColorSpaceRef gray_space = CGColorSpaceCreateDeviceGray();
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, gray, sizeof(gray), NULL);
    CGImageRef gray_image = gray_space && provider
                          ? CGImageCreate(2, 1, 8, 8, 2, gray_space, (CGBitmapInfo) kCGImageAlphaNone, provider, NULL, false, kCGRenderingIntentDefault)
                          : NULL;
    CGImageRef gray_restored = gray_image ? cgimage_restore_alpha(gray_image) : NULL;
    TEST_CHECK(gray_restored != NULL, true);
    if (gray_restored) {
        TEST_CHECK(CGColorSpaceGetModel(CGImageGetColorSpace(gray_restored)), kCGColorSpaceModelRGB);
        CGImageRelease(gray_restored);
    }

    if (gray_image) CGImageRelease(gray_image);
    if (provider) CGDataProviderRelease(provider);
    if (gray_space) CGColorSpaceRelease(gray_space);
});
