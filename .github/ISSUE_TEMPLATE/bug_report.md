---
name: Bug report
about: Create a report to help us improve
title: ''
labels: ''
assignees: ''

---

**Describe the bug**

A clear and concise description of what the bug is.

**To reproduce**

Steps to reproduce the behavior:
1. Run the command 'dcm2niix ...'
2. See error ...

**Expected behavior**

A clear and concise description of what you expected to happen.

**Output log**

If applicable, output generated converting data.

**Version**

Please report the complete version string:

 - dcm2niix version string, e.g. `dcm2niiX version v1.0.20201207  Clang12.0.0 ARM (64-bit MacOS)` The version string is always the first line generated when dcm2niix is run.

**Troubleshooting**

Please try the following steps to resolve your issue:

 - Is this the [latest stable release](https://github.com/rordenlab/dcm2niix/releases)? If not, does the latest stable release resolve your issue?
 - If the latest stable version fails, and you are using Windows. Does the latest commit on the development branch resolve your issue? You can get a pre-compiled version from [AppVeyor](https://ci.appveyor.com/project/neurolabusc/dcm2niix) (click on the Artifacts button).
 - If the latest stable version fails, and you are using macOS or Linux. Does the latest commit on the development branch resolve your issue? You can build this using the recipe below:

```
git clone --branch development https://github.com/rordenlab/dcm2niix.git
cd dcm2niix/console
g++  -I.  main_console.cpp nii_foreign.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp  -o dcm2niix -DmyDisableOpenJPEG
./dcm2niix ....
```
