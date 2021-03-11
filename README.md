[![Build Status](https://travis-ci.org/rordenlab/dcm2niix.svg?branch=master)](https://travis-ci.org/rordenlab/dcm2niix)
[![Build status](https://ci.appveyor.com/api/projects/status/7o0xp2fgbhadkgn1?svg=true)](https://ci.appveyor.com/project/neurolabusc/dcm2niix)

## About

dcm2niix is designed to convert neuroimaging data from the DICOM format to the NIfTI format. This web page hosts the developmental source code - a compiled version for Linux, MacOS, and Windows of the most recent stable release is included with [MRIcroGL](https://www.nitrc.org/projects/mricrogl/). A full manual for this software is available in the form of a [NITRC wiki](http://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage).

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

[See releases](https://github.com/rordenlab/dcm2niix/releases) for recent release notes. [See the VERSIONS.md file for details on earlier releases](./VERSIONS.md).

## Running

Command line usage is described in the [NITRC wiki](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#General_Usage). The minimal command line call would be `dcm2niix /path/to/dicom/folder`. However, you may want to invoke additional options, for example the call `dcm2niix -z y -f %p_%t_%s -o /path/ouput /path/to/dicom/folder` will save data as gzip compressed, with the filename based on the protocol name (%p) acquisition time (%t) and DICOM series number (%s), with all files saved to the folder "output". For more help see help: `dcm2niix -h`.

[See the BATCH.md file for instructions on using the batch processing version](./BATCH.md).

## Install

There are a couple ways to install dcm2niix
 - [Github Releases](https://github.com/rordenlab/dcm2niix/releases) provides the latest compiled executables. This is an excellent option for MacOS and Windows users. However, the provided Linux executable requires a recent version of Linux (e.g. Ubuntu 14.04 or later), so the provided Unix executable is not suitable for very old distributions. Specifically, it requires Glibc 2.19 (from 2014) or later. Users of older systems can compile their own copy of dcm2niix or download the compiled version included with MRIcroGL Glibc 2.12 (from 2011, see below).
 - Run the following command to get the latest version for Linux, Macintosh or Windows: 
   * `curl -fLO https://github.com/rordenlab/dcm2niix/releases/latest/download/dcm2niix_lnx.zip`
   * `curl -fLO https://github.com/rordenlab/dcm2niix/releases/latest/download/dcm2niix_mac.zip`
   * `curl -fLO https://github.com/rordenlab/dcm2niix/releases/latest/download/dcm2niix_mac_arm.pkg`
   * `curl -fLO https://github.com/rordenlab/dcm2niix/releases/latest/download/dcm2niix_win.zip`
 - [MRIcroGL (NITRC)](https://www.nitrc.org/projects/mricrogl) or [MRIcroGL (GitHub)](https://github.com/rordenlab/MRIcroGL12/releases) includes dcm2niix that can be run from the command line or from the graphical user interface (select the Import menu item). The Linux version of dcm2niix is compiled on a [holy build box](https://github.com/phusion/holy-build-box), so it should run on any Linux distribution.
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
git clone https://github.com/rordenlab/dcm2niix.git
cd dcm2niix
mkdir build && cd build
cmake -DZLIB_IMPLEMENTATION=Cloudflare -DUSE_JPEGLS=ON -DUSE_OPENJPEG=ON ..
make
```

**optional batch processing version:**

The batch processing binary `dcm2niibatch` is optional. To build `dcm2niibatch` as well change the cmake command to `cmake -DBATCH_VERSION=ON ..`. This requires a compiler that supports c++11.

### Building the command line version without cmake

If you have any problems with the cmake build script described above or want to customize the software see the [COMPILE.md file for details on manual compilation](./COMPILE.md).

## Referencing

 - Li X, Morgan PS, Ashburner J, Smith J, Rorden C (2016) The first step for neuroimaging data analysis: DICOM to NIfTI conversion. J Neurosci Methods. 264:47-56. doi: 10.1016/j.jneumeth.2016.03.001. [PMID: 26945974](https://www.ncbi.nlm.nih.gov/pubmed/26945974) 

## Alternatives

 - [dcm2nii](https://people.cas.sc.edu/rorden/mricron/dcm2nii.html) is the predecessor of dcm2niix. It is deprecated for modern images, but does handle image formats that predate DICOM (proprietary Elscint, GE and Siemens formats).
 - Python [dcmstack](https://github.com/moloney/dcmstack) DICOM to Nifti conversion with meta data preservation.
 - [dicm2nii](http://www.mathworks.com/matlabcentral/fileexchange/42997-dicom-to-nifti-converter) is written in Matlab. The Matlab language makes this very scriptable.
 - [dicom2nifti](https://github.com/icometrix/dicom2nifti) uses the scriptable Python wrapper utilizes the [high performance  GDCMCONV](http://gdcm.sourceforge.net/wiki/index.php/Gdcmconv) executables.
 - [dicomtonifti](https://github.com/dgobbi/vtk-dicom/wiki/dicomtonifti) leverages [VTK](https://www.vtk.org/).
 - [dinifti](http://as.nyu.edu/cbi/resources/Software/DINIfTI.html) is focused on conversion of Siemens data.
 - [DWIConvert](https://github.com/BRAINSia/BRAINSTools/tree/master/DWIConvert) converts DICOM images to NRRD and NIfTI formats.
 - [mcverter](http://lcni.uoregon.edu/%7Ejolinda/MRIConvert/) has great support for various vendors.
 - [mri_convert](https://surfer.nmr.mgh.harvard.edu/pub/docs/html/mri_convert.help.xml.html) is part of the popular FreeSurfer package. In my limited experience this tool works well for GE and Siemens data, but fails with Philips 4D datasets.
 - [MRtrix mrconvert](http://mrtrix.readthedocs.io/en/latest/reference/commands/mrconvert.html) is a useful general purpose image converter and handles DTI data well. It is an outstanding tool for modern Philips enhanced images.
 - [PET CT viewer](http://petctviewer.org/index.php/feature/results-exports/nifti-export) for [Fiji](https://fiji.sc) can load DICOM images and export as NIfTI.
 - [Plastimatch](https://www.plastimatch.org/) is a Swiss Army knife - it computes registration, image processing, statistics and it has a basic image format converter that can convert some DICOM images to NIfTI or NRRD.
 - [Simple Dicom Reader 2 (Sdr2)](http://ogles.sourceforge.net/sdr2-doc/index.html) uses [dcmtk](https://dicom.offis.de/dcmtk.php.en) to read DICOM images and convert them to the NIfTI format.
 - [SlicerHeart extension](https://github.com/SlicerHeart/SlicerHeart) is specifically designed to help 3D Slicer support ultra sound (US) images stored as DICOM.
 - [spec2nii](https://github.com/wexeee/spec2nii) converts MR spectroscopy to NIFTI.
 - [SPM12](http://www.fil.ion.ucl.ac.uk/spm/software/spm12/) is one of the most popular tools in the field. It includes DICOM to NIfTI conversion. Being based on Matlab it is easy to script.

## Links

The following tools exploit dcm2niix

  - [abcd-dicom2bids](https://github.com/DCAN-Labs/abcd-dicom2bids) selectively downloads high quality ABCD datasets. 
  - [autobids](https://github.com/khanlab/autobids) automates dcm2bids which uses dcm2niix.
  - [BiDirect_BIDS_Converter](https://github.com/wulms/BiDirect_BIDS_Converter) for conversion from DICOM to the BIDS standard.
  - [BIDScoin](https://github.com/Donders-Institute/bidscoin) is a DICOM to BIDS converter with a GUI and thorough [documentation](https://bidscoin.readthedocs.io).
  - [BIDS Toolbox](https://github.com/cardiff-brain-research-imaging-centre/bids-toolbox) is a web service for the creation and manipulation of BIDS datasets, using dcm2niix for importing DICOM data.
  - [birc-bids](https://github.com/bircibrain/birc-bids) provides a Docker/Singularity container with various BIDS conversion utilities.
  - [BOLD5000_autoencoder](https://github.com/nmningmei/BOLD5000_autoencoder) uses dcm2niix to pipe imaging data into an unsupervised machine learning algorithm.
  - [brainnetome DiffusionKit](http://diffusion.brainnetome.org/en/latest/) uses dcm2niix to convert images.
  - [Brain imAgiNg Analysis iN Arcana (Banana)](https://pypi.org/project/banana/) is a collection of brain imaging analysis workflows, it uses dcm2niix for format conversions.
  - [BraTS-Preprocessor](https://neuronflow.github.io/BraTS-Preprocessor/) uses dcm2niix to import files for [Brain Tumor Segmentation](https://www.frontiersin.org/articles/10.3389/fnins.2020.00125/full).
  - [clinica](https://github.com/aramis-lab/clinica) is a software platform for clinical neuroimaging studies that uses dcm2niix to convert DICOM images.
  - [dcm2niix can help convert data from the Adolescent Brain Cognitive Development (ABCD) DICOM to BIDS](https://github.com/ABCD-STUDY/abcd-dicom2bids)
  - [bidsify](https://github.com/spinoza-rec/bidsify) is a Python project that uses dcm2niix to convert DICOM and Philips PAR/REC images to the BIDS standard.
  - [bidskit](https://github.com/jmtyszka/bidskit) uses dcm2niix to create [BIDS](http://bids.neuroimaging.io/) datasets.
  - [BioImage Suite Web Project](https://github.com/bioimagesuiteweb/bisweb) is a JavaScript project that uses dcm2niix for its DICOM conversion module.
  - [boutiques-dcm2niix](https://github.com/lalet/boutiques-dcm2niix) is a dockerfile for installing and validating dcm2niix.
  - [DAC2BIDS](https://github.com/dangom/dac2bids) uses dcm2niibatch to create [BIDS](http://bids.neuroimaging.io/) datasets.
  - [Dcm2Bids](https://github.com/cbedetti/Dcm2Bids) uses dcm2niix to create [BIDS](http://bids.neuroimaging.io/) datasets. Here is a [tutorial](https://andysbrainbook.readthedocs.io/en/latest/OpenScience/OS/BIDS_Overview.html) describing usage.
  - [dcm2niir](https://github.com/muschellij2/dcm2niir) R wrapper for dcm2niix/dcm2nii.
  - [dcm2niix_afni](https://afni.nimh.nih.gov/pub/dist/doc/program_help/dcm2niix_afni.html) is a version of dcm2niix included with the [AFNI](https://afni.nimh.nih.gov/) distribution.
  - [dcm2niiXL](https://github.com/neurolabusc/dcm2niiXL) is a shell script and tuned compilation of dcm2niix designed for accelerated conversion of extra large datasets.
  - [DICOM2BIDS](https://github.com/klsea/DICOM2BIDS) is a Python 2 script for creating BIDS files.
  - [dicom2bids](https://github.com/Jolinda/lcnimodules) includes python modules for converting dicom files to nifti in a bids-compatible file structure that use dcm2niix.
  - [dcmwrangle](https://github.com/jbteves/dcmwrangle) a Python interactive and static tool for organizing dicoms.
  - [dicom2nifti_batch](https://github.com/scanUCLA/dicom2nifti_batch) is a Matlab script for automating dcm2niix.
  - [divest](https://github.com/jonclayden/divest) R interface to dcm2niix.
  - [ezBIDS](https://github.com/brainlife/ezbids) is a web service for converting directory full of DICOM images into BIDS without users having to learn python nor custom configuration file.
  - [fmrif tools](https://github.com/nih-fmrif/fmrif_tools) uses dcm2niix for its [oxy2bids](https://fmrif-tools.readthedocs.io/en/latest/#) tool.
  - [fsleyes](https://fsl.fmrib.ox.ac.uk/fsl/fslwiki/FSLeyes) is a powerful Python-based image viewer. It uses dcm2niix to handle DICOM files through its fslpy libraries.
  - [Functional Real-Time Interactive Endogenous Neuromodulation and Decoding (FRIEND) Engine](https://github.com/InstitutoDOr/FriendENGINE) uses dcm2niix.
  - [heudiconv](https://github.com/nipy/heudiconv) can use dcm2niix to create [BIDS](http://bids.neuroimaging.io/) datasets. Data acquired using the [reproin](https://github.com/ReproNim/reproin) convention can be easily converted to BIDS.
  - [kipettools](https://github.com/mathesong/kipettools) uses dcm2niix to load PET data.
  - [LEAD-DBS](http://www.lead-dbs.org/) uses dcm2niix for [DICOM import](https://github.com/leaddbs/leaddbs/blob/master/ea_dicom_import.m).
  - [lin4neuro](http://www.lin4neuro.net/lin4neuro/18.04bionic/vm/) releases such as the English l4n-18.04.4-amd64-20200801-en.ova include MRIcroGL and dcm2niix pre-installed. This allows user with VirtualBox or VMWarePlayer to use these tools (and many other neuroimaging tools) in a graphical virtual machine.	
  - [MRIcroGL](https://github.com/neurolabusc/MRIcroGL) is available for MacOS, Linux and Windows and provides a graphical interface for dcm2niix. You can get compiled copies from the [MRIcroGL NITRC web site](https://www.nitrc.org/projects/mricrogl/).
  - [neurodocker](https://github.com/kaczmarj/neurodocker) includes dcm2niix as a lean, minimal install Dockerfile.
  - [neuro_docker](https://github.com/Neurita/neuro_docker) includes dcm2niix as part of a single, static Dockerfile.
  - [NeuroDebian](http://neuro.debian.net/pkgs/dcm2niix.html) provides up-to-date version of dcm2niix for Debian-based systems.
  - [neurodocker](https://github.com/kaczmarj/neurodocker) generates [custom](https://github.com/rordenlab/dcm2niix/issues/138) Dockerfiles given specific versions of neuroimaging software.
  - [NeuroElf](http://neuroelf.net) can use dcm2niix to convert DICOM images.
  - [Neuroinformatics Database (NiDB)](https://github.com/gbook/nidb) is designed to store, retrieve, analyze, and share neuroimaging data. It uses dcm2niix for image QA and handling some formats. 
  - [NiftyPET](https://niftypet.readthedocs.io/en/latest/install.html) provides PET image reconstruction and analysis, and uses dcm2niix to handle DICOM images. 
  - [nipype](https://github.com/nipy/nipype) can use dcm2niix to convert images.
  - [conversion](https://github.com/pnlbwh/conversion) is a Python library that can convert dcm2niix created NIfTI files to the popular NRRD format (including DWI gradient tables). Note, recent versions of dcm2niix can directly convert DICOM images to NRRD.
  - [pydcm2niix is a Python module for working with dcm2niix](https://github.com/jstutters/pydcm2niix).
  - [pyBIDSconv provides a graphical format for converting DICOM images to the BIDS format](https://github.com/DrMichaelLindner/pyBIDSconv). It includes clever default heuristics for identifying Siemens scans.
  - [qsm](https://github.com/CAIsr/qsm) Quantitative Susceptibility Mapping software.
  - [reproin](https://github.com/ReproNim/reproin) is a setup for automatic generation of shareable, version-controlled BIDS datasets from MR scanners.
  - [sci-tran dcm2niix](https://github.com/scitran-apps/dcm2niix) Flywheel Gear (docker).
  - The [SlicerDcm2nii extension](https://github.com/Slicer/ExtensionsIndex/blob/master/SlicerDcm2nii.s4ext) is one method to import DICOM data into Slicer.
  - [TORTOISE](https://tortoise.nibib.nih.gov) is used for processing diffusion MRI data, and uses dcm2niix to import DICOM images.
  - [TractoR (Tracto­graphy with R) uses dcm2niix for image conversion](http://www.tractor-mri.org.uk/TractoR-and-DICOM).
