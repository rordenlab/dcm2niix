//  main.m dcm2niix
// by Chris Rorden on 3/22/14, see license.txt
//  Copyright (c) 2014 Chris Rorden. All rights reserved.
//  yaml batch suport by Benjamin Irving, 2016 - maintains copyright

#include <stdbool.h> //requires VS 2015 or later
#ifdef _MSC_VER
	#include  <io.h> //access()
	#ifndef F_OK
	#define F_OK 0 /* existence check */
	#endif
#else
	#include <unistd.h> //access()
#endif
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <time.h>  // clock_t, clock, CLOCKS_PER_SEC
#include <yaml-cpp/yaml.h>
#include "nii_dicom.h"
#include "nii_dicom_batch.h"

const char* removePath(const char* path) { // "/usr/path/filename.exe" -> "filename.exe"
    const char* pDelimeter = strrchr (path, '\\');
    if (pDelimeter)
        path = pDelimeter+1;
    pDelimeter = strrchr (path, '/');
    if (pDelimeter)
        path = pDelimeter+1;
    return path;
} //removePath()

int rmainbatch(TDCMopts opts) {
    clock_t start = clock();
    nii_loadDir(&opts);
    printf ("Conversion required %f seconds.\n",((float)(clock()-start))/CLOCKS_PER_SEC);
    return EXIT_SUCCESS;
} //rmainbatch()

void showHelp(const char * argv[]) {
    const char *cstr = removePath(argv[0]);
    printf("Usage: %s <batch_config.yml>\n", cstr);
    printf("\n");
	printf("The configuration file must be in yaml format as shown below\n");
	printf("\n");
	printf("### START YAML FILE ###\n");
	printf("Options:\n");
	printf("   isGz:             false\n");
	printf("   isFlipY:          false\n");
	printf("   isVerbose:        false\n");
	printf("   isCreateBIDS:     false\n");
	printf("   isOnlySingleFile: false\n");
	printf("Files:\n");
	printf("   -\n");
	printf("    in_dir:           /path/to/first/folder\n");
	printf("    out_dir:          /path/to/output/folder\n");
	printf("    filename:         dcemri\n");
	printf("   -\n");
	printf("    in_dir:           /path/to/second/folder\n");
	printf("    out_dir:          /path/to/output/folder\n");
	printf("    filename:         fa3\n");
	printf("### END YAML FILE ###\n");
	printf("\n");
#if defined(_WIN64) || defined(_WIN32)
    printf(" Example :\n");
    printf("  %s c:\\dir\\yaml.yml\n", cstr);

    printf("  %s \"c:\\dir with spaces\\yaml.yml\"\n", cstr);
#else
    printf(" Examples :\n");
    printf("  %s /Users/chris/yaml.yml\n", cstr);
    printf("  %s \"/Users/dir with spaces/yaml.yml\"\n", cstr);
#endif
} //showHelp()

int main(int argc, const char * argv[]) {
	#if defined(__APPLE__)
		#define kOS "MacOS"
	#elif (defined(__linux) || defined(__linux__))
		#define kOS "Linux"
	#else
		#define kOS "Windows"
	#endif
	printf("dcm2niibatch using Chris Rorden's dcm2niiX version %s (%llu-bit %s)\n",kDCMvers, (unsigned long long) sizeof(size_t)*8, kOS);
    if (argc != 2) {
    	if (argc < 2)
    		printf(" Please provide location of config file\n");
    	else
        	printf(" Do not include additional inputs with a config file\n");
        printf("\n");
        showHelp(argv);
        return EXIT_FAILURE;
    }
	if( access( argv[1], F_OK ) == -1 ) {
        printf(" Please provide location of config file\n");
        printf("\n");
        showHelp(argv);
        return EXIT_FAILURE;
	}
    // Process it all via a yaml file
    std::string yaml_file = argv[1];
    std::cout << "yaml_path: " << yaml_file << std::endl;
    YAML::Node config = YAML::LoadFile(yaml_file);
    struct TDCMopts opts;
    readIniFile(&opts, argv); //setup defaults, e.g. path to pigz
    opts.isCreateBIDS = config["Options"]["isCreateBIDS"].as<bool>();
    opts.isOnlySingleFile = config["Options"]["isOnlySingleFile"].as<bool>();
    opts.isFlipY = config["Options"]["isFlipY"].as<bool>();
    opts.isCreateText = false;
    opts.isVerbose = 0;
    opts.isGz = config["Options"]["isGz"].as<bool>(); //save data as compressed (.nii.gz) or raw (.nii)
   	/*bool isInternalGz = config["Options"]["isInternalGz"].as<bool>();
    if (isInternalGz) {
        strcpy(opts.pigzname, "”); //do NOT use pigz: force internal compressor
		//in general, pigz is faster unless you have a very slow network, in which case the internal compressor is better
    }*/
    for (auto i: config["Files"]) {
        std::string indir = i["in_dir"].as<std::string>();
        strcpy(opts.indir, indir.c_str());
        std::string outdir = i["out_dir"].as<std::string>();
        strcpy(opts.outdir, outdir.c_str());
        std::string filename = i["filename"].as<std::string>();
        strcpy(opts.filename, filename.c_str());
        rmainbatch(opts);
    }
    return EXIT_SUCCESS;
} // main()
