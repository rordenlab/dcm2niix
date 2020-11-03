## About

dcm2niix will return an exit status to allow scripts to determine if a conversion was successful. Following Unix convention, the value value 0 is used for [EXIT_SUCCESS](https://www.gnu.org/software/libc/manual/html_node/Exit-Status.html). In contrast, any non-zero result suggests an error. In the Unix terminal and with shell scripts, the variable `$?` reports the most recent exit status. Here is an example of a successful conversion:

```
>dcm2niix ~/dcm
Chris Rorden's dcm2niiX version v1.0.20200331 Clang11.0.0 (64-bit MacOS)
Found 2 DICOM file(s)
Convert 2 DICOM as ~/dcm/dcm_ax_asc_6 (64x64x35x2)
Conversion required 0.015866 seconds (0.012676 for core code).
>echo $?
0
```

Below is a list of possible return values from running dcm2niix. 

| Exit Status | Meaning                                                     |
| ----------- | ----------------------------------------------------------- |
| 0           | Success                                                     |
| 1           | Unspecified error (see console output for details)          |
| 2           | No DICOM images found in input folder                       |
| 3           | Exit from report version (result of `dcm2niix -v`)          |
| 4           | Corrupt DICOM file (Irrecoverable error during conversion)  |
| 5           | Input folder invalid                                        |
| 6           | Output folder invalid                                       |
| 7           | Unable to write to output folder (check file permissions)   |
| 8           | Converted some but not all of the input DICOMs              |
| 9           | Unable to rename files (result of `dcm2niix -r y ~/in`)     |

