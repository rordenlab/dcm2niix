## About

dcm2niix attempts to convert Philips PAR/REC format images to NIfTI. While this format remains popular with users, it is slowly being superceded by Philips enhanced DICOM format as well as the direct NIfTI export that Philips provides for users.

## dcm2niix Limitations

None of the major contributors to the dcm2niix source code have access to Philips hardware. Therefore, one should be cautious when converting Philips PAR/REC data. Two situations will generate error reports from the current version of dcm2niix. If you encounter any of these situations you may want to consider filing a GitHub issue or trying an alternative such as [dicm2nii](https://www.mathworks.com/matlabcentral/fileexchange/42997-dicom-to-nifti-converter--nifti-tool-and-viewer) or [r2agui](http://r2agui.sourceforge.net/).

 - dcm2niix assumes that the data is stored in complete 3D volumes. It will not convert datasets where the scan is interrupted mid-volume (e.g. where the number of 2D slices is not divisible by the number of slices in a volume).
 - dcm2niix is aware of phase maps and magnitude maps. In theory (though we have never seen any examples), the format can also store real, imaginary and subsequent calculations such as B1. These may or may not be converted correctly.

