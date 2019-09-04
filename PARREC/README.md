## About

dcm2niix attempts to convert Philips PAR/REC format images to NIfTI. While this format remains popular with users, it is slowly being superceded by Philips enhanced DICOM format, an XML/REC format as well as the direct NIfTI export. Note that dcm2niix does not support the XML/REC format.


According to [Matthew Clemence](https://www.nitrc.org/forum/forum.php?thread_id=9319&forum_id=4703) DICOM (classic and enhanced) and XML/REC are supported in the base product, NIFTI forms part of a Neuroscience commercial option from release 5 onwards. PAR/REC requires a research agreement to obtain. For the two formats XML/REC and PAR/REC, the "REC" part is identical but instead of a plain text file of the "par" format, the same information is now available as an XML file. This descision has been taken to allow the information to be more easily extended as the PAR file was getting increasingly limited.

## Detecting, Reporting and Fixing the V4 Image offcentre Bug

The PAR header contains a field 'image offcentre (ap,fh,rl in mm )' that we use to detect the spatial position of slices (e.g. for an axial scan is the first slice inferior or superior to the final slice). However, it appears that in some V4 images the values in these columns are actually stored in the order "rl,ap,fh". This has never been reported in V3, V4.1 and V4.2 images. A nice example of this is the ['philips_1_5T_intera' dataset provided with Rosetta Bit](https://www.nitrc.org/projects/rosetta/)(actually from a 3T MRI). This sample includes both DICOM and V4 PAR/REC data. Note the 'Off Centre midslice(ap,fh,rl) [mm]' field gives the volume center in the correct order. However, the subsequent 'image offcentre' fields are swizzled. The latest versions of dcm2niix will detect, report and correct this error. If you do see an error like the one below, please report it on Github as an issue, so we can have a better understanding of its prevalence.

```
Order of 'image offcentre (ap,fh,rl in mm )' appears incorrect (assuming rl,ap,fh)
 err[ap,fh,rl]= 12.7279 (-7.05 -4.55 2.95)
 err[rl,ap,fh]= 0.0223597 (-4.55 2.95 -7.05)
```

## File naming

You can specify the preferred name for your output file with dcm2niix. You do this by passing an argument string (default is '%f_%p_%t_%s'). For example, If you run "dcm2niix -f %p_%s ~/myParDir" the output name will be based on the protocol name and the series name. Here is a list of the possible argument you can use with Philips, and the tag from the header that is used for this value:

- %c: comment : "Examination name"
- %d: description : "Series Type"
- %e: echo number : "echo number" Column
- %f: folder name
- %i: ID : "Technique"
- %m: manufacturer : always 'Ph' for Philips
- %n: name : "Patient name"
- %p: protocol name : "Protocol name"
- %s: series number : "Acquisition nr"
- %t: time : "Examination date/time"
- %u: acquisition number : "Acquisition nr"
- %v: vendor : always 'Philips' for Philips

Note that for Philips (unlike DICOM) the For PAR/REC the acquisition (%u) and series (%s) numbers are the same. Also note that there are several arguments that might be useful with DICOM but will always be unused for PAR/REC:

 - %a: antenna
 - %j: seriesInstanceUID
 - %k: studyInstanceUID
 - %x: study ID
 - %z: sequence name

## dcm2niix Limitations

Be aware that dcm2niix assumes that the data is stored in complete 3D volumes. It will not convert datasets where the scan is interrupted mid-volume (e.g. where the number of 2D slices is not divisible by the number of slices in a volume). This can occur if the user aborts a sequence part way through acquisition. If dcm2niix detects this situation it will suggest you use [dicm2nii](https://www.mathworks.com/matlabcentral/fileexchange/42997-dicom-to-nifti-converter--nifti-tool-and-viewer) which can handle these files.


