## About

dcm2niix attempts to convert Philips PAR/REC format images to NIfTI. While this format remains popular with users, it is slowly being superceded by Philips enhanced DICOM format, an XML/REC format as well as the direct NIfTI export. Note that dcm2niix does not support the XML/REC format.


According to [Matthew Clemence](https://www.nitrc.org/forum/forum.php?thread_id=9319&forum_id=4703) DICOM (classic and enhanced) and XML/REC are supported in the base product, NIFTI forms part of a Neuroscience commercial option from release 5 onwards. PAR/REC requires a research agreement to obtain. For the two formats XML/REC and PAR/REC, the "REC" part is identical but instead of a plain text file of the "par" format, the same information is now available as an XML file. This descision has been taken to allow the information to be more easily extended as the PAR file was getting increasingly limited.

## dcm2niix Limitations

Be aware that dcm2niix assumes that the data is stored in complete 3D volumes. It will not convert datasets where the scan is interrupted mid-volume (e.g. where the number of 2D slices is not divisible by the number of slices in a volume). This can occur if the user aborts a sequence part way through acquisition. If dcm2niix detects this situation it will suggest you use [dicm2nii](https://www.mathworks.com/matlabcentral/fileexchange/42997-dicom-to-nifti-converter--nifti-tool-and-viewer) which can handle these files.


