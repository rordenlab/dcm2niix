# Regular use
CFLAGS=-O3 -s -flto

# Common files used for everything
CFILES=main_console.cpp nii_foreign.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp nii_json_meta.cpp -o dcm2niix

# Universal files used for almost everything
UFILES=$(CFILES) -DmyDisableOpenJPEG
# Debugging
#CFLAGS=-g

# issue659: increase stack to 16mb
#For Linux linker stacksize ignored: we must use setrlimit 
# LFLAGS=-Wl,-z -Wl,stack-size=16777216
LFLAGS=


#Leak tests:
# https://clang.llvm.org/docs/AddressSanitizer.html
# clang++ -O1 -g -fsanitize=address -fno-omit-frame-pointer -I.  main_console.cpp nii_foreign.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp base64.c cJSON.c -o dcm2niix -DmyDisableOpenJPEG

#run "ZLIB=1 make" for ZLIB build
ifeq "$(ZLIB)" "1"
	CFILES += -DmyDisableMiniZ -lz
endif

#run "make" for default build
#run "JPEGLS=1 make" for JPEGLS build
JFLAGS=
ifeq "$(JPEGLS)" "1"
	JFLAGS=-std=c++14 -DmyEnableJPEGLS  charls/jpegls.cpp charls/jpegmarkersegment.cpp charls/interface.cpp  charls/jpegstreamwriter.cpp charls/jpegstreamreader.cpp
endif

#run "JNIfTI=0 make" to disable JNIFTI build
JSFLAGS=
ifneq "$(JNIfTI)" "0"
	JSFLAGS=-DmyEnableJNIFTI base64.cpp cJSON.cpp
endif

ifneq ($(OS),Windows_NT)
	OS = $(shell uname)
 	ifeq "$(OS)" "Darwin"
		#CFLAGS=-dead_strip -O3
		#CFLAGS= -O3
		CFLAGS=-O3 -flto -sectcreate __TEXT __info_plist Info.plist
		#Apple notarization requires a Info.plist
		# For .app bundles, the Info.plist is a separate file, for executables it is appended as a section
		#you can check that the Info.plist section has been inserted with either of these commands
		# otool -l ./dcm2niix | grep info_plist -B1 -A10
		# launchctl plist ./dcm2niix
		#MacOS links g++ to clang++, for gcc install via homebrew and replace g++ with /usr/local/bin/gcc-9
		#issue867 reduce stack pressure
		#LFLAGS=-Wl,-stack_size -Wl,0x1000000
	endif
endif
all:
	g++ $(CFLAGS) -I. $(JSFLAGS) $(JFLAGS) $(LFLAGS) $(UFILES)
	@if [ "$$(uname)" = "Darwin" ]; then \
		strip -x dcm2niix; \
	fi

sanitize:
	g++ -O1 -g -fsanitize=address -fno-omit-frame-pointer $(LFLAGS) $(UFILES)
  #  -fstack-usage

debug:
	g++ -O0 $(LFLAGS) $(UFILES)

turbo:
	g++ -O0 $(LFLAGS) $(UFILES) -DmyTurboJPEG -I/opt/homebrew/include -L/opt/homebrew/lib -lturbojpeg

jp2:
	g++ $(CFLAGS) $(LFLAGS) $(CFILES) $(JFLAGS) -I/opt/homebrew/Cellar/openjpeg/2.5.3/include/openjpeg-2.5/ -L/opt/homebrew/Cellar/openjpeg/2.5.3/lib/ -lopenjp2

noroi:
	g++ $(CFLAGS) -I. $(JSFLAGS) $(JFLAGS) $(LFLAGS) $(UFILES) -DmyNoRois

wasm:
	emcc -O3 $(UFILES) -lz -s USE_ZLIB -s DEMANGLE_SUPPORT=1 -s EXPORTED_RUNTIME_METHODS='["callMain", "ccall", "cwrap", "FS", "FS_createDataFile", "FS_readFile", "FS_unlink", "allocateUTF8", "getValue", "stringToUTF8", "setValue"]' -s STACK_OVERFLOW_CHECK=2 -s STACK_SIZE=16MB -s ALLOW_MEMORY_GROWTH=1 -s WASM=1 -s EXPORT_ES6=1 -s MODULARIZE=1 -s EXPORTED_FUNCTIONS='["_main", "_malloc", "_free"]' -s FORCE_FILESYSTEM=1 -s INVOKE_RUN=0 -o ../js/src/dcm2niix.js
	# STACK_SIZE=16MB is the minimum value found to work with the current codebase when targeting WASM

wasm-jpeg:
	emcc -O3 $(JFLAGS) $(CFILES) -DUSE_OPENJPEG -I${PIXI_PROJECT_ROOT}/openjpeg-2.5.3/src/lib/openjp2/ -I${PIXI_PROJECT_ROOT}/openjpeg-2.5.3/build/src/lib/openjp2/ -L${PIXI_PROJECT_ROOT}/openjpeg-2.5.3/build/bin/ -lopenjp2 -lz -s USE_ZLIB -s DEMANGLE_SUPPORT=1 -s EXPORTED_RUNTIME_METHODS='["callMain", "ccall", "cwrap", "FS", "FS_createDataFile", "FS_readFile", "FS_unlink", "allocateUTF8", "getValue", "stringToUTF8", "setValue"]' -s STACK_OVERFLOW_CHECK=2 -s STACK_SIZE=16MB -s ALLOW_MEMORY_GROWTH=1 -s WASM=1 -s EXPORT_ES6=1 -s MODULARIZE=1 -s EXPORTED_FUNCTIONS='["_main", "_malloc", "_free"]' -s FORCE_FILESYSTEM=1 -s INVOKE_RUN=0 -o ../js/src/dcm2niix.jpeg.js
	# STACK_SIZE=16MB is the minimum value found to work with the current codebase when targeting WASM

