//main_console.cpp dcm2niix
// by Chris Rorden on 3/22/14, see license.txt
// Copyright (c) 2014-2021 Chris Rorden. All rights reserved.

//g++ -O3 main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp -s -o dcm2niix -lz

//if you do not have zlib,you can compile without it
// g++ -O3 -DmyDisableZLib main_console.cpp nii_dicom.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp -s -o dcm2niix
//or you can build your own copy:
// to compile you will first want to build the Z library, then compile the project
// cd zlib-1.2.8
// sudo ./configure;
// sudo make

//to generate combined 32-bit and 64-bit builds for OSX :
// g++ -O3 -x c++ main_console.c nii_dicom.c nifti1_io_core.c nii_ortho.c nii_dicom_batch.c -s -arch x86_64 -o dcm2niix64 -lz
// g++ -O3 -x c++ main_console.c nii_dicom.c nifti1_io_core.c nii_ortho.c nii_dicom_batch.c -s -arch i386 -o dcm2niix32 -lz
// lipo -create dcm2niix32 dcm2niix64 -o dcm2niix

//On windows with mingw you may get "fatal error: zlib.h: No such file
// to remedy, run "mingw-get install libz-dev" from mingw

//Alternatively, windows users with VisualStudio can compile this project
// vcvarsall amd64
// cl /EHsc main_console.cpp nii_foreign.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp -DmyDisableOpenJPEG /o dcm2niix

//#define mydebugtest //automatically process directory specified in main, ignore input arguments

#include <ctype.h>
#include <float.h>
#include <stdbool.h> //requires VS 2015 or later
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
//#include <unistd.h>
#include "nifti1_io_core.h"
#include "nii_dicom.h"
#include "nii_dicom_batch.h"
#include <math.h>
#include <stdio.h>
#include <time.h> // clock_t, clock, CLOCKS_PER_SEC

#if !defined(_WIN64) && !defined(_WIN32)
#include <sys/time.h>
#include <time.h>
double get_wall_time() {
	struct timeval time;
	if (gettimeofday(&time, NULL)) {
		// Handle error
		return 0;
	}
	return (double)time.tv_sec + (double)time.tv_usec * .000001;
}
#endif

const char *removePath(const char *path) { // "/usr/path/filename.exe" -> "filename.exe"
	const char *pDelimeter = strrchr(path, '\\');
	if (pDelimeter)
		path = pDelimeter + 1;
	pDelimeter = strrchr(path, '/');
	if (pDelimeter)
		path = pDelimeter + 1;
	return path;
} //removePath()

char bool2Char(bool b) {
	if (b)
		return ('y');
	return ('n');
}

void showHelp(const char *argv[], struct TDCMopts opts) {
	const char *cstr = removePath(argv[0]);
	printf("usage: %s [options] <in_folder>\n", cstr);
	printf(" Options :\n");
	printf("  -1..-9 : gz compression level (1=fastest..9=smallest, default %d)\n", opts.gzLevel);
	printf("  -a : adjacent DICOMs (images from same series always in same folder) for faster conversion (n/y, default n)\n");
	printf("  -b : BIDS sidecar (y/n/o [o=only: no NIfTI], default %c)\n", bool2Char(opts.isCreateBIDS));
	printf("   -ba : anonymize BIDS (y/n, default %c)\n", bool2Char(opts.isAnonymizeBIDS));
	printf("  -c : comment stored in NIfTI aux_file (up to 24 characters e.g. '-c VIP', empty to anonymize e.g. 0020,4000 e.g. '-c \"\"')\n");
	printf("  -d : directory search depth. Convert DICOMs in sub-folders of in_folder? (0..9, default %d)\n", opts.dirSearchDepth);
#ifdef myEnableJNIFTI
	printf("  -e : export as NRRD (y) or MGH (o) or JSON/JNIfTI (j) or BJNIfTI (b) instead of NIfTI (y/n/o/j/b, default n)\n");
#else
	printf("  -e : export as NRRD (y) or MGH (o) instead of NIfTI (y/n/o/j/b, default n)\n");
#endif
#ifdef mySegmentByAcq
#define kQstr " %q=sequence number,"
#else
#define kQstr ""
#endif
	printf("  -f : filename (%%a=antenna (coil) name, %%b=basename, %%c=comments, %%d=description, %%e=echo number, %%f=folder name, %%g=accession number, %%i=ID of patient, %%j=seriesInstanceUID, %%k=studyInstanceUID, %%m=manufacturer, %%n=name of patient, %%o=mediaObjectInstanceUID, %%p=protocol,%s %%r=instance number, %%s=series number, %%t=time, %%u=acquisition number, %%v=vendor, %%x=study ID; %%z=sequence name; default '%s')\n", kQstr, opts.filename);
	printf("  -g : generate defaults file (y/n/o/i [o=only: reset and write defaults; i=ignore: reset defaults], default n)\n");
	printf("  -h : show help\n");
	printf("  -i : ignore derived, localizer and 2D images (y/n, default n)\n");
	char max16Ch = 'n';
	if (opts.isMaximize16BitRange == kMaximize16BitRange_True)
		max16Ch = 'y';
	if (opts.isMaximize16BitRange == kMaximize16BitRange_Raw)
		max16Ch = 'o';
	printf("  -l : losslessly scale 16-bit integers to use dynamic range (y/n/o [yes=scale, no=no, but uint16->int16, o=original], default %c)\n", max16Ch);
	printf("  -m : merge 2D slices from same series regardless of echo, exposure, etc. (n/y or 0/1/2, default 2) [no, yes, auto]\n");
	printf("  -n : only convert this series CRC number - can be used up to %i times (default convert all)\n", MAX_NUM_SERIES);
	printf("  -o : output directory (omit to save to input folder)\n");
	printf("  -p : Philips precise float (not display) scaling (y/n, default y)\n");
	printf("  -q : only search directory for DICOMs (y/l/n, default y) [y=show number of DICOMs found, l=additionally list DICOMs found, n=no]\n");
	printf("  -r : rename instead of convert DICOMs (y/n, default n)\n");
	printf("  -s : single file mode, do not convert other images in folder (y/n, default n)\n");
//text notes replaced with BIDS: this function is deprecated
//printf("  -t : text notes includes private patient details (y/n, default n)\n");
#if !defined(_WIN64) && !defined(_WIN32) //shell script for Unix only
	printf("  -u : up-to-date check\n");
#endif
	printf("  -v : verbose (n/y or 0/1/2, default 0) [no, yes, logorrheic]\n");
	//#define kNAME_CONFLICT_SKIP 0 //0 = write nothing for a file that exists with desired name
	//#define kNAME_CONFLICT_OVERWRITE 1 //1 = overwrite existing file with same name
	//#define kNAME_CONFLICT_ADD_SUFFIX 2 //default 2 = write with new suffix as a new file
	printf("  -w : write behavior for name conflicts (0,1,2, default 2: 0=skip duplicates, 1=overwrite, 2=add suffix)\n");
	printf("  -x : crop 3D acquisitions (y/n/i, default n, use 'i'gnore to neither crop nor rotate 3D acquisitions)\n");
	char gzCh = 'n';
	if (opts.isGz)
		gzCh = 'y';
#if defined(_WIN64) || defined(_WIN32)
//n.b. the optimal use of pigz requires pipes that are not provided for Windows
#ifdef myDisableZLib
	if (strlen(opts.pigzname) > 0)
		printf("  -z : gz compress images (y/n/3, default %c) [y=pigz, n=no, 3=no,3D]\n", gzCh);
	else
		printf("  -z : gz compress images (y/n/3, default %c)  [y=pigz(MISSING!), n=no, 3=no,3D]\n", gzCh);
#else
#ifdef myDisableMiniZ
	printf("  -z : gz compress images (y/i/n/3, default %c) [y=pigz, i=internal:zlib, n=no, 3=no,3D]\n", gzCh);
#else
	printf("  -z : gz compress images (y/i/n/3, default %c) [y=pigz, i=internal:miniz, n=no, 3=no,3D]\n", gzCh);
#endif
#endif
	printf("  --big-endian : byte order (y/n/o, default o) [y=big-end, n=little-end, o=optimal/native]\n");
	printf("  --progress : report progress (y/n, default n)\n");
	printf("  --ignore_trigger_times : disregard values in 0018,1060 and 0020,9153\n");
	printf("  --terse : omit filename post-fixes (can cause overwrites)\n");
	printf("  --version : report version\n");
	printf("  --xml : Slicer format features\n");
	printf(" Defaults stored in Windows registry\n");
	printf(" Examples :\n");
	printf("  %s c:\\DICOM\\dir\n", cstr);
	printf("  %s -c \"my comment\" c:\\DICOM\\dir\n", cstr);
	printf("  %s -o c:\\out\\dir c:\\DICOM\\dir\n", cstr);
	printf("  %s -f mystudy%%s c:\\DICOM\\dir\n", cstr);
	printf("  %s -o \"c:\\dir with spaces\\dir\" c:\\dicomdir\n", cstr);
#else
#ifdef myDisableZLib
	if (strlen(opts.pigzname) > 0)
		printf("  -z : gz compress images (y/o/n/3, default %c) [y=pigz, o=optimal pigz, n=no, 3=no,3D]\n", gzCh);
	else
		printf("  -z : gz compress images (y/o/n/3, default %c)  [y=pigz(MISSING!), o=optimal(requires pigz), n=no, 3=no,3D]\n", gzCh);
#else
#ifdef myDisableMiniZ
	printf("  -z : gz compress images (y/o/i/n/3, default %c) [y=pigz, o=optimal pigz, i=internal:zlib, n=no, 3=no,3D]\n", gzCh);
#else
	printf("  -z : gz compress images (y/o/i/n/3, default %c) [y=pigz, o=optimal pigz, i=internal:miniz, n=no, 3=no,3D]\n", gzCh);
#endif
#endif
	printf("  --big-endian : byte order (y/n/o, default o) [y=big-end, n=little-end, o=optimal/native]\n");
	printf("  --progress : Slicer format progress information (y/n, default n)\n");
	printf("  --ignore_trigger_times : disregard values in 0018,1060 and 0020,9153\n");
	printf("  --terse : omit filename post-fixes (can cause overwrites)\n");
	printf("  --version : report version\n");
	printf("  --xml : Slicer format features\n");
	printf(" Defaults file : %s\n", opts.optsname);
	printf(" Examples :\n");
	printf("  %s /Users/chris/dir\n", cstr);
	printf("  %s -c \"my comment\" /Users/chris/dir\n", cstr);
	printf("  %s -o /users/cr/outdir/ -z y ~/dicomdir\n", cstr);
	printf("  %s -f %%p_%%s -b y -ba n ~/dicomdir\n", cstr);
	printf("  %s -f mystudy%%s ~/dicomdir\n", cstr);
	printf("  %s -o \"~/dir with spaces/dir\" ~/dicomdir\n", cstr);
#endif
} //showHelp()

int invalidParam(int i, const char *argv[]) {
	if (strchr("yYnNoOhHiIjlLJBb01234",argv[i][0]))
		return 0;

	//if (argv[i][0] != '-') return 0;
	printf(" Error: invalid option '%s %s'\n", argv[i - 1], argv[i]);
	return 1;
}

#if !defined(_WIN64) && !defined(_WIN32) //shell script for Unix only

int checkUpToDate() {
#define URL "/rordenlab/dcm2niix/releases/"
#define APIURL "\"https://api.github.com/repos" URL "latest\""
#define HTMURL "https://github.com" URL
#define SHELLSCRIPT "#!/usr/bin/env bash\n curl --silent " APIURL " | grep '\"tag_name\":' | sed -E 's/.*\"([^\"]+)\".*/\\1/'"
//check first 13 characters, e.g. "v1.0.20171204"
#define versionChars 13
	FILE *pipe = popen(SHELLSCRIPT, "r");
	char ch, gitvers[versionChars + 1];
	int n = 0;
	int nMatch = 0;
	while ((ch = fgetc(pipe)) != EOF) {
		if (n < versionChars) {
			gitvers[n] = ch;
			if (gitvers[n] == kDCMvers[n])
				nMatch++;
			n++;
		}
	}
	pclose(pipe);
	gitvers[n] = 0; //null terminate
	if (n < 1) {	//script reported nothing
		printf("Error: unable to check version with script:\n %s\n", SHELLSCRIPT);
		return 3; //different from EXIT_SUCCESS (0) and EXIT_FAILURE (1)
	}
	if (nMatch == versionChars) { //versions match
		printf("Good news: Your version is up to date: %s\n", gitvers);
		return EXIT_SUCCESS;
	}
	//report error
	char myvers[versionChars + 1];
	for (int i = 0; i < versionChars; i++)
		myvers[i] = kDCMvers[i];
	myvers[versionChars] = 0; //null terminate
	int myv = atoi(myvers + 5); //skip "v1.0."
	int gitv = atoi(gitvers + 5); //skip "v1.0."
	if (myv > gitv) {
		printf("Warning: your version ('%s') more recent than stable release ('%s')\n %s\n", myvers, gitvers, HTMURL);
		return 2; //different from EXIT_SUCCESS (0) and EXIT_FAILURE (1)
	}
	printf("Error: your version ('%s') is not the latest release ('%s')\n %s\n", myvers, gitvers, HTMURL);
	return EXIT_FAILURE;
} //checkUpToDate()

#endif //shell script for UNIX only

void showXML() {
	//https://www.slicer.org/wiki/Documentation/Nightly/Developers/SlicerExecutionModel#XML_Schema
	printf("<?xml version="
		   "1.0"
		   " encoding="
		   "utf-8"
		   "?>\n");
	printf("<executable>\n");
	printf("<title>dcm2niix</title>\n");
	printf("<description>DICOM importer</description>\n");
	printf("  <parameters>\n");
	printf("    At least one parameter\n");
	printf("  </parameters>\n");
	printf("</executable>\n");
}

//#define mydebugtest
int main(int argc, const char *argv[]) {
	struct TDCMopts opts;
	bool isSaveIni = false;
	bool isOutNameSpecified = false;
	bool isResetDefaults = false;
	readIniFile(&opts, argv); //set default preferences
#ifdef mydebugtest
	//strcpy(opts.indir, "/Users/rorden/desktop/sliceOrder/dicom2/Philips_PARREC_Rotation/NoRotation/DBIEX_4_1.PAR");
	//strcpy(opts.indir, "/Users/rorden/desktop/sliceOrder/dicom2/test");
	strcpy(opts.indir, "e:\\t1s");
#else
#if defined(__APPLE__)
#define kOS "MacOS"
#elif (defined(__linux) || defined(__linux__))
#define kOS "Linux"
#else
#define kOS "Windows"
#endif
	printf("Chris Rorden's dcm2niiX version %s (%llu-bit %s)\n", kDCMvers, (unsigned long long)sizeof(size_t) * 8, kOS);
	if (argc < 2) {
		showHelp(argv, opts);
		return 0;
	}
	//for (int i = 1; i < argc; i++) { printf(" argument %d= '%s'\n", i, argv[i]);}
	int i = 1;
	int lastCommandArg = 0;
	while (i < (argc)) {									//-1 as final parameter is DICOM directory
		if ((strlen(argv[i]) > 1) && (argv[i][0] == '-')) { //command
			if (argv[i][1] == 'h') {
				showHelp(argv, opts);
			} else if ((!strcmp(argv[i], "--big-endian")) && ((i + 1) < argc)) {
				i++;
				if ((littleEndianPlatform()) && ((argv[i][0] == 'y') || (argv[i][0] == 'Y'))) {
					opts.isSaveNativeEndian = false;
					printf("NIfTI data will be big-endian (byte-swapped)\n");
				}
				if ((!littleEndianPlatform()) && ((argv[i][0] == 'n') || (argv[i][0] == 'N'))) {
					opts.isSaveNativeEndian = false;
					printf("NIfTI data will be little-endian\n");
				}
			} else if (!strcmp(argv[i], "--ignore_trigger_times")) {
				opts.isIgnoreTriggerTimes = true;
				printf("ignore_trigger_times may have unintended consequences (issue 499)\n");
			} else if (!strcmp(argv[i], "--terse")) {
				opts.isAddNamePostFixes = false;
			} else if (!strcmp(argv[i], "--version")) {
				printf("%s\n", kDCMdate);
				return kEXIT_REPORT_VERSION;
			} else if ((!strcmp(argv[i], "--progress")) && ((i + 1) < argc)) {
				i++;
				if ((argv[i][0] == 'n') || (argv[i][0] == 'N') || (argv[i][0] == '0'))
					opts.isProgress = 0;
				else
					opts.isProgress = 1;
				if (argv[i][0] == '2')
					opts.isProgress = 2; //logorrheic
			} else if (!strcmp(argv[i], "--xml")) {
				showXML();
				return EXIT_SUCCESS;
			} else if ((argv[i][1] == 'a') && ((i + 1) < argc)) { //adjacent DICOMs
				i++;
				if (invalidParam(i, argv))
					return 0;
				if ((argv[i][0] == 'n') || (argv[i][0] == 'N') || (argv[i][0] == '0'))
					opts.isOneDirAtATime = false;
				else
					opts.isOneDirAtATime = true;
			} else if ((argv[i][1] >= '1') && (argv[i][1] <= '9')) {
				opts.gzLevel = abs((int)strtol(argv[i], NULL, 10));
				if (opts.gzLevel > 11)
					opts.gzLevel = 11;
			} else if ((argv[i][1] == 'b') && ((i + 1) < argc)) {
				if (strlen(argv[i]) < 3) { //"-b y"
					i++;
					if (invalidParam(i, argv))
						return 0;
					if ((argv[i][0] == 'n') || (argv[i][0] == 'N') || (argv[i][0] == '0'))
						opts.isCreateBIDS = false;
					else if ((argv[i][0] == 'i') || (argv[i][0] == 'I')) {
						//input only mode (for development): does not create NIfTI or BIDS outputs!
						opts.isCreateBIDS = false;
						opts.isOnlyBIDS = true;
					} else {
						opts.isCreateBIDS = true;
						if ((argv[i][0] == 'o') || (argv[i][0] == 'O'))
							opts.isOnlyBIDS = true;
					}
				} else if (argv[i][2] == 'a') { //"-ba y"
					i++;
					if (invalidParam(i, argv))
						return 0;
					if ((argv[i][0] == 'n') || (argv[i][0] == 'N') || (argv[i][0] == '0'))
						opts.isAnonymizeBIDS = false;
					else
						opts.isAnonymizeBIDS = true;
				} else if (argv[i][2] == 'i') { //"-bi M2022" provide BIDS subject ID
					i++;
					snprintf(opts.bidsSubject, kOptsStr-1, "%s", argv[i]);
				} else if (argv[i][2] == 'v') { //"-bv 1222" provide BIDS subject visit
					i++;
					snprintf(opts.bidsSession, kOptsStr-1, "%s", argv[i]);
				} else
					printf("Error: Unknown command line argument: '%s'\n", argv[i]);
			} else if ((argv[i][1] == 'c') && ((i + 1) < argc)) {
				i++;
				snprintf(opts.imageComments, 24, "%s", argv[i]);
				if (strlen(opts.imageComments) == 0) //empty string is flag to anonymize DICOM image comments
					snprintf(opts.imageComments, 24, "%s", "\t");
			} else if ((argv[i][1] == 'd') && ((i + 1) < argc)) {
				i++;
				if ((argv[i][0] >= '0') && (argv[i][0] <= '9'))
					opts.dirSearchDepth = abs((int)strtol(argv[i], NULL, 10));
			} else if ((argv[i][1] == 'e') && ((i + 1) < argc)) {
				i++;
				if (invalidParam(i, argv))
					return 0;
				if ((argv[i][0] == 'y') || (argv[i][0] == 'Y') || (argv[i][0] == '1'))
					opts.saveFormat = kSaveFormatNRRD;
				if ((argv[i][0] == 'o') || (argv[i][0] == 'O') || (argv[i][0] == '2'))
					opts.saveFormat = kSaveFormatMGH;
				if ((argv[i][0] == 'j') || (argv[i][0] == 'J') || (argv[i][0] == '3'))
					opts.saveFormat = kSaveFormatJNII;
				if ((argv[i][0] == 'b') || (argv[i][0] == 'B') || (argv[i][0] == '4'))
					opts.saveFormat = kSaveFormatBNII;
				#ifndef myEnableJNIFTI
				if ((opts.saveFormat == kSaveFormatJNII) || (opts.saveFormat == kSaveFormatBNII)) {
					printf("Recompile for JNIfTI support.\n");
					return EXIT_SUCCESS;
				}
				#endif
			} else if ((argv[i][1] == 'g') && ((i + 1) < argc)) {
				i++;
				if (invalidParam(i, argv))
					return 0;
				if ((argv[i][0] == 'y') || (argv[i][0] == 'Y'))
					isSaveIni = true;
				if (((argv[i][0] == 'i') || (argv[i][0] == 'I')) && (!isResetDefaults)) {
					isResetDefaults = true;
					printf("Defaults ignored\n");
					setDefaultOpts(&opts, argv);
					i = 0; //re-read all settings for this pass, e.g. "dcm2niix -f %p_%s -d o" should save filename as "%p_%s"
				}
				if (((argv[i][0] == 'o') || (argv[i][0] == 'O')) && (!isResetDefaults)) {
					//reset defaults - do not read, but do write defaults
					isSaveIni = true;
					isResetDefaults = true;
					printf("Defaults reset\n");
					setDefaultOpts(&opts, argv);
					//this next line is optional, otherwise "dcm2niix -f %p_%s -d o" and "dcm2niix -d o -f %p_%s" will create different results
					i = 0; //re-read all settings for this pass, e.g. "dcm2niix -f %p_%s -d o" should save filename as "%p_%s"
				}
			} else if ((argv[i][1] == 'i') && ((i + 1) < argc)) {
				i++;
				if (invalidParam(i, argv))
					return 0;
				if ((argv[i][0] == 'n') || (argv[i][0] == 'N') || (argv[i][0] == '0'))
					opts.isIgnoreDerivedAnd2D = false;
				else
					opts.isIgnoreDerivedAnd2D = true;
			} else if ((argv[i][1] == 'j') && ((i + 1) < argc)) {
				i++;
				if (invalidParam(i, argv))
					return 0;
				if ((argv[i][0] == 'y') || (argv[i][0] == 'Y') || (argv[i][0] == '1')) {
					opts.isTestx0021x105E = true;
					printf("undocumented '-j y' compares GE slice timing from 0021,105E\n");
				}
			} else if ((!strcmp(argv[i], "--diffCyclingModeGE")) && ((i + 1) < argc)) {
				// see issue 635
				i++;
				if (argv[i][0] == '0') {
					opts.diffCyclingModeGE = 0;
					printf("undocumented '--diffCyclingModeGE 0' cycling OFF\n");
				}
				else if (argv[i][0] == '1') {
					opts.diffCyclingModeGE = 1;
					printf("undocumented '--diffCyclingModeGE 1' cycling All-TR\n");
				}
				else if (argv[i][0] == '2') {
					opts.diffCyclingModeGE = 2;
					printf("undocumented '--diffCyclingModeGE 2' cycling 2-TR\n");
				}
				else if (argv[i][0] == '3') {
					opts.diffCyclingModeGE = 3;
					printf("undocumented '--diffCyclingModeGE 3' cycling 3-TR\n");
				}				
			} else if ((argv[i][1] == 'l') && ((i + 1) < argc)) {
				i++;
				if (invalidParam(i, argv))
					return 0;
				if ((argv[i][0] == 'o') || (argv[i][0] == 'O'))
					opts.isMaximize16BitRange = kMaximize16BitRange_Raw;
				else if ((argv[i][0] == 'n') || (argv[i][0] == 'N') || (argv[i][0] == '0'))
					opts.isMaximize16BitRange = kMaximize16BitRange_False;
				else
					opts.isMaximize16BitRange = kMaximize16BitRange_True;
			} else if ((argv[i][1] == 'm') && ((i + 1) < argc)) {
				i++;
				if (invalidParam(i, argv))
					return 0;
				if ((argv[i][0] == 'n') || (argv[i][0] == 'N') || (argv[i][0] == '0'))
					opts.isForceStackSameSeries = 0;
				if ((argv[i][0] == 'y') || (argv[i][0] == 'Y') || (argv[i][0] == '1'))
					opts.isForceStackSameSeries = 1;
				if ((argv[i][0] == '2'))
					opts.isForceStackSameSeries = 2;
				if ((argv[i][0] == 'o') || (argv[i][0] == 'O')) {
					opts.isForceStackDCE = false;
					//printf("Advanced feature: '-m o' merges images despite varying series number\n");
				}
				if ((argv[i][0] == '2')) {
					opts.isIgnoreSeriesInstanceUID = true;
					printf("Advanced feature: '-m 2' ignores Series Instance UID.\n");
				}
			} else if ((argv[i][1] == 'p') && ((i + 1) < argc)) {
				i++;
				if (invalidParam(i, argv))
					return 0;
				if ((argv[i][0] == 'n') || (argv[i][0] == 'N') || (argv[i][0] == '0'))
					opts.isPhilipsFloatNotDisplayScaling = false;
				else
					opts.isPhilipsFloatNotDisplayScaling = true;
			} else if ((argv[i][1] == 'r') && ((i + 1) < argc)) {
				i++;
				if (invalidParam(i, argv))
					return 0;
				if ((argv[i][0] == 'y') || (argv[i][0] == 'Y'))
					opts.isRenameNotConvert = true;
			} else if ((argv[i][1] == 'q') && ((i + 1) < argc)) {
				i++;
				if (invalidParam(i, argv))
					return 0;
				if ((argv[i][0] == 'y') || (argv[i][0] == 'Y'))
					opts.onlySearchDirForDICOM = 1;
				else if ((argv[i][0] == 'l') || (argv[i][0] == 'L'))
					opts.onlySearchDirForDICOM = 2;
			} else if ((argv[i][1] == 's') && ((i + 1) < argc)) {
				i++;
				if (invalidParam(i, argv))
					return 0;
				if ((argv[i][0] == 'n') || (argv[i][0] == 'N') || (argv[i][0] == '0'))
					opts.isOnlySingleFile = false;
				else
					opts.isOnlySingleFile = true;
			} else if ((argv[i][1] == 't') && ((i + 1) < argc)) {
				i++;
				if (invalidParam(i, argv))
					return 0;
				if ((argv[i][0] == 'n') || (argv[i][0] == 'N') || (argv[i][0] == '0'))
					opts.isCreateText = false;
				else
					opts.isCreateText = true;
#if !defined(_WIN64) && !defined(_WIN32) //shell script for Unix only
			} else if (argv[i][1] == 'u') {
				return checkUpToDate();
#endif
			} else if ((argv[i][1] == 'v') && ((i + 1) >= argc)) {
				printf("%s\n", kDCMdate);
				return kEXIT_REPORT_VERSION;
			} else if ((argv[i][1] == 'v') && ((i + 1) < argc)) {
				i++;
				if (invalidParam(i, argv))
					return 0;
				if ((argv[i][0] == 'n') || (argv[i][0] == 'N') || (argv[i][0] == '0')) //0: verbose OFF
					opts.isVerbose = 0;
				else if ((argv[i][0] == 'h') || (argv[i][0] == 'H') || (argv[i][0] == '2')) //2: verbose HYPER
					opts.isVerbose = 2;
				else
					opts.isVerbose = 1; //1: verbose ON
			} else if ((argv[i][1] == 'w') && ((i + 1) < argc)) {
				i++;
				if (invalidParam(i, argv))
					return 0;
				if (argv[i][0] == '0')
					opts.nameConflictBehavior = 0;
				if (argv[i][0] == '1')
					opts.nameConflictBehavior = 1;
				if (argv[i][0] == '2')
					opts.nameConflictBehavior = 2;
			} else if ((argv[i][1] == 'x') && ((i + 1) < argc)) {
				i++;
				if (invalidParam(i, argv))
					return 0;
				if ((argv[i][0] == 'n') || (argv[i][0] == 'N') || (argv[i][0] == '0'))
					opts.isCrop = false;
				else if ((argv[i][0] == 'i') || (argv[i][0] == 'I')) {
					opts.isRotate3DAcq = false;
					opts.isCrop = false;
				} else
					opts.isCrop = true;
			} else if ((argv[i][1] == 'y') && ((i + 1) < argc)) {
				i++;
				bool isFlipY = opts.isFlipY;
				if (invalidParam(i, argv))
					return 0;
				if ((argv[i][0] == 'y') || (argv[i][0] == 'Y')) {
					opts.isFlipY = true; //force use of internal compression instead of pigz
					strcpy(opts.pigzname, "");
				} else if ((argv[i][0] == 'n') || (argv[i][0] == 'N'))
					opts.isFlipY = false;
				if (isFlipY != opts.isFlipY)
					printf("Advanced feature: You are flipping the default order of rows in your image.\n");
			} else if ((argv[i][1] == 'z') && ((i + 1) < argc)) {
				i++;
				if (invalidParam(i, argv))
					return 0;
				if ((argv[i][0] == '3')) {
					opts.isGz = false; //uncompressed 3D
					opts.isSave3D = true;
				} else if ((argv[i][0] == 'i') || (argv[i][0] == 'I')) {
					opts.isGz = true;
#ifndef myDisableZLib
					strcpy(opts.pigzname, ""); //force use of internal compression instead of pigz
#endif
				} else if ((argv[i][0] == 'n') || (argv[i][0] == 'N') || (argv[i][0] == '0'))
					opts.isGz = false;
				else
					opts.isGz = true;
				if (argv[i][0] == 'o')
					opts.isPipedGz = true; //pipe to pigz without saving uncompressed to disk
			} else if ((argv[i][1] == 'f') && ((i + 1) < argc)) {
				i++;
				strcpy(opts.filename, argv[i]);
				isOutNameSpecified = true;
			} else if ((argv[i][1] == 'o') && ((i + 1) < argc)) {
				i++;
				strcpy(opts.outdir, argv[i]);
			} else if ((argv[i][1] == 'n') && ((i + 1) < argc)) {
				i++;
				double seriesNumber = atof(argv[i]);
				if (seriesNumber < 0)
					opts.numSeries = -1.0; //report series: convert none
				else if ((opts.numSeries >= 0) && (opts.numSeries < MAX_NUM_SERIES)) {
					opts.seriesNumber[opts.numSeries] = seriesNumber;
					opts.numSeries += 1;
				} else {
					printf("Warning: too many series specified, ignoring -n %s\n", argv[i]);
				}
			} else
				printf(" Error: invalid option '%s %s'\n", argv[i], argv[i + 1]);
			;
			lastCommandArg = i;
		}	 //if parameter is a command
		i++; //read next parameter
	}		 //while parameters to read
#ifndef myDisableZLib
	if ((opts.isGz) && (opts.dirSearchDepth < 1) && (strlen(opts.pigzname) > 0)) {
		strcpy(opts.pigzname, "");
		printf("n.b. Setting directory search depth of zero invokes internal gz (network mode)\n");
	}
#endif
	if ((opts.isRenameNotConvert) && (!isOutNameSpecified)) { //sensible naming scheme for renaming option
//strcpy(opts.filename,argv[i]);
//2019 - now include "%o" to append media SOP UID, as instance number is not required to be unique
#if defined(_WIN64) || defined(_WIN32)
		strcpy(opts.filename, "%t\\%s_%p\\%4r_%o.dcm"); //nrrd or nhdr (windows folders)
#else
		strcpy(opts.filename, "%t/%s_%p/%4r_%o.dcm"); //nrrd or nhdr (unix folders)
#endif
		printf("renaming without output filename, assuming '-f %s'\n", opts.filename);
	}
	if (isSaveIni)
		saveIniFile(opts);
	//printf("%d %d",argc,lastCommandArg);
	if (argc == (lastCommandArg + 1)) { //+1 as array indexed from 0
		//the user did not provide an input filename, report filename structure
		char niiFilename[1024];
		strcpy(opts.outdir, ""); //no input supplied
		nii_createDummyFilename(niiFilename, opts);
		printf("%s\n", niiFilename);
		return EXIT_SUCCESS;
	}
#endif
#ifndef myEnableMultipleInputs
	if ((argc - lastCommandArg - 1) > 1) {
		printf("Warning: only processing last of %d input files (recompile with 'myEnableMultipleInputs' to recursively process multiple files)\n", argc - lastCommandArg - 1);
		lastCommandArg = argc - 2;
	}
#endif
#if !defined(_WIN64) && !defined(_WIN32)
	double startWall = get_wall_time();
#endif
	clock_t start = clock();
	for (i = (lastCommandArg + 1); i < argc; i++) {
		strcpy(opts.indir, argv[i]); // [argc-1]
		int ret = nii_loadDir(&opts);
		if (ret != EXIT_SUCCESS)
			return ret;
	}

	if (opts.onlySearchDirForDICOM == 0) {
#if !defined(_WIN64) && !defined(_WIN32)
		printf("Conversion required %f seconds (%f for core code).\n", get_wall_time() - startWall, ((float)(clock() - start)) / CLOCKS_PER_SEC);
#else
		printf("Conversion required %f seconds.\n", ((float)(clock() - start)) / CLOCKS_PER_SEC);
#endif
	}
	//if (isSaveIni) //we now save defaults earlier, in case of early termination.
	//	saveIniFile(opts);
	return EXIT_SUCCESS;
}
