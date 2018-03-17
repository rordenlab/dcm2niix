## About

dcm2niix attempts to convert all DICOM images to NIfTI. The Philips enhanced DICOM images are elegantly able to save all images from a series as a single file. However, this format is necessarily complex. The usage of this format has evolved over time, and can become further complicated when DICOM are handled by DICOM tools (for example, anonymization, transfer which converts explicit VRs to implicit VRs, etc.).

This web page describes some of the strategies handle these images. However, users should be vigilant when handling these datasets. If you encounter problems using dcm2niix you can explore [https://github.com/rordenlab/dcm2niix competing DICOM to NIfTI converters] or [ [https://github.com/rordenlab/dcm2niix report an issue].

## Image Patient Position

The Image Patient Position (0020,0032) tag is required to determine the position of the slices with respect to each other (and with respect to the scanner bore). Philips scans often report two conflicting IPPs for each single slice: with one stored in the private sequence (SQ) 2005,140F while the other is in the public sequence. This is unusual, but is [ftp://medical.nema.org/medical/dicom/final/cp758_ft.pdf legal].

In practice, this complication has several major implications. First, not all software translate private SQs well. One potential problem is when the implicit VRs are saved as implicit VRs. This can obscure the fact that 2005,140F is an SQ. Indeed, some tools will convert the private SQ type as a "UN" unknown type and add another public sequence. This can confuse reading software.

Furthermore, in the real world there are many Philips DICOM images that ONLY contain IPP inside SQ 2005,140F. It is not clear if this reflect the source Philips data or handling by other tools.

Therefore, dcm2niix will ignore the IPP enclosed in 2005,140F unless no alternative exists.

## Derived Apparent Diffusion Coefficient maps stored with raw diffusion data

The DICOM standard requires derived data to be saved as a separate series number than the raw data. However, many Philips diffusion images append the derived ADC maps with the original data. Strangely, some of these ADC maps seem to be stored with specific diffusion directions, and with the same volume number as the B=0 image. In these cases, dcm2niix uses the Diffusion Directionality (0018,9075) tag to detect B=0 unweighted ("NONE"), B-weighted ("DIRECTIONAL"), and derived ("ISOTROPIC") images. Note that the Dimension Index Values (0020,9157) tag provides an alternative approach to discriminate these images. Here are sample tags from a Philips enhanced image that includes and ADC map (3rd dimension is "1" while the other images set this to "2").

```
(0018,9075) CS [DIRECTIONAL]
(0020,9157) UL 1\1\2\32
...
(0018,9075) CS [ISOTROPIC]
(0020,9157) UL 1\2\1\33
...
(0018,9075) CS [NONE]
(0020,9157) UL 1\1\2\33
```

## Diffusion Direction

Some Philips enhanced scans use tag 0018,9089 to report the 3 gradient directions. Other files use the tags 2005,10b0, 2005,10b1, 2005,10b2. In general, dcm2niix will use the values that most closely precede the Dimension Index Values (0020,9157).

## General variations

The tags SliceNumberMrPhilips (2001,100A), NumberOfSlicesMrPhilips (2001,1018), and InStackPositionNumber (0020,9057) MRImageGradientOrientationNumber (2005,1413) are all potentially very useful for simplifying handling of enhanced DICOM images. However, none of these are reliably stored in all DICOM images. Therefore, dcm2niix does not depend on any of these, though it attempts to use several of these if they are available.