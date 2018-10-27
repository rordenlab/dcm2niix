## About

dcm2niix attempts to convert Siemens DICOM format images to NIfTI. This page describes some vendor-specific details.

## Vida XA10A

The Siemens Vida introduced the new XA10A DICOM format. Users are strongly encouraged to export data using the "Enhanced" format and to not use any of the "Anonymize" features on the console. The consequences of these options is discussed in detail in [issue 236](https://github.com/rordenlab/dcm2niix/issues/236). In brief, the Vida can export to enhanced, mosaic or classic 2D. Note that the mosaics are considered secondary capture images intended for quality assurance only. The mosaic scans lack several "Type 1" DICOM properties, necessarily limiting conversion. The non-mosaic 2D enhanced DICOMs are compact and efficient, but appear to have limited details relative to the enhanced output. Finally, each of the formats (enhanced, mosaic, classic) can be exported as anonymized. The Siemens console anonymization of current XA10A (Fall 2018) strips many useful tags. Siemens suggests "the use an offline/in-house anonymization software instead." Another limitation of the current XA10A format is that it retains no versioning details for software and hardware stepping, despite the fact that the data format is rapidly evolving. If you use a Vida, you are strongly encouraged to log every hardware or software upgrade to allow future analyses to identify and regress out any effects of these modifications.  Since the XA10A format does not have a CSA header, dcm2niix will attempt to use the new private DICOM tags to populate the BIDS file. These tags are described in [issue 240](https://github.com/rordenlab/dcm2niix/issues/240).

## CSA Header

Many crucial Siemens parameters are stored in the [proprietary CSA header](http://nipy.org/nibabel/dicom/siemens_csa.html). This has a binary section that allows quick reading for many useful parameters. It also includes an ASCII text portion that includes a lot of information but is slow to parse and poorly curated.

## Slice Timing

The CSA header provides [slice timing](https://www.mccauslandcenter.sc.edu/crnl/tools/stc), and therefore dcm2niix should provide accurate slice timing information for non-XA10 datasets. For archival studies, be aware that some sequences [incorrectly reported slice timing](https://github.com/rordenlab/dcm2niix/issues/126).

## Total Readout Time

One often wants to determine [echo spacing, bandwidth, ](https://support.brainvoyager.com/brainvoyager/functional-analysis-preparation/29-pre-processing/78-epi-distortion-correction-echo-spacing-and-bandwidth) and total read-out time for EPI data so they can be undistorted. The [Siemens validation dataset](https://github.com/neurolabusc/dcm_qa/tree/master/In/TotalReadoutTime) demonstrates that dcm2niix can accurately report these parameters - the included notes and spreadsheet describe this in more detail.

## Diffusion Tensor Notes

Diffusion specific parameters are described on the [NA-MIC](https://www.na-mic.org/wiki/NAMIC_Wiki:DTI:DICOM_for_DWI_and_DTI#Private_vendor:_Siemens) website. Gradient vectors are reported with respect to the scanner bore, and dcm2niix will attempt to re-orient these to [FSL format](http://justinblaber.org/brief-introduction-to-dwmri/) [bvec files](https://fsl.fmrib.ox.ac.uk/fsl/fslwiki/FDT/FAQ#What_conventions_do_the_bvecs_use.3F).

## Sample Datasets

 - [Slice timing dataset](httphttps://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Slice_timing_corrections://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage).
 - [A validation dataset for dcm2niix commits](https://github.com/neurolabusc/dcm_qa).
 - [A mixture of GE and Siemens data](https://github.com/neurolabusc/dcm_qa_nih).
 - [DTI examples](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Diffusion_Tensor_Imaging).
 - [Archival (old) examples](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Archival_MRI).
 - [Unusual examples](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Unusual_MRI).

