#### Introduction

dcm2niix is a community effort

Like the [Brain Imaging Data Structure](https://bids.neuroimaging.io/get_involved.html), which it supports, dcm2niix is developed by the community for the community and everybody can become a part of the community.

The easiest way to contribute to dcm2niix is to ask questions you have by [generating Github issues](https://github.com/rordenlab/dcm2niix/issues) or [asking a question on the NITRC forum](https://www.nitrc.org/forum/?group_id=880). 

The code is open source, and you can share your improvements by [creating a pull request](https://github.com/rordenlab/dcm2niix/pulls) to the **development** branch. Please note that the **master** branch is always the current stable release and does not accept pull requests.
dcm2niix is a community project that has benefitted from many [contributors](https://github.com/rordenlab/dcm2niix/graphs/contributors). 

The INCF suggests indicating who is responsible for maintaining software for [stability and support](https://incf.org/incf-standards-review-criteria-v20). Therefore, below we indicate several active contributors and their primary domain of expertise. However, this list is not comprehensive, and it is noted that the project has been supported by contributions from many users. This list does not reflect magnitude of prior contributions, rather it is a non-exhaustive list of members who are actively maintaining the project.

 - Jon Clayden: (@jonclayden): [R Deployment](https://github.com/jonclayden/divest)
 - Ningfei Li : (@ningfei) CMake, AppVeyor, Travis
 - Yaroslav O. Halchenko: (@yarikoptic) Debian distributions
 - Taylor Hanayik (@hanayik): FSL integration
 - Michael Harms (@mharms): Advanced modalities
 - Roger D Newman-Norlund (@rogiedodgie): User support
 - Rob Reid (@captainnova): Clinical modalities
 - Chris Rorden (@neurolabusc): General development, user support
 
#### Validation

Regression tests do not belong in this repository. Each validation dataset (DICOM inputs in `In`, reference outputs in `Ref`, and a `batch.sh` that converts one and compares it with the other) is its own repository, added as a submodule of [dcm_validate](https://github.com/neurolabusc/dcm_validate). Synthetic cases go there too, with the script that generates them (for example `dcm_qa/make_synthetic.py`, `dcm_qa_ct/make_synthetic.py`). When a change alters output, update the affected `Ref` files in the same round and describe the change in the dataset's commit.

#### Style Guide

dcm2niix is written in C. Different programmers prefer different styles of indentation. Feel free to contribute code without being concerned about matching the style of the rest of the code. Once in a while, the code base will be automatically reformatted to make it appear more consistent for all users. This is done automatically with clang-format:

```
clang-format -i -style="{BasedOnStyle: LLVM, IndentWidth: 4, IndentCaseLabels: false, TabWidth: 4, UseTab: Always, ColumnLimit: 0}" *.cpp *.h
```

For markdown files, the convention is to have no hard-wrapping.
