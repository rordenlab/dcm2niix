## About

dcm2niix attempts to convert all DICOM images to NIfTI. The Philips enhanced DICOM images are elegantly able to save all images from a series as a single file. However, this format is necessarily complex. The usage of this format has evolved over time, and can become further complicated when DICOM are modified by DICOM tools (for example, anonymization, mangled by a [dcm4che/AGFA PACS](https://github.com/neurolabusc/dcm_qa_agfa), conversion of explicit VRs to implicit VRs, etc.).

This web page describes some of the strategies handle these images. However, users should be vigilant when handling these datasets. If you encounter problems using dcm2niix you can explore [alternative DICOM to NIfTI converters](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Alternatives) or [report an issue](https://github.com/rordenlab/dcm2niix).

## Image Patient Position

The Image Patient Position (0020,0032) tag is required to determine the position of the slices with respect to each other (and with respect to the scanner bore). Philips scans often report two conflicting IPPs for each single slice: with one stored in the private sequence (SQ) 2005,140F while the other is in the public sequence. This is unusual, but is [legal](ftp://medical.nema.org/medical/dicom/final/cp758_ft.pdf).

In practice, this complication has several major implications. First, not all software translate private SQs well. One potential problem is when the implicit VRs are saved as implicit VRs. This can obscure the fact that 2005,140F is an SQ. Indeed, some tools will convert the private SQ type as a "UN" unknown type and add another public sequence. This can confuse reading software.

Furthermore, in the real world there are many Philips DICOM images that ONLY contain IPP inside SQ 2005,140F. These situations appear to reflect modifications applied by a PACS to the DICOM data or attempts to anonymize the DICOM images (e.g. using simple Matlab scripts). Note that the private IPP differs from the public one by half a voxel. Therefore, in theory if one only has the private IPP, one can infer the public IPP location. Current versions of dcm2niix do not do this: the error is assumed small enough that it will not impact image processing steps such as coregistration.

In general it is recommended that you archive and convert DICOM images as they are created from the scanner. If one does use an export tool such as the excellent dcmtk, it is recommended that you preserve the explicit VR, as implicit VR has the potential of obscuring private sequence (SQ) tags. Be aware that subsequent processing of DICOM data can disrupt data conversion.

Therefore, dcm2niix will ignore the IPP enclosed in 2005,140F unless no alternative exists.

## Image Scaling

How data is represented in DICOM for MR has several challenges and the technology and standard has evolved over the years to accommodate new uses. Unlike CT, where the signal is naturally displayed in Hounsfield units, MR has no natural signal units and the magnitude is influenced by the electronics and the software processing required to bring this to the final image. Secondly most of the original DICOM implementations used small bit number integers to store the underlying images for economy of storage. As a result it is necessary to apply scaling from the internal DICOM storage to a form suitable for radiographic display or quantitative measurement. There remain several challenges with this process, ensuring that the mapping to the integer values makes best use of the available bit depth for images with large dynamic range, or large changes between images, without clipping the data while also preserving the appearance of the noise field which is demanded by the needs of radiographic visual review. Note that for most MRI modalities these concerns do not impact analyses: the intensity is assumed arbitrary, the statistics treat signal offset and scaling as nuisance regressors when fitting models, and cacluations are computed with high precision floating point numbers. However, there are some situations such as arterial spin labeling where image scaling is important. In these situations, scaling is a crucial aspect to be aware of for quantitative methods and which representation is used depends upon your needs.

At its simplest image scaling requires a rescale slope and intercept defined by the DICOM standard tags [0028,1053](http://dicomlookup.com/lookup.asp?sw=Tnumber&q=(0028,1053)) and [0028,1052](http://dicomlookup.com/lookup.asp?sw=Tnumber&q=(0028,1053)). Whether these values are the same for all images, or image specific depends upon the implementation and potentially the location of these tags withing the DICOM tag structure. For manufacturers other than Philips, these are the only intensity scaling values provided, so there is no concern regarding which scaling values should be used.

However, the DICOM standard introduced the concept of [`real world units`](http://dicom.nema.org/dicom/2013/output/chtml/part03/sect_A.46.html). This allows the storage of one or more mappings to allow selective viewing of the data mapped into different value ranges (which may also be non-linear mappings).

Philips thinks in terms of three different representations (using the terminology of the documentation available to Philips collaborators):

| Name             | ID            | Description|
| ---------------- | ------------- | ------------- |
| Stored Value     | SV            | Raw data stored in DICOM tag PIXEL DATA tag (7FE0,0010)|
| Displayed Value  | DV            | The value which is shown to the user when using scanner interface, ROIS, measurements etc.  |
| Floating Point   | FP            | An internal value at a point earlier in the reconstruction chain before the conversion to DICOM/integer for image presentation.  |
| Real World Value | WV            | DICOM defined real world units|

In general SV should not be used for quantitative measurements as it is an integer format. In practice, if the Rescale values are the same for all images (the typical case, but not guaranteed) SV can be used to compare signal intensities between images from the same scan. Note that the NIfTI format only provides a single `scl_slope` and `scl_inter` for the entire file, whereas in DICOM rescale values can in theory differ across 2D slices. Therefore, in situations where the rescale values do differ across slices, dcm2niix will apply the requested rescale to each slice and save the scaled data as the 32-bit float NIfTI dataset. This preserves the varibility reported by the rescale tags, at the cost of disk space. 

DV can be used for quantitative comparison of signal intensities between images in the same scan as long as the relevant rescale values are taken into account. These rescale values may come from the tags standard tags 0028,1053 and 0028,1052 or from a relevant RealWorld block if present. If the DV is derived from a RealWorld block with defined units (tag (0008,0104) such as Hz or ms rather than “no units”) or a RescaleType (0028,1054) with a non-US type (not defined by the standard), then the DV is already quantitative and cross scan comparison may be done.

However, in general DV is not sufficient to compare images from different scans, especially if the signal intensity varies a lot (eg multiple inversion recovery scans) in which case the FP value may be used as this may be compared (with some caveats) across scans and across timescales. This scaling requires an additional scale factor on top of the DV value, the Scale Slope (private tag (2005,100E))

As long as rescale values are identical across all DICOM slices, dcm2niix losslessly copies the raw pixel data from the DICOM tag (7FE0,0010) to NIfTI image. These values are typically stored as 16-bit integers in the range -32768..32767. Both the DICOM and NIfTI formats describe how scaling intercept and slope values can be used to convert these raw values into calibrated values. For example, with an intercept of 0 and slope of 0.01 the raw value of 50 would be converted to 0.5. 

The [NIfTI](https://nifti.nimh.nih.gov/pub/dist/src/niftilib/nifti1.h) header provides the `scl_slope` and `scl_inter` fields so each voxel value in the dataset is scaled as:

```
I = scl_slope * SV + scl_inter
```

where `SV` is the raw stored value and `I` is the "true" transformed voxel intensity.

Philips has three possible intensity transforms for their DICOM images (world (`W`), display (`D`), precise (`P`)). All of these transforms might be provided in a single DICOM image, while the [NIfTI](https://nifti.nimh.nih.gov/pub/dist/src/niftilib/nifti1.h) header only designates a single `scl_slope` and `scl_inter` for each image. dcm2niix will attempt to retain the stored values (`SV`) and sets the NIfTI `scl_inter` and `scl_slope values` for the desired intensity transform. dcm2niix will use `FP` if possible. If this is not possibleor the user specifies `-p n` dcm2niix will use the transforms for `DV`.

The formulas are provided below. The DICOM tags are in brackets (e.g. `(0040,9225)`) and the BIDS tag is in double quotes (e.g. `"PhilipsRWVSlope"`). Since all the scaling values are stored in the BIDS sidecar, you can always use these to later your preferred intensity transform (assume all slices used the same scaling values).

```
Inputs:
 SV = stored value of DICOM PIXEL DATA without scaling
 WS = RealWorldValue slope (0040,9225) "PhilipsRWVSlope"
 WI = RealWorldValue intercept (0040,9224) "PhilipsRWVIntercept"
 RS = rescale slope (0028,1053) "PhilipsRescaleSlope"
 RI = rescale intercept (0028,1052) "PhilipsRescaleIntercept"
 SS = scale slope (2005,100E) "PhilipsScaleSlope"
Outputs:
 WV = real world value
 FP = precise value
 DV = displayed value
Formulas:
 WV = SV * WS + WI
 DV = SV * RS + RI
 FP = DV / (RS * SS)
```

## Derived parametric maps stored with raw diffusion data

Some Philips diffusion DICOM images include derived image(s) along with the images. Other manufacturers save these derived images as a separate series number, and the DICOM standard seems ambiguous on whether it is allowable to mix raw and derived data in the same series (see PS 3.3-2008, C.7.6.1.1.2-3). In practice, many Philips diffusion images append [derived parametric maps](http://www.revisemri.com/blog/2008/diffusion-tensor-imaging/) with the original data. With Philips, appending the derived isotropic image is optional - it is only created for the 'clinical' DTI schemes for radiography analysis and is triggered if the first three vectors in the gradient table are the unit X,Y and Z vectors. For conventional DWI, the result is the conventional mean of the ADC X,Y,Z for DTI it the conventional mean of the 3 principle Eigen vectors. As scientists, we want to discard these derived images, as they will disrupt data processing and we can generate better parametric maps after we have applied undistortion methods such as [Eddy and Topup](https://fsl.fmrib.ox.ac.uk/fsl/fslwiki/eddy/UsersGuide). The current version of dcm2niix uses the Diffusion Directionality (0018,9075) tag to detect B=0 unweighted ("NONE"), B-weighted ("DIRECTIONAL"), and derived ("ISOTROPIC") images. Note that the Dimension Index Values (0020,9157) tag provides an alternative approach to discriminate these images. Here are sample tags from a Philips enhanced image that includes and derived map (3rd dimension is "1" while the other images set this to "2").

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

## Missing Information

Philips DICOMs do not contain all the information desired by many neuroscientists. Due to this, the [BIDS](http://bids.neuroimaging.io/) files created by dcm2niix are impoverished relative to data from other vendors. This reflects a limitation in the Philips DICOMs, not dcm2niix.

[Slice timing correction](https://www.mccauslandcenter.sc.edu/crnl/tools/stc) can account for some variability in fMRI datasets. Unfortunately, Philips DICOM data [does not encode slice timing information](https://neurostars.org/t/heudiconv-no-extraction-of-slice-timing-data-based-on-philips-dicoms/2201/4). Therefore, dcm2niix is unable to populate the "SliceTiming" BIDS field. However, one can typically infer slice timing by recording the [mode and number of packages](https://en.wikibooks.org/w/index.php?title=SPM/Slice_Timing&stable=0#Philips_scanners) reported for the sequence on the scanner console or the [sequence PDF](http://adni.loni.usc.edu/wp-content/uploads/2017/09/ADNI-3-Basic-Philips-R5.pdf). For precise timing, it is also worth knowing if equidistant "temporal slice spacing" is set and whether "prospect. motion corr." is on or off (if on, a short delay occurs between volumes).

Likewise, the BIDS tag "PhaseEncodingDirection" allows tools like [eddy](https://fsl.fmrib.ox.ac.uk/fsl/fslwiki/eddy) and [TOPUP](https://fsl.fmrib.ox.ac.uk/fsl/fslwiki/topup) to undistort images. While the Philips DICOM header distinguishes the phase encoding axis (e.g. anterior-posterior vs left-right) it does not encode the polarity (A->P vs P->A).

Another value desirable for TOPUP is the "TotalReadoutTime". Again, one can not confidently calculate this from Philips DICOMs (though on can [appoximate it if you make a few assumptions](https://github.com/nipreps/sdcflows/issues/5)). If you do decide to calculate this using values from the MRI console, be aware that the [FSL definition](https://github.com/rordenlab/dcm2niix/issues/130) is not intuitive for scans with interpolation, partial Fourier, parallel imaging, etc. However, it should be pointed out that the "TotalReadoutTime" only influences TOPUP's calibrated validation images that are typically ignored. The data used in subsequent steps will not be influenced by this value.

## Partial Volumes

NIfTI expects all 3D volumes of a 4D series to have the same number of series (e.g. a time series of 3D fMRI volumes, or a diffusion set with 3D volumes with different gradients applied). If a fMRI sequence is aborted part way through, it is possible that a Philips scanner will only save part of the final volume. An example would be where the total slices (9970) does not equal Dynamics (290) x slices (35) = 10150. Current versions of dcm2niix expect complete volumes. You can repair your data using the console or a Python script, as discussed in [issue 357](https://github.com/rordenlab/dcm2niix/issues/357). To resolve this situation by hand you could also [rename](RENAMING.md) your DICOM files with a call like `./dcm2niix -r y -f %t/%s_%p_%4y_%2r.dcm ~/out 0020,0100`. In this example, the [`%4y`](FILENAMING.md) parameter adds the volume (Temporal Position, 0020,0100) to the filename, allowing you to identify volumes with missing slices.

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
 - [Unusual Philips Examples (e.g. multi-echo)](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Unusual_MRI)
 - [Archival samples](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Archival_MRI)
 - [Diffusion Examples](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Diffusion_Tensor_Imaging)
 - [Additional Diffusion Examples](https://github.com/neurolabusc/dcm_qa_philips)
 - [Enhanced DICOMs](https://github.com/neurolabusc/dcm_qa_enh)