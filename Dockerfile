FROM ubuntu:trusty
MAINTAINER <alexcohen@gmail.com> # feel free to change/adopt

ENV http_proxy 'http://proxy.tch.harvard.edu:3128'
ENV https_proxy 'http://proxy.tch.harvard.edu:3128'

# Install Dependencies
RUN apt-get update && apt-get upgrade -y && \
	apt-get install -y build-essential pkg-config libyaml-cpp-dev libyaml-cpp0.5 cmake libboost-dev git pigz && \
	apt-get clean -y && apt-get autoclean -y && apt-get autoremove -y

# Get dcm2niix from github and compile
RUN cd /tmp && \
	git clone https://github.com/rordenlab/dcm2niix.git && \
	cd dcm2niix && mkdir build && cd build && \
	cmake -DBATCH_VERSION=ON .. && \
	make && make install

# Get dcm2niix from github and compile (without cmake)
# RUN cd /tmp && \
# 	git clone https://github.com/rordenlab/dcm2niix.git && \
# 	cd dcm2niix/console/ && \
# 	g++ -O3 -I. main_console.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp  -o dcm2niix -DmyDisableOpenJPEG -DmyDisableJasper && \
# 	cp dcm2niix /usr/local/bin/ 

ENTRYPOINT ["/usr/local/bin/dcm2niix"]