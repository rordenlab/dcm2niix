## About

dcm2niix attempts to convert all DICOM images to NIfTI. However, different manufacturers handle the format differently. [Mediso](https://mediso.com/usa/en/) is a manufacturer that supports preclinical tools for PET, MRI, SPECT and CT.

In general, this manufacturer uses public tags and generates simple DICOM headers. While these files do not contain the rich meta data available from other manufacturers, they are simple to parse.
 
## Sample Datasets

 - The [ftp://medical.nema.org/MEDICAL](ftp://medical.nema.org/MEDICAL)  server provides reference images in Dicom/Datasets/WG30/Mediso
