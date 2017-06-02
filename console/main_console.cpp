//  main.m dcm2niix
// by Chris Rorden on 3/22/14, see license.txt
//  Copyright (c) 2014 Chris Rorden. All rights reserved.

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
// cl /EHsc main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp -DmyDisableOpenJPEG -DmyDisableJasper /odcm2niix


//#define mydebugtest //automatically process directory specified in main, ignore input arguments


#include <stdlib.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <stddef.h>
#include <float.h>
//#include <unistd.h>
#include <time.h>  // clock_t, clock, CLOCKS_PER_SEC
#include <stdio.h>
#include "nii_dicom_batch.h"
#include "nii_dicom.h"
#include <math.h>

#if !defined(_WIN64) && !defined(_WIN32)
#include <time.h>
#include <sys/time.h>
double get_wall_time(){
    struct timeval time;
    if (gettimeofday(&time,NULL)){
        //  Handle error
        return 0;
    }
    return (double)time.tv_sec + (double)time.tv_usec * .000001;
}
#endif


const char* removePath(const char* path) { // "/usr/path/filename.exe" -> "filename.exe"
    const char* pDelimeter = strrchr (path, '\\');
    if (pDelimeter)
        path = pDelimeter+1;
    pDelimeter = strrchr (path, '/');
    if (pDelimeter)
        path = pDelimeter+1;
    return path;
} //removePath()

void showHelp(const char * argv[], struct TDCMopts opts) {
    const char *cstr = removePath(argv[0]);
    printf("usage: %s [options] <in_folder>\n", cstr);
    printf(" Options :\n");
    printf("  -1..-9 : gz compression level (1=fastest, 9=smallest)\n");
    char bidsCh = 'n';
    if (opts.isCreateBIDS) bidsCh = 'y';
    printf("  -b : BIDS sidecar (y/n, default %c)\n", bidsCh);
    if (opts.isAnonymizeBIDS) bidsCh = 'y'; else bidsCh = 'n';
    printf("   -ba : anonymize BIDS (y/n, default %c)\n", bidsCh);
    #ifdef mySegmentByAcq
     #define kQstr " %%q=sequence number,"
    #else
     #define kQstr ""
    #endif
    printf("  -f : filename (%%a=antenna  (coil) number, %%c=comments, %%d=description, %%e echo number, %%f=folder name, %%i ID of patient, %%j seriesInstanceUID, %%k studyInstanceUID, %%m=manufacturer, %%n=name of patient, %%p=protocol,%s %%s=series number, %%t=time, %%u=acquisition number, %%x study ID; %%z sequence name; default '%s')\n", kQstr, opts.filename);
    printf("  -h : show help\n");
    printf("  -i : ignore derived, localizer and 2D images (y/n, default n)\n");
    printf("  -t : text notes includes private patient details (y/n, default n)\n");
    printf("  -m : merge 2D slices from same series regardless of study time, echo, coil, orientation, etc. (y/n, default n)\n");
    printf("  -o : output directory (omit to save to input folder)\n");
    printf("  -p : Philips precise float (not display) scaling (y/n, default y)\n");
    printf("  -s : single file mode, do not convert other images in folder (y/n, default n)\n");
    printf("  -t : text notes includes private patient details (y/n, default n)\n");
    printf("  -v : verbose (n/y or 0/1/2 [no, yes, logorrheic], default 0)\n");
    printf("  -x : crop (y/n, default n)\n");
    char gzCh = 'n';
    if (opts.isGz) gzCh = 'y';
    #ifdef myDisableZLib
		if (strlen(opts.pigzname) > 0)
			printf("  -z : gz compress images (y/n, default %c)\n", gzCh);
		else
			printf("  -z : gz compress images (y/n, default %c) [REQUIRES pigz]\n", gzCh);
    #else
    	#ifdef myDisableMiniZ
    	printf("  -z : gz compress images (y/i/n, default %c) [y=pigz, i=internal:zlib, n=no]\n", gzCh);
		#else
		printf("  -z : gz compress images (y/i/n, default %c) [y=pigz, i=internal, n=no]\n", gzCh);
		#endif
    #endif

#if defined(_WIN64) || defined(_WIN32)
    printf(" Defaults stored in Windows registry\n");
    printf(" Examples :\n");
    printf("  %s c:\\DICOM\\dir\n", cstr);
    printf("  %s -o c:\\out\\dir c:\\DICOM\\dir\n", cstr);
    printf("  %s -f mystudy%%s c:\\DICOM\\dir\n", cstr);
    printf("  %s -o \"c:\\dir with spaces\\dir\" c:\\dicomdir\n", cstr);
#else
    printf(" Defaults file : %s\n", opts.optsname);
    printf(" Examples :\n");
    printf("  %s /Users/chris/dir\n", cstr);
    printf("  %s -o /users/cr/outdir/ -z y ~/dicomdir\n", cstr);
    printf("  %s -f %%p_%%s -b y -ba n ~/dicomdir\n", cstr);
    printf("  %s -f mystudy%%s ~/dicomdir\n", cstr);
    printf("  %s -o \"~/dir with spaces/dir\" ~/dicomdir\n", cstr);
#endif
} //showHelp()

//#define mydebugtest

int main(int argc, const char * argv[])
{
    struct TDCMopts opts;
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
    printf("Chris Rorden's dcm2niiX version %s (%llu-bit %s)\n",kDCMvers, (unsigned long long) sizeof(size_t)*8, kOS);
    if (argc < 2) {
        showHelp(argv, opts);
        return 0;
    }
    //bool isCustomOutDir = false;
    int i = 1;
    int lastCommandArg = 0;
    while (i < (argc)) { //-1 as final parameter is DICOM directory
        if ((strlen(argv[i]) > 1) && (argv[i][0] == '-')) { //command
            if (argv[i][1] == 'h')
                showHelp(argv, opts);
            else if ((argv[i][1] >= '1') && (argv[i][1] <= '9')) {
            	opts.gzLevel = abs((int)strtol(argv[i], NULL, 10));
            	if (opts.gzLevel > 11)
        	 		opts.gzLevel = 11;
            } else if ((argv[i][1] == 'b') && ((i+1) < argc)) {
                if (strlen(argv[i]) < 3) { //"-b y"
                	i++;
                	if ((argv[i][0] == 'n') || (argv[i][0] == 'N')  || (argv[i][0] == '0'))
                    	opts.isCreateBIDS = false;
                	else
                    	opts.isCreateBIDS = true;
                } else if (argv[i][2] == 'a') {//"-ba y"
                	i++;
                	if ((argv[i][0] == 'n') || (argv[i][0] == 'N')  || (argv[i][0] == '0'))
                    	opts.isAnonymizeBIDS = false;
                	else
                    	opts.isAnonymizeBIDS = true;

                } else
                	printf("Error: Unknown command line argument: '%s'\n", argv[i]);
            } else if ((argv[i][1] == 'i') && ((i+1) < argc)) {
                i++;
                if ((argv[i][0] == 'n') || (argv[i][0] == 'N')  || (argv[i][0] == '0'))
                    opts.isIgnoreDerivedAnd2D = false;
                else
                    opts.isIgnoreDerivedAnd2D = true;
            } else if ((argv[i][1] == 'm') && ((i+1) < argc)) {
                i++;
                if ((argv[i][0] == 'n') || (argv[i][0] == 'N')  || (argv[i][0] == '0'))
                    opts.isForceStackSameSeries = false;
                else
                    opts.isForceStackSameSeries = true;
            } else if ((argv[i][1] == 'p') && ((i+1) < argc)) {
                i++;
                if ((argv[i][0] == 'n') || (argv[i][0] == 'N')  || (argv[i][0] == '0'))
                    opts.isPhilipsFloatNotDisplayScaling = false;
                else
                    opts.isPhilipsFloatNotDisplayScaling = true;

            } else if ((argv[i][1] == 's') && ((i+1) < argc)) {
                i++;
                if ((argv[i][0] == 'n') || (argv[i][0] == 'N')  || (argv[i][0] == '0'))
                    opts.isOnlySingleFile = false;
                else
                    opts.isOnlySingleFile = true;
            } else if ((argv[i][1] == 't') && ((i+1) < argc)) {
                i++;
                if ((argv[i][0] == 'n') || (argv[i][0] == 'N')  || (argv[i][0] == '0'))
                    opts.isCreateText = false;
                else
                    opts.isCreateText = true;
            } else if ((argv[i][1] == 'v') && ((i+1) < argc)) {
                i++;
                if ((argv[i][0] == 'n') || (argv[i][0] == 'N')  || (argv[i][0] == '0')) //0: verbose OFF
                    opts.isVerbose = 0;
                else if ((argv[i][0] == 'h') || (argv[i][0] == 'H')  || (argv[i][0] == '2')) //2: verbose HYPER
                    opts.isVerbose = 2;
                else
                    opts.isVerbose = 1; //1: verbose ON
            } else if ((argv[i][1] == 'x') && ((i+1) < argc)) {
                i++;
                if ((argv[i][0] == 'n') || (argv[i][0] == 'N')  || (argv[i][0] == '0'))
                    opts.isCrop = false;
                else
                    opts.isCrop = true;
            } else if ((argv[i][1] == 'z') && ((i+1) < argc)) {
                i++;
                if ((argv[i][0] == 'i') || (argv[i][0] == 'I') ) {
                    opts.isGz = true; //force use of internal compression instead of pigz
                	strcpy(opts.pigzname,"");
                } else if ((argv[i][0] == 'n') || (argv[i][0] == 'N')  || (argv[i][0] == '0'))
                    opts.isGz = false;
                else
                    opts.isGz = true;
            } else if ((argv[i][1] == 'f') && ((i+1) < argc)) {
                i++;
                strcpy(opts.filename,argv[i]);
            } else if ((argv[i][1] == 'o') && ((i+1) < argc)) {
                i++;
                //isCustomOutDir = true;
                strcpy(opts.outdir,argv[i]);
            }
            lastCommandArg = i;
        } //if parameter is a command
        i ++; //read next parameter
    } //while parameters to read
    //printf("%d %d",argc,lastCommandArg);
    if (argc == (lastCommandArg+1))  { //+1 as array indexed from 0
        //the user did not provide an input filename, report filename structure
        char niiFilename[1024];
        strcpy(opts.outdir,"");//no input supplied
        nii_createDummyFilename(niiFilename, opts);
        printf("%s\n",niiFilename);
        return EXIT_SUCCESS;
    }
#endif
	#if !defined(_WIN64) && !defined(_WIN32)
	double startWall = get_wall_time();
	#endif
    clock_t start = clock();
    for (i = (lastCommandArg+1); i < argc; i++) {
    	strcpy(opts.indir,argv[i]); // [argc-1]
    	//if (!isCustomOutDir) strcpy(opts.outdir,opts.indir);
    	if (nii_loadDir(&opts) != EXIT_SUCCESS)
    		return EXIT_FAILURE;
    }
    #if !defined(_WIN64) && !defined(_WIN32)
		printf ("Conversion required %f seconds (%f for core code).\n",get_wall_time() - startWall, ((float)(clock()-start))/CLOCKS_PER_SEC);
	#else
	printf ("Conversion required %f seconds.\n",((float)(clock()-start))/CLOCKS_PER_SEC);
    #endif
    saveIniFile(opts);
    return EXIT_SUCCESS;
}
