## About

dcm2niix attempts to convert Siemens DICOM format images to NIfTI. This page describes some vendor-specific details.

## Siemens X-Series

Siemens MR is named by Series, Generation, Major Version and Minor Version. Prior to the Siemens Vida, all contemporary Siemens MRI systems (Trio, Prisma, Skyra, etc) were part of the V series. So a Trio might be on VB17, and a Prisma on VE11 (series 'V', generation 'E', major version '1', minor version '1'). The 3T Vida and 1.5T Sola introduce the X-series (XA10, XA11, XA20). Since the V-series was dominant for so long, most users simply omit the series, e.g. referring to a system as `B19`. However, Siemens has recently introduced a new X-series.

The DICOM images exported by the X-series is radically different than the V-series. The images lack the proprietary CSA header with its rich meta data.  

X-series users are strongly encouraged to export data using the "Enhanced" format and to not use any of the "Anonymize" features on the console. The consequences of these options is discussed in detail in [issue 236](https://github.com/rordenlab/dcm2niix/issues/236). Siemens notes `We highly recommend that the Enhanced DICOM format be used. This is because this format retains far more information in the header`. Failure to export data in this format has led to catastrophic data loss for numerous users (for publicly reported details see issues [203](https://github.com/rordenlab/dcm2niix/issues/203), [236](https://github.com/rordenlab/dcm2niix/issues/236), [240](https://github.com/rordenlab/dcm2niix/issues/240), [274](https://github.com/rordenlab/dcm2niix/issues/274), [303](https://github.com/rordenlab/dcm2niix/issues/303), [370](https://github.com/rordenlab/dcm2niix/issues/370), [394](https://github.com/rordenlab/dcm2niix/issues/394)). This reflects limitations of the DICOM data, not dcm2niix.

While X-series consoles allow users to export data as enhanced, mosaic or classic 2D formats, choosing an option other than enhanced dramatically degrades the meta data. Note that the Siemens considers mosaic images `secondary capture` data intended for quality assurance only. The mosaic scans lack several "Type 1" DICOM properties, necessarily limiting conversion. The non-mosaic 2D enhanced DICOMs are compact and efficient, but appear to have limited details relative to the enhanced output. This is unfortunate, as for the V-series the mosaic format has major benefits, so users may be in the habit of prefering mosaic export. Finally, each of the formats (enhanced, mosaic, classic) can be exported as anonymized. The Siemens console anonymization of current XA10A (Fall 2018) strips many useful tags. Siemens suggests `the use an offline/in-house anonymization software instead`. Another limitation of the current X-series format is that it retains no versioning details beyond the minor version for software and hardware stepping (e.g. versions are merely XA10 or XA11 with no details for service packs). If you use a X-series, you are strongly encouraged to manually log every hardware or software upgrade to allow future analyses to identify and regress out any effects of these modifications.  Since the X-series format does not have a CSA header, dcm2niix will attempt to use the new private DICOM tags to populate the BIDS file. These tags are described in [issue 240](https://github.com/rordenlab/dcm2niix/issues/240).

When creating enhanced DICOMs diffusion information is provided in public tags. Based on a limited sample, it seems that classic DICOMs do not store diffusion data for XA10, and use private tags for [XA11](https://www.nitrc.org/forum/forum.php?thread_id=10013&forum_id=4703).

Public Tags
```
(0018,9089) FD -0.20\-0.51\-0.83 #DiffusionGradientOrientation
(0018,9087) FD 1000 #DiffusionBValue

```

Private Tags
```
(0019,100c) IS 1000 #SiemensDiffusionBValue
(0019,100e) FD -0.20\-0.51\-0.83 #SiemensDiffusionGradientOrientation

```

In theory, the public DICOM tag 'Frame Acquisition Date Time' (0018,9074) and the private tag 'Time After Start' (0021,1104) should each allow one to infer slice timing. The tag 0018,9074 uses the DT (date time) format, for example "20190621095520.330000" providing the YYYYYMMDDHHMMSS. Unfortunately, the Siemens de-identification routines will scramble these values, as time of data could be considered an identifiable attribute. The tag 0021,1104 is saved in DS (decimal string) format, for example "4.635" reporting the number of seconds since acquisition started. Be aware that some [Siemens Vida multi-band sequences](https://github.com/rordenlab/dcm2niix/issues/303) appear to fill these tags with the single-band times rather than the actual acquisition times. Therefore, neither of these two methods is perfectly reliable in determining slice timing.

## CSA Header

Many crucial Siemens parameters are stored in the [proprietary CSA header](http://nipy.org/nibabel/dicom/siemens_csa.html), in particular the CSA Image Header Info (0029, 1010) and CSA Series Header Info (0029, 1020). These have binary sections that allows quick reading for many useful parameters. They also include an ASCII text portion that includes a lot of information but is slow to parse and poorly curated. Be aware that Siemens Vida scanners do not generate a CSA header.

## Slice Timing

See the [dcm_qa_stc](https://github.com/neurolabusc/dcm_qa_stc) repository with sample data that exhibits different methods used by Siemens to record slice timing.

Older software (e.g. A25 through B13) sometimes populates the tag sSliceArray.ucMode in the [CSA Series Header (0029, 1020)](https://nipy.org/nibabel/dicom/siemens_csa.html) where the values [1, 2, and 4](https://github.com/xiangruili/dicm2nii/issues/18) correspond to Ascending, Descending and Interleaved acquisitions.

For software versions B15 through E11 where all slices of a volume are stored as a single mosaic file, the proprietary [CSA Image Header (0029,1010)](https://nipy.org/nibabel/dicom/siemens_csa.html) contains the array MosaicRefAcqTimes that provides [slice timing](https://www.mccauslandcenter.sc.edu/crnl/tools/stc). For volumes where each 2D slice is saved as a separate DICOM file, one can infer slice order from the DICOM tag Acquisition Time (0008,0032).

 The prior section describes Vida slice timing issues seen with the XA software series. In brief, dcm2niix will use Frame Acquisition Time (0018,9074) to determine slice times. Some Siemens DICOMs store slice timings in the private tag [0019,1029](https://github.com/rordenlab/dcm2niix/issues/296). In theory, this could be used when the CSA header is missing. For archival studies, be aware that some sequences [incorrectly reported slice timing](https://github.com/rordenlab/dcm2niix/issues/126). The [SPM slice timing wiki](https://en.wikibooks.org/w/index.php?title=SPM/Slice_Timing&stable=0#Siemens_scanners) provides further information on Siemens slice timing.

## Total Readout Time

One often wants to determine [echo spacing, bandwidth, ](https://support.brainvoyager.com/brainvoyager/functional-analysis-preparation/29-pre-processing/78-epi-distortion-correction-echo-spacing-and-bandwidth) and total read-out time for EPI data so they can be undistorted. The [Siemens validation dataset](https://github.com/neurolabusc/dcm_qa/tree/master/In/TotalReadoutTime) demonstrates that dcm2niix can accurately report these parameters - the included notes and spreadsheet describe this in more detail.

## Diffusion Tensor Notes

Diffusion specific parameters are described on the [NA-MIC](https://www.na-mic.org/wiki/NAMIC_Wiki:DTI:DICOM_for_DWI_and_DTI#Private_vendor:_Siemens) website. Gradient vectors are reported with respect to the scanner bore, and dcm2niix will attempt to re-orient these to [FSL format](http://justinblaber.org/brief-introduction-to-dwmri/) [bvec files](https://fsl.fmrib.ox.ac.uk/fsl/fslwiki/FDT/FAQ#What_conventions_do_the_bvecs_use.3F).

For Siemens V-series systems from the B-generation onward (around 2005), the most reliable way to read diffusion gradients is from the [CSA header](https://nipy.org/nibabel/dicom/siemens_csa.html). Specially, the CSA's 'DiffusionGradientDirection' and 'B_value' tags. For the X-series, the private DICOM tags B_value (0019,100c) and DiffusionGradientDirection (0019,100e) are used.

## Arterial Spin Labeling

Tools like [ExploreASL](https://sites.google.com/view/exploreasl) and [FSL BASIL](https://fsl.fmrib.ox.ac.uk/fsl/fslwiki/BASIL) can help process arterial spin labeling data. These tools require sequence details. These details differ between different sequences. If you create a BIDS JSON file with dcm2niix, the following tags will be created, using the same names used in the Siemens sequence PDFs. Note different sequences provide different values. The  [dcm_qa_asl](https://github.com/neurolabusc/dcm_qa_asl) repository provides example DICOM ASL datasets.

ep2d_pcasl, ep2d_pcasl_UI_PHC //pCASL 2D [Danny J.J. Wang](http://www.loft-lab.org)
 - LabelOffset
 - PostLabelDelay
 - NumRFBlocks
 - RFGap
 - MeanGzx10
 - PhiAdjust

tgse_pcasl //pCASL 3D [Danny J.J. Wang](http://www.loft-lab.org)
 - RFGap
 - MeanGzx10
 - T1

ep2d_pasl //PASL 2D Siemens Product
 - InversionTime
 - SaturationStopTime

tgse_pasl //PASL 3D [Siemens Product](http://adni.loni.usc.edu/wp-content/uploads/2010/05/ADNI3_Basic_Siemens_Skyra_E11.pdf)
 - BolusDuration
 - InversionTime

ep2d_fairest //PASL 2D http://www.pubmed.com/11746944 http://www.pubmed.com/21606572
 - PostInversionDelay
 - PostLabelDelay

to_ep2d_VEPCASL //pCASL 2D specific tags - Oxford (Thomas OKell)
 - InversionTime
 - BolusDuration
 - TagRFFlipAngle
 - TagRFDuration
 - TagRFSeparation
 - MeanTagGradient
 - TagGradientAmplitude
 - TagDuration
 - MaximumT1Opt
 - InitialPostLabelDelay [Array]
  
jw_tgse_VEPCASL //pCASL 3D Oxford
 - TagRFFlipAngle
 - TagRFDuration
 - TagRFSeparation
 - MaximumT1Opt
 - Tag0
 - Tag1
 - Tag2
 - Tag3
 - InitialPostLabelDelay [Array]
 
## Sample Datasets

 - [Slice timing dataset](httphttps://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Slice_timing_corrections://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage).
 - [A validation dataset for dcm2niix commits](https://github.com/neurolabusc/dcm_qa).
 - [A mixture of GE and Siemens data](https://github.com/neurolabusc/dcm_qa_nih).
 - [DTI examples](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Diffusion_Tensor_Imaging).
 - [Archival (old) examples](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Archival_MRI).
 - [Unusual examples](https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Unusual_MRI).

