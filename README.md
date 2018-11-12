[![Build Status](https://travis-ci.org/rordenlab/dcm2niix.svg?branch=master)](https://travis-ci.org/rordenlab/dcm2niix)
[![Build status](https://ci.appveyor.com/api/projects/status/7o0xp2fgbhadkgn1?svg=true)](https://ci.appveyor.com/project/neurolabusc/dcm2niix)

## About

dcm2niix is a designed to convert neuroimaging data from the DICOM format to the NIfTI format. This web page hosts the developmental source code - a compiled version for Linux, MacOS, and Windows of the most recent stable release is included with [MRIcroGL](https://www.nitrc.org/projects/mricrogl/). A full manual for this software is available in the form of a [NITRC wiki](http://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage).

## License

This software is open source. The bulk of the code is covered by the BSD license. Some units are either public domain (nifti*.*, miniz.c) or use the MIT license (ujpeg.cpp). See the license.txt file for more details.

## Dependencies

This software should run on macOS, Linux and Windows typically without requiring any other software. However, if you use dcm2niix to create gz-compressed images it will be faster if you have [pigz](https://github.com/madler/pigz) installed. You can get a version of both dcm2niix and pigz compiled for your operating system by downloading [MRIcroGL](https://www.nitrc.org/projects/mricrogl/).


## Image Conversion and Compression

DICOM provides many ways to store/compress image data, known as [transfer syntaxes](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#DICOM_Transfer_Syntaxes_and_Compressed_Images). The [COMPILE.md file describes details](./COMPILE.md) on how to enable different options to provide support for more formats.

 - The base code includes support for raw, run-length encoded, and classic JPEG lossless decoding.
 - Lossy JPEG is handled by the included [NanoJPEG](https://keyj.emphy.de/nanojpeg/). This support is modular: you can compile for [libjpeg-turbo](https://github.com/chris-allan/libjpeg-turbo) or disable it altogether.
 - JPEG-LS lossless support is optional, and can be provided by using [CharLS](https://github.com/team-charls/charls).
  - JPEG2000 lossy and lossless support is optional, and can be provided using [OpenJPEG](https://github.com/uclouvain/openjpeg) or [Jasper](https://www.ece.uvic.ca/~frodo/jasper/).
 - GZ compression (e.g. creating .nii.gz images) is optional, and can be provided using either the included [miniz](https://github.com/richgel999/miniz) or the popular zlib. Of particular note, the [Cloudflare zlib](https://github.com/cloudflare/zlib) exploits modern hardware (available since 2008) for very rapid compression. Alternatively, you can compile dcm2niix without a gzip compressor. Regardless of how you compile dcm2niix, it can use the external program [pigz](https://github.com/madler/pigz) for parallel compression.

## Versions

[See the VERSIONS.md file for details on releases](./VERSIONS.md).

## Running

Command line usage is described in the [NITRC wiki](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#General_Usage). The minimal command line call would be `dcm2niix /path/to/dicom/folder`. However, you may want to invoke additional options, for example the call `dcm2niix -z y -f %p_%t_%s -o /path/ouput /path/to/dicom/folder` will save data as gzip compressed, with the filename based on the protocol name (%p) acquisition time (%t) and DICOM series number (%s), with all files saved to the folder "output". For more help see help: `dcm2niix -h`.

[See the BATCH.md file for instructions on using the batch processing version](./BATCH.md).

## Install

There are a couple ways to install dcm2niix
 - [Github Releases](https://github.com/rordenlab/dcm2niix/releases) provides the latest compiled executables. This is an excellent option for MacOS and Windows users. However, the provided Linux executable requires a recent version of Linux, so the provided Unix executable is not suitable for all distributions.
 - [MRIcroGL](https://github.com/neurolabusc/MRIcroGL/releases) includes dcm2niix that can be run from the command line or from the graphical user interface (select the Import menu item). The Linux version of dcm2niix is compiled on a holy build box, so it should run on any Linux distribution.
 - If you have a MacOS computer with Homebrew you can run `brew install dcm2niix`.
 - If you have Conda, [`conda install -c conda-forge dcm2niix`](https://anaconda.org/conda-forge/dcm2niix) on Linux, MacOS or Windows.
 - On Debian Linux computers you can run `sudo apt-get install dcm2niix`.

## Build from source

It is often easier to download and install a precompiled version. However, you can also build from source.

### Build command line version with cmake (Linux, MacOS, Windows)

`cmake` and `pkg-config` (optional) can be installed as follows:

Ubuntu: `sudo apt-get install cmake pkg-config`

MacOS: `brew install cmake pkg-config`

**Basic build:**
```bash
git clone https://github.com/rordenlab/dcm2niix.git
cd dcm2niix
mkdir build && cd build
cmake ..
make
```
`dcm2niix` will be created in the `bin` subfolder. To install on the system run `make install` instead of `make` - this will copy the executable to your path so you do not have to provide the full path to the executable.

In rare case if cmake fails with the message like `"Generator: execution of make failed"`, it could be fixed by ``sudo ln -s `which make` /usr/bin/gmake``.

**Advanced build:**

As noted in the `Image Conversion and Compression Support` section, the software provides many optional modules with enhanced features. A common choice might be to include support for JPEG2000, [JPEG-LS](https://github.com/team-charls/charls) (this option requires a  c++14 compiler), as well as using the high performance Cloudflare zlib library (this option requires a CPU built after 2008). To build with these options simply request them when configuring cmake:

```bash
cmake -DZLIB_IMPLEMENTATION=Cloudflare -DUSE_JPEGLS=ON -DUSE_OPENJPEG=ON ..
```

**optional batch processing version:**

The batch processing binary `dcm2niibatch` is optional. To build `dcm2niibatch` as well change the cmake command to `cmake -DBATCH_VERSION=ON ..`. This requires a compiler that supports c++11.

### Building the command line version without cmake

If you have any problems with the cmake build script described above or want to customize the software see the [COMPILE.md file for details on manual compilation](./COMPILE.md).

## Alternatives

 - [dinifti](http://cbi.nyu.edu/software/dinifti.php) is focused on conversion of Siemens data.
 - [dcm2nii](http://www.mccauslandcenter.sc.edu/mricro/mricron/dcm2nii.htm) is the predecessor of dcm2niix. It is deprecated for modern images, but does handle image formats that predate DICOM (proprietary Elscint, GE and Siemens formats).
 - [DWIConvert](https://github.com/BRAINSia/BRAINSTools/tree/master/DWIConvert) converts DICOM images to NRRD and NIfTI formats.
 - [dicm2nii](http://www.mathworks.com/matlabcentral/fileexchange/42997-dicom-to-nifti-converter) is written in Matlab. The Matlab language makes this very scriptable.
 - [dicom2nifti](https://github.com/icometrix/dicom2nifti) uses the scriptable Python wrapper utilizes the [high performance  GDCMCONV](http://gdcm.sourceforge.net/wiki/index.php/Gdcmconv) executables.
 - [MRtrix mrconvert](http://mrtrix.readthedocs.io/en/latest/reference/commands/mrconvert.html) is a useful general purpose image converter and handles DTI data well. It is an outstanding tool for modern Philips enhanced images.
 - [mcverter](http://lcni.uoregon.edu/%7Ejolinda/MRIConvert/) has great support for various vendors.
 - [mri_convert](https://surfer.nmr.mgh.harvard.edu/pub/docs/html/mri_convert.help.xml.html) is part of the popular FreeSurfer package. In my limited experience this tool works well for GE and Siemens data, but fails with Philips 4D datasets.
 - [SPM12](http://www.fil.ion.ucl.ac.uk/spm/software/spm12/) is one of the most popular tools in the field. It includes DICOM to NIfTI conversion. Being based on Matlab it is easy to script.

## Links

  - [bidsify](https://github.com/spinoza-rec/bidsify) is a Python project that uses dcm2niix to convert DICOM and Philips PAR/REC images to the BIDS standard.
  - [bidskit](https://github.com/jmtyszka/bidskit) uses dcm2niix to create [BIDS](http://bids.neuroimaging.io/) datasets.
  - [boutiques-dcm2niix](https://github.com/lalet/boutiques-dcm2niix) is a dockerfile for installing and validating dcm2niix.
  - [DAC2BIDS](https://github.com/dangom/dac2bids) uses dcm2niibatch to create [BIDS](http://bids.neuroimaging.io/) datasets.
  - [Dcm2Bids](https://github.com/cbedetti/Dcm2Bids) uses dcm2niix to create [BIDS](http://bids.neuroimaging.io/) datasets.
  - [dcm2niir](https://github.com/muschellij2/dcm2niir) R wrapper for dcm2niix/dcm2nii.
  - [dcm2niix_afni](https://afni.nimh.nih.gov/pub/dist/doc/program_help/dcm2niix_afni.html) is a version of dcm2niix included with the [AFNI](https://afni.nimh.nih.gov/) distribution.
  - [dcm2niiXL](https://github.com/neurolabusc/dcm2niiXL) is a shell script and tuned compilation of dcm2niix designed for accelerated conversion of extra large datasets.
  - [dicom2nifti_batch](https://github.com/scanUCLA/dicom2nifti_batch) is a Matlab script for automating dcm2niix.
  - [divest](https://github.com/jonclayden/divest) R interface to dcm2niix.
  - [fsleyes](https://fsl.fmrib.ox.ac.uk/fsl/fslwiki/FSLeyes) is a powerful Python-based image viewer. It uses dcm2niix to handle DICOM files through its [fslpy libraries](https://users.fmrib.ox.ac.uk/~paulmc/fsleyes/fslpy/latest/fsl.data.dicom.html).
  - [heudiconv](https://github.com/nipy/heudiconv) can use dcm2niix to create [BIDS](http://bids.neuroimaging.io/) datasets.
  - [MRIcroGL](https://github.com/neurolabusc/MRIcroGL) is available for MacOS, Linux and Windows and provides a graphical interface for dcm2niix. You can get compiled copies from the [MRIcroGL NITRC web site](https://www.nitrc.org/projects/mricrogl/).
  - [neuro_docker](https://github.com/Neurita/neuro_docker) includes dcm2niix as part of a single, static Dockerfile.
  - [NeuroDebian](http://neuro.debian.net/pkgs/dcm2niix.html) provides up-to-date version of dcm2niix for Debian-based systems.
  - [neurodocker](https://github.com/kaczmarj/neurodocker) generates [custom](https://github.com/rordenlab/dcm2niix/issues/138) Dockerfiles given specific versions of neuroimaging software.
  - [nipype](https://github.com/nipy/nipype) can use dcm2niix to convert images.
  - [pydcm2niix is a Python module for working with dcm2niix](https://github.com/jstutters/pydcm2niix).
  - [sci-tran dcm2niix](https://github.com/scitran-apps/dcm2niix) Flywheel Gear (docker).
