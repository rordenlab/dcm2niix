## About

dcm2niix attempts to convert Toshiba DICOM format images to NIfTI. This page notes vendor specific conversion details.

## Diffusion Weighted Imaging Notes

In contrast to several other vendors, Toshiba uses public tags to report diffusion properties. Specifically, [DiffusionBValue (0018,9087)](http://dicomlookup.com/lookup.asp?sw=Tnumber&q=(0018,9087)) and [DiffusionGradientOrientation (0018,9089)](http://dicomlookup.com/lookup.asp?sw=Tnumber&q=(0018,9089)). Be aware that these tags are only populated for images where a diffusion gradient is applied. Consider a typical diffusion series where some volumes are acquired with B=0 while others have B=1000. In this case, only the volumes with B>0 will report a DiffusionBValue.

## Unknown Properties

The (BIDS format)[https://bids.neuroimaging.io] can record several sequence properties that are useful for processing MRI data. The DICOM headers created by Toshiba scanners are very clean and minimalistic, and do not report several of these advanced properties. Therefore, dcm2niix is unable to populate these properties of the JSON file. This reflects a limitation of the DICOM images, not of dcm2niix.

 - SliceTiming is not recorded. This can be useful for slice time correction.
 - Phase encoding polarity is not record. This is useful for undistortion with [TOPUP](https://fsl.fmrib.ox.ac.uk/fsl/fslwiki/topup).


## Sample Datasets

 - [Toshiba Aquilion](https://www.aliza-dicom-viewer.com/download/datasets).
 - [Toshiba 3T Galan Diffusion Dataset](https://github.com/neurolabusc/dcm_qa_toshiba).
