//  main.m dcm2niix
// by Chris Rorden on 3/22/14, see license.txt
//  Copyright (c) 2014 Chris Rorden. All rights reserved.
//  yaml batch suport by Benjamin Irving, 2016 - maintains copyright

#include <stdlib.h>
#include <stdbool.h>
#include <time.h>  // clock_t, clock, CLOCKS_PER_SEC
#include <stdio.h>
#include <cmath>
#include <string>
#include <iostream>

#include "nii_dicom_batch.h"

#include <fstream>
#include <yaml-cpp/yaml.h>

const char* removePath(const char* path) { // "/usr/path/filename.exe" -> "filename.exe"
    const char* pDelimeter = strrchr (path, '\\');
    if (pDelimeter)
        path = pDelimeter+1;
    pDelimeter = strrchr (path, '/');
    if (pDelimeter)
        path = pDelimeter+1;
    return path;
} //removePath()


int rmainbatch(TDCMopts opts)
{
    printf("Chris Rorden's dcm2niiX version %s (%lu-bit)\n",kDCMvers, sizeof(size_t)*8);

    clock_t start = clock();
    nii_loadDir(&opts);
    printf ("Conversion required %f seconds.\n",((float)(clock()-start))/CLOCKS_PER_SEC);
    return EXIT_SUCCESS;
}

int main(int argc, const char * argv[])
{

    if (argc != 2) {
        std::cout << "Do not include additional inputs with a config file \n";
        throw;
    }

    // Process it all via a yaml file
    std::string yaml_file = argv[1];
    std::cout << "yaml_path: " << yaml_file << std::endl;
    YAML::Node config = YAML::LoadFile(yaml_file);

    struct TDCMopts opts;
    readIniFile(&opts, argv); //setup defaults, e.g. path to pigz
    opts.isCreateBIDS = config["Options"]["isCreateBIDS"].as<bool>();
    opts.isOnlySingleFile = config["Options"]["isOnlySingleFile"].as<bool>();
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
    return 1;
}
