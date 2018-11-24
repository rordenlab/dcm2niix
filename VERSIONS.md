## Versions

14-November-2018
 - [GE images provide more BIDS tags](https://github.com/rordenlab/dcm2niix/issues/163).
 - [Bruker enhanced DICOM support](https://github.com/rordenlab/dcm2niix/issues/241).
 - [Siemens Vida XA10 support](https://github.com/rordenlab/dcm2niix/issues/240). Note that Vida DICOM data is crippled [if the user exports as mosaics or anonymized/reduced](https://github.com/rordenlab/dcm2niix/issues/236).
 - [UIH enhanced DICOM support](https://github.com/rordenlab/dcm2niix/issues/225).
 - New DICOM [renaming](RENAMING.md) feature.

22-June-2018
 - [Fix issues where 6-June-2018 release could save Enhanced DICOM Philips bvec/bval with different order than .nii images](https://github.com/rordenlab/dcm2niix/issues/201).

6-June-2018
 - [Improved Philips PAR/REC support](https://github.com/rordenlab/dcm2niix/issues/171)
 - [Improved Philips Enhanced
 DICOM support](https://github.com/rordenlab/dcm2niix/issues/170) including saving different [real, imaginary, magnitude and phase images in a single DICOM file](https://github.com/rordenlab/dcm2niix/issues/189).
 - GE and Philips data now report [PhaseEncodingAxis](https://github.com/rordenlab/dcm2niix/issues/163) instead of PhaseEncodingDirection (these DICOMs store the dimension, but not the polarity).
 - Experimental detection of [phase encoding direction for GE](https://github.com/rordenlab/dcm2niix/issues/163). To enable compile with "MY_DEBUG_GE" flag.
 - Support for Philips Private RLE (1.3.46.670589.33.1.4.1) transfer syntax.
 - Optional support for JPEG-LS (1.2.840.10008.1.2.4.80/1.2.840.10008.1.2.4.81) transfer syntaxes (using [CharLS](https://github.com/team-charls/charls)). Requires c++14.
 - [Improved GE support](https://github.com/rordenlab/dcm2niix/issues/163)
 - Optional [lossless integer scaling](https://github.com/rordenlab/dcm2niix/issues/198) for INT16 and UINT16 DICOM images that only use a fraction of the possible range.

15-Dec-2017
 - Support [Siemens XA10 images](https://github.com/rordenlab/dcm2niix/pull/145).
 - [Ability to select specific series to convert](https://github.com/rordenlab/dcm2niix/pull/146).

4-Dec-2017
 - Handle implicit VR DICOMs where [critical values nested in sequence groups (SQ)](https://github.com/rordenlab/dcm2niix/commit/7f5649c6fe6ed366d07776aa54397b50f6641aff)
 - Better support for [PAR/REC files with segmented 3D EPI](https://github.com/rordenlab/dcm2niix/commit/66cdf2dcc60d55a6ef37f5a6db8d500d3eeb7c88).
 - Allow Protocol Name to be [empty](https://github.com/rordenlab/dcm2niix/commit/94f3129898ba83bf310c9ff28e994f29feb13068).

17-Oct-2017
 - Swap [phase-encoding direction polarity](https://github.com/rordenlab/dcm2niix/issues/125) for Siemens images where PE is in the Column direction.
 - Sort diffusion volumes by [B-value amplitude](https://www.nitrc.org/forum/forum.php?thread_id=8396&forum_id=4703) (use "-d n"/"-d y" to turn the feature off/on).
 - BIDS tag [TotalReadoutTime](https://github.com/rordenlab/dcm2niix/issues/130) handles partial fourier, Phase Resolution, etc (Michael Harms).
 - Additional [json fields](https://github.com/rordenlab/dcm2niix/issues/127).

18-Aug-2017
 - Better BVec extraction for  [PAR/REC 4.1](https://www.nitrc.org/forum/forum.php?thread_id=8387&forum_id=4703).
 - Support for [Segami Cerescan volumes](https://www.nitrc.org/forum/forum.php?thread_id=8076&forum_id=4703).

24-July-2017
 - Compiles with recent releases of [OpenJPEG](https://github.com/neurolabusc/dcm_qa/issues/5#issuecomment-317443179) for JPEG2000 support.

23-June-2017
 - [Ensure slice timing always reported for Siemens EPI](https://github.com/neurolabusc/dcm_qa/issues/4#issuecomment-310707906)
 - [Integrates validation](https://github.com/neurolabusc/dcm_qa)
 - JSON fix (InstitutionName -> InstitutionAddress)

21-June-2017
 - Read DICOM header in 1Mb segments rather than loading whole file : reduces ram usage and [faster for systems with slow io](https://github.com/rordenlab/dcm2niix/issues/104).
 - Report [TotalReadoutTime](https://github.com/rordenlab/dcm2niix/issues/98).
 - Fix JPEG2000 support in [Superbuild](https://github.com/rordenlab/dcm2niix/issues/105).

28-May-2017
 - Remove all derived images from [Philips DTI series](http://www.nitrc.org/forum/message.php?msg_id=21025).
 - Provide some [Siemens EPI sequence details](https://github.com/rordenlab/dcm2niix/issues).

28-April-2017
 - Experimental [ECAT support](https://github.com/rordenlab/dcm2niix/issues/95).
 - Updated cmake to make JPEG2000 support easier with improved Travis and AppVeyor support [Ningfei Li](https://github.com/ningfei).
 - Supports Data/Time for images that report Data/Time (0008,002A) but not separate Date and Time (0008,0022 and 0008,0032).
 - [BIDS reports SliceTiming correctly](http://www.nitrc.org/forum/message.php?msg_id=20852).
 - Options -1..-9 to control [gz compression level](https://github.com/rordenlab/dcm2niix/issues/90).
 - Includes some [PET details in the BIDS JSON sidecar](https://github.com/rordenlab/dcm2niix/issues/87).
 - Better detection of image order for Philips 4D DICOM (reported by Jason McMorrow and Stephen Wilson).
 - [Include StudyInstanceUID and SeriesInstanceUID in filename](https://github.com/rordenlab/dcm2niix/issues/94).

7-Feb-2017
 - Can be compiled to use either Philips [Float or Display](http://www.nitrc.org/forum/message.php?msg_id=20213) intensity intercept and slope values.
 - Handle 3D Philips DICOM and [PAR/REC](https://www.nitrc.org/forum/forum.php?thread_id=7707&forum_id=4703) files where images are not stored in a spatially contiguous order.
 - Handle DICOM violations where icon is uncompressed but image data is compressed.
 - Best guess matrix for 2D slices (similar to dcm2nii, SPM and MRIconvert).
 - Linux (case sensitive filenames) now handles par/rec as well as PAR/REC.
 - Images with unknown phase encoding do not generate [BIDS entry](https://github.com/rordenlab/dcm2niix/issues/79).
 - Unified printMessage/printWarning/printError aids embedding in other projects, such as [divest](https://github.com/jonclayden/divest).

1-Nov-2016
 - AppVeyor Support (Ningfei Li & Chris Filo Gorgolewski)
 - Swap 3rd/4th dimensions for GE sequential multi-phase acquisitions (Niels Janssen).

10-Oct-2016
 - Restores/improves building for the Windows operating system using MinGW.

30-Sept-2016
 - Save ImageType (0x0008,0x0008) to BIDS.
 - Separate CT scans with different exposures.
 - Fixed issues where some compilers would generate erratic filenames for zero-padded series (e.g. "-f %3s").

21-Sept-2016
 - Reduce verbosity (reduce number of repeated warnings, less scary warnings for derived rather than raw images).
 - Re-enable custom output directory "-o" option broken by 30-Apr-2016 version.
 - Deal with mis-behaved GE CT images where slice direction across images is not consistent.
 - Add new BIDS fields (field strength, manufacturer, etc).
 - Philips PAR/REC conversion now reports inconsistent requested vs measured TR (due to prospect. motion corr.?).
 - GE: Locations In Acquisition (0054, 0081) is inaccurate if slices are interpolated, use Images In Acquisition (0020,1002) if available.
 - New filename options %d Series description (0008,103E), %z Sequence Name (0018,0024).
 - New filename options %a antenna (coil) number, %e echo number.
 - Initialize unused portions of NIfTI header to zero so multiple runs always produce identical results.
 - Supports 3D lossless JPEG saved as [multiple fragments](http://www.nitrc.org/forum/forum.php?thread_id=5872&forum_id=4703).

5-May-2016
 - Crop 3D T1 acquisitions (e.g. ./dcm2niix -x y ~/DICOM).

30-Apr-2016
 - Convert multiple files/folders with single command line invocation (e.g. ./dcm2niix -b y ~/tst ~/tst2).

22-Apr-2016
 - Detect Siemens Phase maps (phase image names end with "_ph").
 - Use current working directory if file name not specified.

12-Apr-2016
 - Provide override (command line option "-m y") to stack images of the same series even if they differ in study date/time, echo/coil number, or slice orientation. This mechanism allows users to concatenate images that break strict DICOM compliance.

22-Mar-2016
 - Experimental support for [DICOM datasets without DICOM file meta information](http://dicom.nema.org/dicom/2013/output/chtml/part10/chapter_7.html).

12-Dec-2015
 - Support PAR/REC FP values when possible (see PMC3998685).

11-Nov-2015
 - Minor refinements.

12-June-2015
 - Uses less memory (helpful for large datasets).

2-Feb-2015
 - Support for Visual Studio.
 - Remove dependency on zlib (now uses miniz).

1-Jan-2015
 - Images separated based on TE (fieldmaps).
 - Support for JPEG2000 using OpenJPEG or Jasper libraries.
 - Support for JPEG using NanoJPEG library.
 - Support for lossless JPEG using custom library.

24-Nov-2014
 - Support for CT scans with gantry tilt and varying distance between slices.

11-Oct-2014
 - Initial public release.