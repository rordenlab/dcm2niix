## About

In general dcm2niix creates images with 3D dimensions, or 4 dimensions when the 4th dimension is time (fMRI) or gradient number (DWI). It will use the following extensions to disambiguate additional dimensions from the same series:

 - _c1.._cN  coil ID (only seen if saved independently for each coil)
 - _e2..eN additional echoes (the first echo is implicit)
 - _ph phase map
 - _imaginary imaginary component of complex image
 - _real real component of complex image
 - _phMag rare case where phase and magnitude are saved as the 4th dimension
 - _t  If the trigger delay time (0020,9153) is non-zero, it will be recorded in the filename. For example, the files "T1_t178.nii" and "T1_t511" suggests that the T1 scan was acquired with two cardiac trigger delays (178 and 511ms after the last R-peak).
 - _ADC Philips specific case DWI image where derived isotropic, ADC or trace volume was appended to the series. Since this image will disrupt subsequent processing, and because subsequent processing (dwidenoise, topup, eddy) will yield better derived images, dcm2niix will also create an additional image without this volume. Therefore, the _ADC file should typically be discarded. If you want dcm2niix to discard these useless derived images , use the ignore feature ('-i y').
 - _Eq is specific to [CT scans](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Computed_Tomography_.28CT.2C_CAT.29). These scans can be acquired with variable distance between the slices of a 3D volume. NIfTI asumes all 2D slices that form a 3D stack are equidistant. Therefore, dcm2niix reslices the input data to generate an equidistant volume.
 - _Tilt is specific to [CT scans](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Computed_Tomography_.28CT.2C_CAT.29). These scans can be acquired with a gantry tilt that causes a skew that can not be stored in a NIfTI qForm. Therefore, the slices are resampled to remove the effect of tilt.
