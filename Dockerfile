FROM ubuntu:trusty
MAINTAINER <alexcohen@gmail.com> # feel free to change/adopt

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

ENTRYPOINT ["/usr/local/bin/dcm2niix"]
