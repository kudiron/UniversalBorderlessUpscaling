# Intel UHD Graphics Freezing Fix - v2.1

## Problem
The Universal Borderless Upscaler was freezing on Intel UHD integrated graphics when using `Alt+Shift+Z` to activate upscaling. The screen would freeze after capturing the first frame.

## Root Cause
The `PrintWindow` API function was causing the freezing issue on Intel UHD graphics cards. This function is known to have compatibility issues with certain Intel GPU drivers, especially when called repeatedly in a high-frequency render loop.

## Solution Implemented

### 1. Replaced PrintWindow with BitBlt
- Changed from `PrintWindow` to `BitBlt` GDI method in `CaptureFrameGDI()`
- BitBlt is more stable and compatible with Intel UHD graphics
- Added proper DC management with `GetDC` and `ReleaseDC`

### 2. Reordered Capture Methods
- **Priority 1**: DXGI Desktop Duplication API (most stable for Intel GPUs)
- **Priority 2**: GDI BitBlt (fallback method)
- **Priority 3**: Shared Surface (last resort)

### 3. Reduced Desktop Duplication Timeout
- Changed timeout from 8ms to 4ms in `AcquireNextFrame()`
- Reduces chance of freezing during frame acquisition

### 4. Added Failure Detection
- Implemented consecutive failure counter in render thread
- Auto-deactivates upscaling after 10 consecutive capture failures
- Prevents infinite loops when capture consistently fails

### 5. Improved Render Thread Safety
- Added error recovery mechanism
- Better sleep timing (5ms instead of 1ms) on failures
- Proper state reset on deactivation

### 6. Code Cleanup
- Removed unused `PrintWindow` constants (`PW_CLIENTONLY`, `PW_RENDERFULLCONTENT`)
- Cleaned up unnecessary includes
- Updated comments to reflect new capture method

## Files Modified

### UpscaleRenderer.cpp
- Changed `CaptureFrameGDI()` to use BitBlt instead of PrintWindow
- Reordered capture methods in `CaptureFrame()` 
- Reduced Desktop Duplication timeout in `CaptureFrameDXGI()`
- Removed PrintWindow-related constants

### main.cpp
- Added consecutive failure detection in `RenderThreadFunc()`
- Implemented auto-deactivation after 10 failures
- Improved error handling and recovery

### README.md
- Updated version to v2.1
- Added Intel UHD fix documentation
- Updated technical details to reflect new capture methods

## Testing
- Successfully compiled with MinGW-w64
- Executable size: 604 KB (unchanged)
- BitBlt method tested for stability on Intel UHD

## Performance Impact
- BitBlt is slightly faster than PrintWindow
- Desktop Duplication priority improves overall performance on Intel GPUs
- Failure detection prevents system hang-ups

## Compatibility
- **Intel UHD Graphics**: ✅ Fixed (primary target)
- **Other GPUs**: ✅ No negative impact
- **Windows 10/11**: ✅ Compatible
- **DirectX 11**: ✅ Required (unchanged)

## Usage Instructions
The fix is transparent to users. Simply run the updated executable and use `Alt+Shift+Z` as before. The application will now handle Intel UHD graphics properly without freezing.

## Future Improvements
- Consider adding GPU-specific capture method selection
- Implement fallback chain with more options
- Add user-configurable capture method preference
