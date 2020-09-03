## About

dcm2niix can convert Canon (né Toshiba) DICOM format images to NIfTI. This page notes vendor specific conversion details.

## Diffusion Weighted Imaging Notes

In contrast to several other vendors, Toshiba used public tags to report diffusion properties. Specifically, [DiffusionBValue (0018,9087)](http://dicomlookup.com/lookup.asp?sw=Tnumber&q=(0018,9087)) and [DiffusionGradientOrientation (0018,9089)](http://dicomlookup.com/lookup.asp?sw=Tnumber&q=(0018,9089)). Be aware that these tags are only populated for images where a diffusion gradient is applied. Consider a typical diffusion series where some volumes are acquired with B=0 while others have B=1000. In this case, only the volumes with B>0 will report a DiffusionBValue.

Since the acquisition by Canon, these public tags are no longer populated. The diffusion gradient directions are now stored in the ASCII Image Comments tag. Like GE (but unlike [Siemens, GE and Toshiba](https://www.na-mic.org/wiki/NAMIC_Wiki:DTI:DICOM_for_DWI_and_DTI)), these directions are with respect to the image space, not the scanner bore. For empirical data see the Sample Datasets section.

```
 (0018,9087) FD 1500                                     #   8, 1 DiffusionBValue
 (0020,4000) LT [b=1500(0.445,0.000,0.895)]              #  26, 1 ImageComments
```


## Unknown Properties

The [BIDS format](https://bids.neuroimaging.io) can record several sequence properties that are useful for processing MRI data. The DICOM headers created by Toshiba scanners are very clean and minimalistic, and do not report several of these advanced properties. Therefore, dcm2niix is unable to populate these properties of the JSON file. This reflects a limitation of the DICOM images, not of dcm2niix.

 - SliceTiming is not recorded. This can be useful for slice time correction.
 - Phase encoding polarity is not record. This is useful for undistortion with [TOPUP](https://fsl.fmrib.ox.ac.uk/fsl/fslwiki/topup).


## Sample Datasets

 - [Toshiba Aquilion](https://www.aliza-dicom-viewer.com/download/datasets).
 - [Toshiba 3T Galan Diffusion Dataset](https://github.com/neurolabusc/dcm_qa_toshiba).
 - [Canon 3T Galan Diffusion Dataset](https://github.com/neurolabusc/dcm_qa_canon).