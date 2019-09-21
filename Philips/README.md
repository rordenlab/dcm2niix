## About

dcm2niix attempts to convert all DICOM images to NIfTI. The Philips enhanced DICOM images are elegantly able to save all images from a series as a single file. However, this format is necessarily complex. The usage of this format has evolved over time, and can become further complicated when DICOM are handled by DICOM tools (for example, anonymization, transfer which converts explicit VRs to implicit VRs, etc.).

This web page describes some of the strategies handle these images. However, users should be vigilant when handling these datasets. If you encounter problems using dcm2niix you can explore [alternative DICOM to NIfTI converters](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Alternatives) or [report an issue](https://github.com/rordenlab/dcm2niix).

## Image Patient Position

The Image Patient Position (0020,0032) tag is required to determine the position of the slices with respect to each other (and with respect to the scanner bore). Philips scans often report two conflicting IPPs for each single slice: with one stored in the private sequence (SQ) 2005,140F while the other is in the public sequence. This is unusual, but is [legal](ftp://medical.nema.org/medical/dicom/final/cp758_ft.pdf).

In practice, this complication has several major implications. First, not all software translate private SQs well. One potential problem is when the implicit VRs are saved as implicit VRs. This can obscure the fact that 2005,140F is an SQ. Indeed, some tools will convert the private SQ type as a "UN" unknown type and add another public sequence. This can confuse reading software.

Furthermore, in the real world there are many Philips DICOM images that ONLY contain IPP inside SQ 2005,140F. These situations appear to reflect modifications applied by a PACS to the DICOM data or attempts to anonymize the DICOM images (e.g. using simple Matlab scripts). Note that the private IPP differs from the public one by half a voxel. Therefore, in theory if one only has the private IPP, one can infer the public IPP location. Current versions of dcm2niix do not do this: the error is assumed small enough that it will not impact image processing steps such as coregistration.

In general it is recommended that you archive and convert DICOM images as they are created from the scanner. If one does use an export tool such as the excellent dcmtk, it is recommended that you preserve the explicit VR, as implicit VR has the potential of obscuring private sequence (SQ) tags. Be aware that subsequent processing of DICOM data can disrupt data conversion.

Therefore, dcm2niix will ignore the IPP enclosed in 2005,140F unless no alternative exists.

## Image Scaling

dcm2niix losslessy copies the raw data from DICOM to NIfTI format. These values are typically stored as 16-bit integers in the range -32768..32767. Both the DICOM and NIfTI formats describe how scaling intercept and slope values can be used to convert these raw values into calibrated values. For example, with an intercept of 0 and slope of 0.01 the raw value of 50 would be converted to 0.5. 

Unlike other vendors, Philips can store different scaling factors in their DICOM header. For most MRI modalities where the intensity brightness is relative, this has no impact. However, for modalities like ASL it can have an impact. The NIfTI format requires a single intensity intercept and slope is chosen. Therefore, dcm2niix will choose the "Real World" values if provided. If these are not available, dcm2niix will choose either the "precise" (default) or "display" (if the user choose "-p n") value. dcm2niix will also populate the folllowing tags in the BIDS header that allow the user to select between different intensity scaling formats: "PhilipsRescaleSlope", "PhilipsRescaleIntercept", "PhilipsScaleSlope", "UsePhilipsFloatNotDisplayScaling" (where "1" indicates NIfTI uses precise value, and "0" indicates display values)., "PhilipsRWVSlope" and "PhilipsRWVIntercept".

The relevant DICOM tags are
RS = rescale slope ([0028,1053](http://dicomlookup.com/lookup.asp?sw=Tnumber&q=(0028,1053)))
RI = rescale intercept ([0028,1052](http://dicomlookup.com/lookup.asp?sw=Tnumber&q=(0028,1052)))
SS = scale slope (2005,100E)
RealWorldIntercept = (0040,9224) 
Real World Slope = (0040,9225)
The transformation formulas are:
R = raw value, P = precise value, D = displayed value
D = R * RS + RI
P = D/(RS * SS)

## Derived parametric maps stored with raw diffusion data

Some Philips diffusion DICOM images include derived image(s) along with the images. Other manufacturers save these derived images as a separate series number, and the DICOM standard seems ambiguous on whether it is allowable to mix raw and derived data in the same series (see PS 3.3-2008, C.7.6.1.1.2-3). In practice, many Philips diffusion images append [derived parametric maps](http://www.revisemri.com/blog/2008/diffusion-tensor-imaging/) with the original data. With Philips, appending the derived isotropic image is optional  - it is only created for the 'clinical' DTI schemes for radiography analysis and is triggered if the first three vectors in the gradient table are the unit X,Y and Z vectors. For conventional DWI, the result is the conventional mean of the ADC  X,Y,Z for DTI it the conventional mean of the 3 principle Eigen vectors. As scientists, we want to discard these derived images, as they will disrupt data processing and we can generate better parametric maps after we have applied undistortion methods such as [Eddy and Topup](https://fsl.fmrib.ox.ac.uk/fsl/fslwiki/eddy/UsersGuide). The current version of dcm2niix uses the Diffusion Directionality (0018,9075) tag to detect B=0 unweighted ("NONE"), B-weighted ("DIRECTIONAL"), and derived ("ISOTROPIC") images. Note that the Dimension Index Values (0020,9157) tag provides an alternative approach to discriminate these images. Here are sample tags from a Philips enhanced image that includes and derived map (3rd dimension is "1" while the other images set this to "2").

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

Public Tags
```
(0018,9089) FD 1\0\0
(0018,9087) FD 1000
```

Private Tags
```
(2001,1003) FL 1000
(2005,10b0) FL 1.0
(2005,10b1) FL 1.0
(2005,10b2) FL 0.0
```

For modern Philips DICOMs, the current version of dcm2niix uses Dimension Index Values (0020,9157) to determine gradient number, which can also be found in (2005,1413). However, while 2005,1413 is always indexed from one, this is not necessarily the case for 0020,9157. For example, the ADNI DWI dataset for participant 018_S_4868 has values of 2005,1413 that range from 1..36 for the 36 directions, while 0020,9157 ranges from 2..37. The current version of dcm2niix compensates for this by re-indexing the values of 0020,9157 after all the volumes have been read.

For acquiring DWI data, you can adjust your setup with from the Philips console. Specifically, in Contrast one selects Diffusion mode to DTI and adjusts the directional resolution. Options for directional resolution are `Low` which acquires 6 directions (P,M,S, plus 3 oblique), `Medium` (15 directions), `High` (32 directions) and `Opt x` where x is a number from 6 - 128 directions. DTI Elite users can also select `From File`. This will import the text file named E:\Export\dti_vectors_input.txt. This text file has a simple format. The first line is optional and is the name of the scheme - this line should not begin with a number. If the file contains a b=0 vector, it must be the first line of the file. The following lines specify the direction and bvalues. If you want to acquire more than one b=0 volume, each must specify a unique direction. One can only process these custom files with Philips FiberTrak if the same directions have been obtained for all b-values. Here is an example file that obeys these rules:

```
MyCustomDirections
0.000	0.000	1.000	0
0.049	-0.919	-0.391	1000
0.726	0.301	-0.618	1000
-0.683	0.255	-0.684	1000
0.845	-0.502	-0.186	1000
-0.73	-0.619	-0.288	1000
-0.051	0.039	0.998	1000
-0.018	0.871	-0.491	1000
-0.444	0.494	0.747	1000
-0.989	-0.086	-0.116	1000
1.000	0.000	0.000	0

```

## Missing Information.

Philips DICOMs do not contain all the information desired by many neuroscientists. Due to this, the [BIDS](http://bids.neuroimaging.io/) files created by dcm2niix are impoverished relative to data from other vendors. This reflects a limitation in the Philips DICOMs, not dcm2niix.

[Slice timing correction](https://www.mccauslandcenter.sc.edu/crnl/tools/stc) can account for some variability in fMRI datasets. Unfortunately, Philips DICOM data [does not encode slice timing information](https://neurostars.org/t/heudiconv-no-extraction-of-slice-timing-data-based-on-philips-dicoms/2201/4). Therefore, dcm2niix is unable to populate the "SliceTiming" BIDS field. However, one can typically infer slice timing by recording the [mode and number of packages](https://en.wikibooks.org/w/index.php?title=SPM/Slice_Timing&stable=0#Philips_scanners) reported for the sequence on the scanner console or the [sequence PDF](http://adni.loni.usc.edu/wp-content/uploads/2017/09/ADNI-3-Basic-Philips-R5.pdf). For precise timing, it is also worth knowing if equidistant "temporal slice spacing" is set and whether "prospect. motion corr." is on or off (if on, a short delay occurs between volumes).

Likewise, the BIDS tag "PhaseEncodingDirection" allows tools like [eddy](https://fsl.fmrib.ox.ac.uk/fsl/fslwiki/eddy) and [TOPUP](https://fsl.fmrib.ox.ac.uk/fsl/fslwiki/topup) to undistort images. While the Philips DICOM header distinguishes the phase encoding axis (e.g. anterior-posterior vs left-right) it does not encode the polarity (A->P vs P->A).

Another value desirable for TOPUP is the "TotalReadoutTime". Again, one can not calculate this from Philips DICOMs. If you do decide to calculate this using values from the MRI console, be aware that the [FSL definition](https://github.com/rordenlab/dcm2niix/issues/130) is not intuitive for scans with interpolation, partial Fourier, parallel imaging, etc. However, it should be pointed out that the "TotalReadoutTime" only inlfuences TOPUP's calibrated validation images that are typically ignored. The data used in subsequent steps will not be influenced by this value.

## Non-Image DICOMs

NIfTI is an image format, while DICOM is a multi-purpose format that can store videos (MPEG) or other data. Specifically, some Philips systems save Exam Cards and other non-image data as DICOM files. In these case, dcm2niix should skip these files, as they can not be represented in NIfTI. You can discriminate these files by reading the [MediaStorageSOPClassUID (0002,0002)](https://github.com/rordenlab/dcm2niix/issues/328).

- MR Image Storage = 1.2.840.10008.5.1.4.1.1.4
- Enhanced MR Image Storage = 1.2.840.10008.5.1.4.1.1.4.1
- MR Spectroscopy Storage = 1.2.840.10008.5.1.4.1.1.4.2
- Secondary Capture Image Storage = 1.2.840.10008.5.1.4.1.1.7
- Grayscale Softcopy Presentation State = 1.2.840.10008.5.1.4.1.1.11.1
- Raw Data Storage = 1.2.840.10008.5.1.4.1.1.66
- (Old) Private MR Spectrum Storage = 1.3.46.670589.11.0.0.12.1
- (Old) Private MR Series Data Storage = 1.3.46.670589.11.0.0.12.2
- (Old) Private MR Examcard Storage = 1.3.46.670589.11.0.0.12.4 

## General variations

Prior versions of dcm2niix used different methods to sort images. However, these have proved unreliable The undocumented tags SliceNumberMrPhilips (2001,100A). In theory, InStackPositionNumber (0020,9057) should be present in all enhanced files, but has not proved reliable (perhaps not in older Philips images or DICOM images that were modified after leaving the scanner). MRImageGradientOrientationNumber (2005,1413) is complicated by the inclusion of derived images. Therefore, current versions of dcm2niix do not generally depend on any of these.

## Sample Datasets

 - [National Alliance for Medical Image Computing (NAMIC) samples](http://www.insight-journal.org/midas/collection/view/194)
 - [Unusual Philips Examples](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Unusual_MRI).
 - [Diffusion Examples](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Diffusion_Tensor_Imaging).