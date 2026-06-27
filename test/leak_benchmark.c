#include <CoreFoundation/CoreFoundation.h>
#include <CoreVideo/CoreVideo.h>
#include <CoreMedia/CoreMedia.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// Old leaking code simulation
int hb_cv_match_rgb_to_colorspace_leaking(int rgb, int color_prim, int color_transfer, int color_matrix)
{
    const unsigned r = (rgb >> 16) & 0xff;
    const unsigned g = (rgb >> 8) & 0xff;
    const unsigned b = (rgb) & 0xff;
    
    CGColorRef rgb_color = CGColorCreateSRGB(r / 255.f, g / 255.f, b / 255.f, 1.f);
    
    float g_val = 2.2f;
    CFNumberRef gamma = CFNumberCreate(NULL, kCFNumberFloat32Type, &g_val); // Retained, leaked
    
    CFMutableDictionaryRef attachments = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attachments, kCVImageBufferColorPrimariesKey, kCVImageBufferColorPrimaries_ITU_R_709_2);
    
    CGColorSpaceRef colorspace = CVImageBufferCreateColorSpaceFromAttachments(attachments);
    if (colorspace == NULL)
    {
        return rgb; // Leaks: rgb_color, gamma, attachments
    }
    
    CGColorRef matched_color = CGColorCreateCopyByMatchingToColorSpace(colorspace, kCGRenderingIntentPerceptual, rgb_color, NULL);
    if (matched_color == NULL)
    {
        return rgb; // Leaks: rgb_color, gamma, attachments, colorspace
    }
    
    const CGFloat *components = CGColorGetComponents(matched_color);
    int color = ((int)(components[0] * 255) << 16) | ((int)(components[1] * 255) << 8) | (int)(components[2] * 255);
    return color; // Leaks: rgb_color, gamma, attachments, colorspace, matched_color
}

// New fixed code
int hb_cv_match_rgb_to_colorspace_fixed(int rgb, int color_prim, int color_transfer, int color_matrix)
{
    const unsigned r = (rgb >> 16) & 0xff;
    const unsigned g = (rgb >> 8) & 0xff;
    const unsigned b = (rgb) & 0xff;
    
    CGColorRef rgb_color = CGColorCreateSRGB(r / 255.f, g / 255.f, b / 255.f, 1.f);
    
    float g_val = 2.2f;
    CFNumberRef gamma = CFNumberCreate(NULL, kCFNumberFloat32Type, &g_val);
    
    CFMutableDictionaryRef attachments = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attachments, kCVImageBufferColorPrimariesKey, kCVImageBufferColorPrimaries_ITU_R_709_2);
    
    CGColorSpaceRef colorspace = CVImageBufferCreateColorSpaceFromAttachments(attachments);
    CFRelease(attachments);
    
    if (gamma != NULL)
    {
        CFRelease(gamma);
    }
    
    if (colorspace == NULL)
    {
        CFRelease(rgb_color);
        return rgb;
    }
    
    CGColorRef matched_color = CGColorCreateCopyByMatchingToColorSpace(colorspace, kCGRenderingIntentPerceptual, rgb_color, NULL);
    CFRelease(colorspace);
    CFRelease(rgb_color);
    
    if (matched_color == NULL)
    {
        return rgb;
    }
    
    const CGFloat *components = CGColorGetComponents(matched_color);
    int color = ((int)(components[0] * 255) << 16) | ((int)(components[1] * 255) << 8) | (int)(components[2] * 255);
    CFRelease(matched_color);
    return color;
}

uint64_t get_memory_footprint()
{
    task_vm_info_data_t vm_info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vm_info, &count) == KERN_SUCCESS)
    {
        return vm_info.phys_footprint;
    }
    return 0;
}

int main()
{
    printf("=== MEMORY LEAK BENCHMARK REPORT ===\n");
    
    // Benchmark Old Leaking Code
    uint64_t start_mem_old = get_memory_footprint();
    for (int i = 0; i < 5000; i++)
    {
        hb_cv_match_rgb_to_colorspace_leaking(0xFF0000, 1, 1, 1);
    }
    uint64_t end_mem_old = get_memory_footprint();
    int64_t diff_old = end_mem_old - start_mem_old;
    
    // Benchmark New Fixed Code
    uint64_t start_mem_new = get_memory_footprint();
    for (int i = 0; i < 5000; i++)
    {
        hb_cv_match_rgb_to_colorspace_fixed(0xFF0000, 1, 1, 1);
    }
    uint64_t end_mem_new = get_memory_footprint();
    int64_t diff_new = end_mem_new - start_mem_new;
    
    printf("OLD CODE (LEAKING) MEMORY BEFORE: %llu bytes\n", start_mem_old);
    printf("OLD CODE (LEAKING) MEMORY AFTER:  %llu bytes\n", end_mem_old);
    printf("OLD CODE (LEAKING) LEAK RATE:    %lld bytes\n", diff_old);
    printf("------------------------------------\n");
    printf("NEW CODE (FIXED)   MEMORY BEFORE: %llu bytes\n", start_mem_new);
    printf("NEW CODE (FIXED)   MEMORY AFTER:  %llu bytes\n", end_mem_new);
    printf("NEW CODE (FIXED)   LEAK RATE:    %lld bytes\n", diff_new);
    printf("====================================\n");
    
    return 0;
}
