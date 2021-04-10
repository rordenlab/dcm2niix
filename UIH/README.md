## About

dcm2niix attempts to convert UIH DICOM format images to NIfTI.

## Notes

Shan C Young provided the [following information](https://github.com/rordenlab/dcm2niix/issues/225), which is used by dcm2niix to generate NIfTI and BIDS format files.

UIH supports two ways of archiving the DWI/DTI and fMRI data. One way is one DICOM file per slice and the other is one dicom file per volume (UIH refers to this as GRID format, similar to the Siemens Mosaic format). The private tags used in the images are shown in the following table.


Tag ID | Tag Name | VR | VM | Description | Sample
-- | -- | -- | -- | -- | --
0061,1002 | Generate Private | US | 1 | Flag to generate private format file | 1
0061,4002 | FOV | SH | 1 | FOV(mm) | 224*224
0065,1000 | MeasurmentUID | UL | 1 | Measurement UID of Protocol | 12547865
0065,1002 | ImageOrientationDisplayed | SH | 1 | Image Orientation Displayed | Sag or Sag>Cor
0065,1003 | ReceiveCoil | LO | 1 | Receive Coil Information | H 8
0065,1004 | Interpolation | SH | 1 | Interpolation | I
0065,1005 | PE Direction Displayed | SH | 1 | Phase encoding diretion displayed | A->P or H->F
0065,1006 | Slice Group ID | IS | 1 | Slice Group ID | 1
0065,1007 | Uprotocol | OB | 1 | Uprotocol value |  
0065,1009 | BActualValue | FD | 1 | Actual B-Value from sequence | 1000.0
0065,100A | BUserValue | FD | 1 | User Choose B-Value from UI | 1000.0
0065,100B | Block Size | DS | 1 | Size of the paradigm/block | 10
0065,100C | Experimental status | SH | 1 | fMRI | rest/active
0065,100D | Parallel Information | SH | 1 | ratio of parallel acquisition and acceleration |  
0065,100F | Slice Position | SH | 1 | Slice location displayed on the screen | H23.4
0065,1011 | Sections | SH | 1 |   |  
0065,1013 | InPlaneRotAngle | FD（°） | 1 | Rotation angle in the plane | -0.5936
0065,1014 | SliceNormalVector | DS | 3 | Normal vector of the slice | 0\0\1
0065,1015 | SliceCenterPosition | DS | 3 | Center position of the   slice | 0\0\0
0065,1016 | PixelRotateModel | UL | 1 | Pixel Rotation Model | 4
0065,1017 | SAR Model | LO | 1 | Calculation model of SAR   value | Normal:WHBST
0065,1018 | dB/dt Model | LO | 1 | Calculation model of dB/dt | Normal
0065,1023 | TablePosition | LO | 1 | Table Position | 0
0065,1025 | Slice Gap | DS | 1 | Slice Gap | 0.0
0065,1029 | AcquisitionDuration | SH | 1 | Acquisition Duration | 0.03
0065,102B | ApplicationCategory | LT | 1 | Application names available | DTI\Func
0065,102C | RepeatitionIndex | IS | 1 |   | 0
0065,102D | SequenceDisplayName | ST | 1 | Sequence display name | Epi_dti_b0
0065,102E | NoiseDecovarFlag | LO | 1 | Noise decorrelation flag | PreWhite
0065,102F | ScaleFactor | FL | 1 | scale factor | 2.125
0065,1031 | MRSequenceVariant | SH | 1 | SequenceVariant |  
0065,1032 | MRKSpaceFilter | SH | 1 | K space filter |  
0065,1033 | MRTableMode | SH | 1 | Table mode | Fix
0065,1036 | MRDiscoParameter | OB | 1 |   |  
0065,1037 | MRDiffusionGradOrientation | FD | 3 | Diffusion gradient   orientation | 0\0\0
0065,1038 | MRPerfusionNoiseLevel | FD | 1 | epi_dwi/perfusion noise   level | 40
0065,1039 | MRGradRange | SH | 6 | linear range of gradient | 0.0\157\0.0\157\0.0\125
0065,1050 | MR Number Of Slice In   Volume | DS | 1 | Number Of Frames In a   Volume，Columns of each frame: cols =ceil(sqrt(total)) ; Rows of   each frame: rows =ceil(total/cols) ;   appeared when image type   (00080008) has VFRAME | 27
0065,1051 | MR VFrame Sequence | SQ | 1 | 1 |  
 ->0008,0022 | Acquisition Date | DA | 1 |   |  
 ->0008,0032 | Acquisition Time | TM | 1 |   |  
 ->0008,002A | Acquisition DateTime | DT | 1 |   |  
 ->0020,0032 | ImagePosition(Patient) | DS | 3 |   |  
 ->00201041 | Slice Location | DS | 1 |   |  
 ->0018,9073 | Acquisition Duration | FD | 1 |   |  
 ->0065,100C | MRExperimental Status | SH | 1 |   | rest/active

## Sample Datasets

 - [UIH has provided a reference dataset](https://1drv.ms/f/s!Avf7THyflzj1gnO37GL2I8Hk-0MV).
 - [A validation dataset for dcm2niix commits](https://github.com/neurolabusc/dcm_qa_uih).