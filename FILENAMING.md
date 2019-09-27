## About

DICOM files tend to have bizarre file names, for example based on the instance UID, e.g. `MR.1.3.12.2.1107.5.2.32.35131.2014031013003871821190579`. In addition, DICOM images are often 2D slices or 3D volumes that we will combine into a single unified NIfTI file. On the other hand, some enhanced DICOM images save different reconstructions (e.g. phase and magnitude) of the same image that we will want to save as separate NIfTI files. Therefore, dcm2niix attempts to provide a sensible file naming scheme.

## Basics

You request the output file name with the `-f` argument. For example, consider you convert files with `dcm2niix -f %s_%p`: in this case an image from series 3 with the protocol name `T1` will be saved as `3_T1.nii`. Here are the available parameters for file names:

 - %a=antenna (coil) name (from Siemens 0051,100F)
 - %b=basename (file name of first DICOM)
 - %c=comments (from 0020,4000)
 - %d=description (from 0008,103E)
 - %e=echo number (from 0018,0086)
 - %f=folder name (name of folder containing first DICOM)
 - %i=ID of patient (from 0010,0020)
 - %j=seriesInstanceUID (from 0020,000E)
 - %k=studyInstanceUID (from 0020,000D)
 - %m=manufacturer short name (from 0008,0070: GE, Ph, Si, To, UI, NA)
 - %n=name of patient (from 0010,0010)
 - %o=mediaObjectInstanceUID (0002,0003)
 - %p=protocol name (from 0018,1030)
 - %r=instance number (from 0020,0013)
 - %s=series number (from 0020,0011)
 - %t=time of study (from 0008,0020 and 0008,0030)
 - %u=acquisition number (from 0020,0012)
 - %v=vendor long name (from 0008,0070: GE, Philips, Siemens, Toshiba, UIH, NA)
 - %x=study ID (from 0020,0010)
 - %z=sequence name (from 0018,0024)

## File Name Post-fixes: Image Disambiguation

In general dcm2niix creates images with 3D dimensions, or 4 dimensions when the 4th dimension is time (fMRI) or gradient number (DWI). It will use the following extensions to disambiguate additional dimensions from the same series:

 - _cNx.._cNz  where C* refers to the coil name (typically only seen for uncombined data, where a separate image is generated for each antenna)
 - _e1..eN echo number for multi-echo sequences
 - _ph phase map
 - _imaginary imaginary component of complex image
 - _real real component of complex image
 - _phMag rare case where phase and magnitude are saved as the 4th dimension
 - _t  If the trigger delay time (0020,9153) is non-zero, it will be recorded in the file name. For example, the files "T1_t178.nii" and "T1_t511" suggests that the T1 scan was acquired with two cardiac trigger delays (178 and 511ms after the last R-peak).
 - _ADC Philips specific case. A DWI image where derived isotropic, ADC or trace volume was appended to the series. Since this image will disrupt subsequent processing, and because subsequent processing (dwidenoise, topup, eddy) will yield better derived images, dcm2niix will also create an additional image without this volume. Therefore, the _ADC file should typically be discarded. If you want dcm2niix to discard these useless derived images, use the ignore feature ('-i y').
 - _Eq is specific to [CT scans](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Computed_Tomography_.28CT.2C_CAT.29). These scans can be acquired with variable distance between the slices of a 3D volume. NIfTI asumes all 2D slices that form a 3D stack are equidistant. Therefore, dcm2niix reslices the input data to generate an equidistant volume.
 - _Tilt is specific to [CT scans](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Computed_Tomography_.28CT.2C_CAT.29). These scans can be acquired with a gantry tilt that causes a skew that can not be stored in a NIfTI qForm. Therefore, the slices are resampled to remove the effect of tilt.
 - _MoCo is appended to the ProtocolName if Image Type (0008,0008) includes the term 'MOCO'. This helps disambiguate Siemens fMRI runs where both motion corrected and raw data is stored for a single session.

## File Name Conflicts

dcm2niix will attempt to write your image using the naming scheme you specify with the '-f' parameter. However, if an image already exists with the specified output name, dcm2niix will append a letter (e.g. 'a') to the end of a file name to avoid overwriting existing images. Consider a situation where dcm2niix is run with '-f %t'. This will name images based on the study time. If a single study has multiple series (for example, a T1 sequence and a fMRI scan, the reulting file names will conflict with each other. In order to avoid overwriting images, dcm2niix will resort to adding the post fix 'a', 'b', etc. There are a few solutions to avoiding these situations. You may want to consider using both of these:
 - Make sure you specify a naming scheme that can discriminate between your images. For example '-f %t' will not disambiguate different series acquired in the same session. However, '-f %t_%s' will discriminate between series.
 - Localizer (scout) images are the first scans acquired for any scanning session, and are used to plan the location for subsequent images. Localizers are not used in subsequent analyses (due to resolution, artefacts, etc). Localizers are often acquired with three orthogonal image planes (sagittal, coronal and axial). The NIfTI format requires that all slices in a volume are co-planar, so these localizers will generate naming conflicts. The solution is to use '-i y' which will ignore (not convert) localizers (it will also ignore derived images and 2D slices). This command helps exclude images that are not required for subsequent analyses.
 - Be aware that if you run dcm2niix twice with the same parameters on the same data, you will encounter file naming conflicts.

## Special Characters

[Some characters are not permitted](https://stackoverflow.com/questions/1976007/what-characters-are-forbidden-in-windows-and-linux-directory-names) in file names. The following characters will be replaced with underscorces (`_`). Note that the forbidden characters vary between operating systems (Linux only forbids the forward slash, MacOS forbids forward slash and colon, while Windows forbids any of the characters listed below). To ensure that files can be easily copied between file systems, [dcm2niix restricts file names to characters allowed by Windows](https://github.com/rordenlab/dcm2niix/issues/237).

### List of Forbidden Characters (based on Windows)
```
< (less than)
> (greater than)
: (colon - sometimes works, but is actually NTFS Alternate Data Streams)
" (double quote)
/ (forward slash)
\ (backslash)
| (vertical bar or pipe)
? (question mark)
* (asterisk)
```

Be warned that dcm2niix will copy all allowed characters verbatim, which can cause problems for some other tools. Consider this [sample dataset](https://github.com/neurolabusc/dcm_qa_nih/tree/master/In/20180918GE/mr_0004) where the DICOM Protocol Name (0018,1030) is 'Axial_EPI-FMRI_(Interleaved_I_to_S)'. The parentheses ("round brackets") may cause other tools issues. Consider converting this series with the command 'dcm2niix -f %s_%p ~/DICOM' to create the file '4_Axial_EPI-FMRI_(Interleaved_I_to_S).nii'.If you now run the command 'fslhd 4_Axial_EPI-FMRI_(Interleaved_I_to_S).nii' you will get the error '-bash: syntax error near unexpected token `(''. Therefore, it is often a good idea to use double quotes to specify the names of files. In this example 'fslhd "4_Axial_EPI-FMRI_(Interleaved_I_to_S).nii"' will work correctly.