## About

dcm2niix attempts to convert all DICOM images to NIfTI. The Philips enhanced DICOM images are elegantly able to save all images from a series as a single file. However, this format is necessarily complex. The usage of this format has evolved over time, and can become further complicated when DICOM are handled by DICOM tools (for example, anonymization, transfer which converts explicit VRs to implicit VRs, etc.).

This web page describes some of the strategies handle these images. However, users should be vigilant when handling these datasets. If you encounter problems using dcm2niix you can explore [alternative DICOM to NIfTI converters](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Alternatives) or [report an issue](https://github.com/rordenlab/dcm2niix).

## Image Patient Position

The Image Patient Position (0020,0032) tag is required to determine the position of the slices with respect to each other (and with respect to the scanner bore). Philips scans often report two conflicting IPPs for each single slice: with one stored in the private sequence (SQ) 2005,140F while the other is in the public sequence. This is unusual, but is [legal](ftp://medical.nema.org/medical/dicom/final/cp758_ft.pdf).

In practice, this complication has several major implications. First, not all software translate private SQs well. One potential problem is when the implicit VRs are saved as implicit VRs. This can obscure the fact that 2005,140F is an SQ. Indeed, some tools will convert the private SQ type as a "UN" unknown type and add another public sequence. This can confuse reading software.

Furthermore, in the real world there are many Philips DICOM images that ONLY contain IPP inside SQ 2005,140F. These situations appear to reflect modifications applied by a PACS to the DICOM data or attempts to anonymize the DICOM images (e.g. using simple Matlab scripts). Note that the private IPP differs from the public one by half a voxel. Therefore, in theory if one only has the private IPP, one can infer the public IPP location. Current versions of dcm2niix do not do this: the error is assumed small enough that it will not impact image processing steps such as coregistration.

In general it is recommended that you archive and convert DICOM images as they are created from the scanner. If one does use an export tool such as the excellent dcmtk, it is recommended that you preserve the explicit VR, as implicit VR has the potential of obscuring private sequence (SQ) tags. Be aware that subsequent processing of DICOM data can disrupt data conversion.

Therefore, dcm2niix will ignore the IPP enclosed in 2005,140F unless no alternative exists.

## Derived parametric maps stored with raw diffusion data

Some Philips diffusion DICOM images include derived image(s) along with the images. Other manufacturers save these derived images as a separate series number, and the DICOM standard seems ambiguous on whether it is allowable to mix raw and derived data in the same series (see PS 3.3-2008, C.7.6.1.1.2-3). In practice, many Philips diffusion images append [derived parametric maps](http://www.revisemri.com/blog/2008/diffusion-tensor-imaging/) with the original data. For example, ADC, Trace and Isotropic images can all be derived from the raw scans. As scientists, we want to discard these derived images, as they will disrupt data processing and we can generate better parametric maps after we have applied undistortion methods such as [Eddy and Topup](https://fsl.fmrib.ox.ac.uk/fsl/fslwiki/eddy/UsersGuide). The current version of dcm2niix uses the Diffusion Directionality (0018,9075) tag to detect B=0 unweighted ("NONE"), B-weighted ("DIRECTIONAL"), and derived ("ISOTROPIC") images. Note that the Dimension Index Values (0020,9157) tag provides an alternative approach to discriminate these images. Here are sample tags from a Philips enhanced image that includes and derived map (3rd dimension is "1" while the other images set this to "2").

```
(0018,9075) CS [DIRECTIONAL]
(0018,9089) FD 1\0\0
(0018,9087) FD 1000
(0020,9157) UL 1\1\2\32
...
(0018,9075) CS [ISOTROPIC]
(0018,9087) FD 1000
(0020,9157) UL 1\2\1\33
...
(0018,9075) CS [NONE]
(0018,9087) FD 0
(0020,9157) UL 1\1\2\33
```

## Diffusion Direction

Proper Philips enhanced scans use tag 0018,9089 to report the 3 gradient directions. However, in the wild, other files from Philips (presumably using older versions of Philips software) use the tags 2005,10b0, 2005,10b1, 2005,10b2. In general, dcm2niix will use the values that most closely precede the Dimension Index Values (0020,9157).

The current version of dcm2niix uses Dimension Index Values (0020,9157) to determine gradient number, which can also be found in (2005,1413). However, while 2005,1413 is always indexed from one, this is not necessarily the case for 0020,9157. For example, the ADNI DWI dataset for participant 018_S_4868 has values of 2005,1413 that range from 1..36 for the 36 directions, while 0020,9157 ranges from 2..37. The current version of dcm2niix compensates for this by re-indexing the values of 0020,9157 after all the volumes have been read.

## General variations

Prior versions of dcm2niix used different methods to sort images. However, these have proved unreliable The undocumented tags SliceNumberMrPhilips (2001,100A). In theory, InStackPositionNumber (0020,9057) should be present in all enhanced files, but has not proved reliable (perhaps not in older Philips images or DICOM images that were modified after leaving the scanner). MRImageGradientOrientationNumber (2005,1413) is complicated by the inclusion of derived images. Therefore, current versions of dcm2niix do not generally depend on any of these.

## Sample Datasets

 - [National Alliance for Medical Image Computing (NAMIC) samples](http://www.insight-journal.org/midas/collection/view/194)
 - [Unusual Philips Examples](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Unusual_MRI).
 - [Diffusion Examples](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Diffusion_Tensor_Imaging).