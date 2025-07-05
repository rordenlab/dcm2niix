# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

dcm2niix is a C++ tool that converts neuroimaging data from DICOM format to NIfTI format. The project generates BIDS-compliant JSON sidecars and supports multiple output formats including NIfTI, NRRD, and MGH.

## Build System

### Primary Commands
- **Build (Make)**: `cd console && make` - Basic build using the makefile
- **Build (CMake)**: `mkdir build && cd build && cmake .. && make` - Standard CMake build
- **Build with features**: `cmake -DZLIB_IMPLEMENTATION=Cloudflare -DUSE_JPEGLS=ON -DUSE_OPENJPEG=ON .. && make`
- **Clean**: `make clean` (in console dir) or `rm -rf build/` (for CMake)

### Testing
No specific test framework is documented in the project. Manual testing is done by converting sample DICOM files.

### Special Build Variants (Make)
- `make debug` - Unoptimized build for debugging
- `make jp2` - Build with JPEG2000 support
- `make sanitize` - Build with AddressSanitizer for memory error detection
- `JPEGLS=1 make` - Add CharLS library support
- `ZLIB=1 make` - Use system zlib instead of bundled miniz

## Architecture & Key Files

### Main Executables
- **dcm2niix**: Primary DICOM to NIfTI converter (`console/main_console.cpp`)
- **dcm2niibatch**: Batch processing version using YAML configs (`console/main_console_batch.cpp`)

### Core Processing Files
- `nii_dicom.cpp/.h` - Main DICOM parsing and NIfTI conversion logic
- `nii_dicom_batch.cpp/.h` - Batch processing and directory handling  
- `nifti1_io_core.cpp/.h` - NIfTI file I/O operations
- `nii_foreign.cpp/.h` - Support for non-DICOM formats (PAR/REC, etc.)
- `nii_ortho.cpp/.h` - 3D image cropping and orientation handling

### Image Format Support
- `jpg_0XC3.cpp/.h` - JPEG lossless decompression (0XC3 transfer syntax)
- `ujpeg.cpp/.h` - Lossy JPEG support via NanoJPEG
- `charls/` directory - JPEG-LS support (optional, enabled with `-DmyEnableJPEGLS`)
- OpenJPEG integration for JPEG2000 (optional, enabled with `-DUSE_OPENJPEG=ON`)

### Configuration Management
- `TDCMopts` struct in `nii_dicom.h` - Central configuration structure
- Settings saved/loaded via registry (Windows) or preferences file (Unix)
- YAML batch configuration support (`batch_config.yml`)

## Adding New CLI Arguments

CLI arguments are processed in `console/main_console.cpp:257-572`. To add a new option:

1. **Add to TDCMopts struct** in `nii_dicom.h` - This is the central configuration structure
2. **Add parsing logic** in `main_console.cpp` around lines 257-572 in the main argument parsing loop
3. **Add help text** in `showHelp()` function around lines 49-155
4. **Set default value** in the default initialization (typically in `readIniFile()`)
5. **Use the option** in the core processing code (`nii_dicom.cpp` or related files)

### CLI Argument Patterns
- Single letter options: `-f filename`, `-o outdir`, `-z y`
- Boolean flags: typically `y/n` or `0/1/2` for tri-state
- Extended options: `--version`, `--big-endian`, `--progress`
- Parameter validation via `invalidParam()` function

### Example Option Types
- **String**: `-f %p_%s` (filename pattern), `-c "comment"` (image comments)
- **Boolean**: `-b y` (BIDS), `-i y` (ignore derived), `-z y` (compress)
- **Numeric**: `-d 5` (directory depth), `-1` to `-9` (compression level)
- **Multi-value**: `-n 3 -n 7` (series numbers to convert)

## Core Data Structures

### TDCMopts Structure
Central configuration stored in `nii_dicom.h`. Key fields include:
- `filename[kOptsStr]` - Output filename pattern with % placeholders
- `outdir[kOptsStr]` - Output directory path
- `indir[kOptsStr]` - Input directory path
- Boolean flags: `isGz`, `isCreateBIDS`, `isVerbose`, `isFlipY`, etc.
- `seriesNumber[MAX_NUM_SERIES]` - Array of series numbers to convert

### Filename Pattern Tokens
The `-f` option supports tokens like `%p` (protocol), `%s` (series), `%t` (time), etc. These are processed in the core conversion functions.

## Optional Dependencies

### Image Compression Libraries
- **ZLIB**: Controlled by `ZLIB_IMPLEMENTATION` (Miniz/System/Cloudflare)
- **CharLS**: JPEG-LS support via `-DUSE_JPEGLS=ON` or `JPEGLS=1 make`
- **OpenJPEG**: JPEG2000 support via `-DUSE_OPENJPEG=ON` 
- **TurboJPEG**: Alternative JPEG decoder via `-DUSE_TURBOJPEG=ON`

### Build-time Flags
- `myDisableOpenJPEG` - Disable JPEG2000 support
- `myEnableJPEGLS` - Enable JPEG-LS support
- `myTurboJPEG` - Use TurboJPEG instead of NanoJPEG
- `myEnableJNIFTI` - Enable JSON NIfTI support (default ON)

## File Organization

### Console Directory Structure
- `main_console.cpp` - CLI entry point and argument parsing
- `main_console_batch.cpp` - Batch processing entry point
- `makefile` - Simple make-based build
- `CMakeLists.txt` - CMake configuration
- Core `.cpp/.h` files for image processing
- `charls/` - JPEG-LS library source (optional)

### Root Directory
- `README.md` - Primary documentation
- `COMPILE.md` - Detailed build instructions
- `BATCH.md` - Batch processing documentation
- Vendor-specific documentation in `Siemens/`, `GE/`, `Philips/` etc.
- `js/` - WebAssembly build support

## Development Notes

- The codebase uses C-style structs and functions for the core processing
- Memory management follows traditional C patterns with careful buffer handling
- Cross-platform compatibility (Windows/macOS/Linux) with platform-specific code sections
- Large stack allocation requirements (16MB) due to extensive DICOM metadata processing
- Extensive DICOM vendor-specific handling for different scanner manufacturers