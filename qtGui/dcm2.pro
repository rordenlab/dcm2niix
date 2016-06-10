#-------------------------------------------------
#
# Project created by QtCreator 2014-04-22T13:13:08
#
#-------------------------------------------------

QT       += core gui
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

DEFINES += UNICODE

#link zlib for Windows OS see https://gitorious.org/tiled/stefanbellers-tiled-qt/commit/2f9cb77ae082223ef2324965cd8f45a2f245098c
#INCLUDEPATH += $$[QT_INSTALL_PREFIX]/src/3rdparty/zlib
#INCLUDEPATH += ../zlib
LIBS += -lz

TARGET = dcm2
TEMPLATE = app

#our .c files have c++ code, but old versions of xcode will complain if we use .cpp instead of .c
QMAKE_CFLAGS +=  -x c++
QMAKE_CXXFLAGS += -x c++


#we need to redirect the std:cout instead of printf
QMAKE_CFLAGS +=  -DmyUseCOut

SOURCES += main.cpp\
        mainwindow.cpp \
    nifti1_io_core.c \
   nii_dicom_batch.c \
    nii_dicom.c \
    nii_ortho.c \

HEADERS  += mainwindow.h \
    nifti1_io_core.h \
    nifti1.h \
    nii_dicom_batch.h \
    nii_dicom.h \
    nii_ortho.h \
    tinydir.h \
    Q_DebugStream.h

FORMS    += mainwindow.ui
