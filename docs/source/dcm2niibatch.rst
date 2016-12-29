:orphan:

dcm2niibatch manual
===================

Synopsis
--------

**dcm2niixbatch** <*configuration-file*>


Description
-----------

Most medical imaging devices save images in some variation of the popular DICOM
format. However, most scientific tools expect medical images to be stored in
the comparatively simpler NIfTI format. **dcm2niix** is designed to perform
such conversion from DICOM to NIfTI with a simple command-line interface.

**dcm2niibatch** acts as a wrapper around **dcm2niix** and allows the processing of
multiple DICOM sequences by specifying the list of files and settings in a yaml text file.
The makes processing a dataset of DICOM scans simpler and more easily repeatable.

In addition, yaml files are designed to be both human and machine readable and a
script can easily be written in many programming languages to automatically create a
yaml file based on an existing folder structure.

Please be advised that **dcm2niix** and **dcm2niibatch** have been developed for research
purposes only and should not be considered a clinical tool.


Configuration file format
-------------------------

Perform a batch conversion of multiple dicoms using **dcm2niibatch**, which is run by passing a
configuration file e.g *dcm2niibatch batch_config.yml*

The configuration file should be in yaml format as shown in example *batch_config.yaml*

.. code-block:: yaml

    Options:
      isGz:             false
      isFlipY:          false
      isVerbose:        false
      isCreateBIDS:     false
      isOnlySingleFile: false
    Files:
        -
          in_dir:           /path/to/first/folder
          out_dir:          /path/to/output/folder
          filename:         dcemri
        -
          in_dir:           /path/to/second/folder
          out_dir:          /path/to/output/folder
          filename:         fa3

You can add as many files as you want to convert as long as this structure stays consistent.
Note that a dash must separate each file.

in_dir     Path to the dicom files

out_dir    Path to save the nifti file

filename   File name of nifti file to save


See also
--------

:manpage:`dcm2niix(1)`


Licensing
---------

Copying and distribution of this file, with or without modification, are
permitted in any medium without royalty provided the copyright notice and this
notice are preserved. This file is offered as-is, without any warranty.


