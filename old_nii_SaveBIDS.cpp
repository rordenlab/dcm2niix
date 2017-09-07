void nii_SaveBIDS(char pathoutname[], struct TDICOMdata d, struct TDCMopts opts, struct nifti_1_header *h, const char * filename) {
//https://docs.google.com/document/d/1HFUkAEE-pB-angVcYe6pf_-fVf4sCpOHKesUvfb8Grc/edit#
// Generate Brain Imaging Data Structure (BIDS) info
// sidecar JSON file (with the same  filename as the .nii.gz file, but with .json extension).
// we will use %g for floats since exponents are allowed
// we will not set the locale, so decimal separator is always a period, as required
//  https://www.ietf.org/rfc/rfc4627.txt
	if (!opts.isCreateBIDS) return;
	char txtname[2048] = {""};
	strcpy (txtname,pathoutname);
	strcat (txtname,".json");
	FILE *fp = fopen(txtname, "w");
	fprintf(fp, "{\n");
	switch (d.modality) {
		case kMODALITY_CR:
			fprintf(fp, "\t\"Modality\": \"CR\",\n" );
			break;
		case kMODALITY_CT:
			fprintf(fp, "\t\"Modality\": \"CT\",\n" );
			break;
		case kMODALITY_MR:
			fprintf(fp, "\t\"Modality\": \"MR\",\n" );
			break;
		case kMODALITY_PT:
			fprintf(fp, "\t\"Modality\": \"PT\",\n" );
			break;
		case kMODALITY_US:
			fprintf(fp, "\t\"Modality\": \"US\",\n" );
			break;
	};
	switch (d.manufacturer) {
		case kMANUFACTURER_SIEMENS:
			fprintf(fp, "\t\"Manufacturer\": \"Siemens\",\n" );
			break;
		case kMANUFACTURER_GE:
			fprintf(fp, "\t\"Manufacturer\": \"GE\",\n" );
			break;
		case kMANUFACTURER_PHILIPS:
			fprintf(fp, "\t\"Manufacturer\": \"Philips\",\n" );
			break;
		case kMANUFACTURER_TOSHIBA:
			fprintf(fp, "\t\"Manufacturer\": \"Toshiba\",\n" );
			break;
	};
	fprintf(fp, "\t\"ManufacturersModelName\": \"%s\",\n", d.manufacturersModelName );
	if (!opts.isAnonymizeBIDS) {
		if (strlen(d.seriesInstanceUID) > 0)
			fprintf(fp, "\t\"SeriesInstanceUID\": \"%s\",\n", d.seriesInstanceUID );
		if (strlen(d.studyInstanceUID) > 0)
			fprintf(fp, "\t\"StudyInstanceUID\": \"%s\",\n", d.studyInstanceUID );
		if (strlen(d.referringPhysicianName) > 0)
			fprintf(fp, "\t\"ReferringPhysicianName\": \"%s\",\n", d.referringPhysicianName );
		if (strlen(d.studyID) > 0)
			fprintf(fp, "\t\"StudyID\": \"%s\",\n", d.studyID );
		//Next lines directly reveal patient identity
		//if (strlen(d.patientName) > 0)
		//	fprintf(fp, "\t\"PatientName\": \"%s\",\n", d.patientName );
		//if (strlen(d.patientID) > 0)
		//	fprintf(fp, "\t\"PatientID\": \"%s\",\n", d.patientID );
	}
	#ifdef myReadAsciiCsa
	if ((d.manufacturer == kMANUFACTURER_SIEMENS) && (d.CSA.SeriesHeader_offset > 0) && (d.CSA.SeriesHeader_length > 0)) {
		//&& (strlen(d.scanningSequence) > 1) && (d.scanningSequence[0] == 'E') && (d.scanningSequence[1] == 'P')) { //for EPI scans only
		int partialFourier, echoSpacing, echoTrainDuration, epiFactor, parallelReductionFactorInPlane;
		char fmriExternalInfo[kDICOMStr], coilID[kDICOMStr], consistencyInfo[kDICOMStr], coilElements[kDICOMStr], pulseSequenceDetails[kDICOMStr];
		epiFactor = siemensCsaAscii(filename,  d.CSA.SeriesHeader_offset, d.CSA.SeriesHeader_length, &partialFourier, &echoSpacing, &echoTrainDuration, &parallelReductionFactorInPlane, coilID, consistencyInfo, coilElements, pulseSequenceDetails, fmriExternalInfo);
		//printMessage("ES %d ETD %d EPI %d\n", echoSpacing, echoTrainDuration, epiFactor);
		if (partialFourier > 0) {
			//https://github.com/ismrmrd/siemens_to_ismrmrd/blob/master/parameter_maps/IsmrmrdParameterMap_Siemens_EPI_FLASHREF.xsl
			float pf = 1.0f;
			if (partialFourier == 1) pf = 0.5;
			if (partialFourier == 2) pf = 0.75;
			if (partialFourier == 4) pf = 0.875;
			fprintf(fp, "\t\"PartialFourier\": %g,\n", pf);
		}
		if (echoSpacing > 0)
			 fprintf(fp, "\t\"EchoSpacing\": %g,\n", echoSpacing / 1000000.0); //usec -> sec
		if (echoTrainDuration > 0)
			 fprintf(fp, "\t\"EchoTrainDuration\": %g,\n", echoTrainDuration / 1000000.0); //usec -> sec
		if (epiFactor > 0)
			 fprintf(fp, "\t\"EPIFactor\": %d,\n", epiFactor);
		if (strlen(coilID) > 0)
			fprintf(fp, "\t\"ReceiveCoilName\": \"%s\",\n", coilID);
		if (strlen(coilElements) > 0)
			fprintf(fp, "\t\"ReceiveCoilActiveElements\": \"%s\",\n", coilElements);
		if (strlen(pulseSequenceDetails) > 0)
			fprintf(fp, "\t\"PulseSequenceDetails\": \"%s\",\n", pulseSequenceDetails);
		if (strlen(fmriExternalInfo) > 0)
			fprintf(fp, "\t\"FmriExternalInfo\": \"%s\",\n", fmriExternalInfo);
		if (strlen(consistencyInfo) > 0)
			fprintf(fp, "\t\"ConsistencyInfo\": \"%s\",\n", consistencyInfo);
		if (parallelReductionFactorInPlane > 0) {//AccelFactorPE -> phase encoding
			if (d.accelFactPE < 1.0) d.accelFactPE = parallelReductionFactorInPlane; //value found in ASCII but not in DICOM (0051,1011)
			if (parallelReductionFactorInPlane != round(d.accelFactPE))
				printWarning("ParallelReductionFactorInPlane reported in DICOM [0051,1011] (%g) does not match CSA series value %g\n", round(d.accelFactPE), parallelReductionFactorInPlane);
		}
	}
	#endif
	if (d.CSA.multiBandFactor > 1) //AccelFactorSlice
		fprintf(fp, "\t\"MultibandAccelerationFactor\": %d,\n", d.CSA.multiBandFactor);
	if (strlen(d.imageComments) > 0)
		fprintf(fp, "\t\"ImageComments\": \"%s\",\n", d.imageComments);
	if (strlen(opts.imageComments) > 0)
		fprintf(fp, "\t\"ConversionComments\": \"%s\",\n", opts.imageComments);
	if (d.echoTrainLength > 1) //>1 as for Siemens EPI this is 1, Siemens uses EPI factor http://mriquestions.com/echo-planar-imaging.html
		fprintf(fp, "\t\"EchoTrainLength\": %d,\n", d.echoTrainLength);
	if (d.echoNum > 1)
		fprintf(fp, "\t\"EchoNumber\": %d,\n", d.echoNum);
	if (d.isDerived) //DICOM is derived image or non-spatial file (sounds, etc)
		fprintf(fp, "\t\"RawImage\": false,\n");
	if (d.acquNum > 0)
		fprintf(fp, "\t\"AcquisitionNumber\": %d,\n", d.acquNum);
	if (strlen(d.institutionName) > 0)
		fprintf(fp, "\t\"InstitutionName\": \"%s\",\n", d.institutionName );
	if (strlen(d.institutionAddress) > 0)
		fprintf(fp, "\t\"InstitutionAddress\": \"%s\",\n", d.institutionAddress );
	if (strlen(d.deviceSerialNumber) > 0)
		fprintf(fp, "\t\"DeviceSerialNumber\": \"%s\",\n", d.deviceSerialNumber );
	if (strlen(d.stationName) > 0)
		fprintf(fp, "\t\"StationName\": \"%s\",\n", d.stationName );
	if (strlen(d.scanOptions) > 0)
		fprintf(fp, "\t\"ScanOptions\": \"%s\",\n", d.scanOptions );
	if (strlen(d.softwareVersions) > 0)
		fprintf(fp, "\t\"SoftwareVersions\": \"%s\",\n", d.softwareVersions );
	if (strlen(d.procedureStepDescription) > 0)
		fprintf(fp, "\t\"ProcedureStepDescription\": \"%s\",\n", d.procedureStepDescription );
	if (strlen(d.scanningSequence) > 0)
		fprintf(fp, "\t\"ScanningSequence\": \"%s\",\n", d.scanningSequence );
	if (strlen(d.sequenceVariant) > 0)
		fprintf(fp, "\t\"SequenceVariant\": \"%s\",\n", d.sequenceVariant );
	if (strlen(d.seriesDescription) > 0)
		fprintf(fp, "\t\"SeriesDescription\": \"%s\",\n", d.seriesDescription );
	if (strlen(d.bodyPartExamined) > 0)
		fprintf(fp, "\t\"BodyPartExamined\": \"%s\",\n", d.bodyPartExamined );
	if (strlen(d.protocolName) > 0)
		fprintf(fp, "\t\"ProtocolName\": \"%s\",\n", d.protocolName );
	if (strlen(d.sequenceName) > 0)
		fprintf(fp, "\t\"SequenceName\": \"%s\",\n", d.sequenceName );
	if (strlen(d.imageType) > 0) {
		fprintf(fp, "\t\"ImageType\": [\"");
		bool isSep = false;
		for (int i = 0; i < strlen(d.imageType); i++) {
			if (d.imageType[i] != '_') {
				if (isSep)
		  			fprintf(fp, "\", \"");
				isSep = false;
				fprintf(fp, "%c", d.imageType[i]);
			} else
				isSep = true;
		}
		fprintf(fp, "\"],\n");
	}
	//Chris Gorgolewski: BIDS standard specifies ISO8601 date-time format (Example: 2016-07-06T12:49:15.679688)
	//Lines below directly save DICOM values
	if (d.acquisitionTime > 0.0 && d.acquisitionDate > 0.0){
		long acquisitionDate = d.acquisitionDate;
		double acquisitionTime = d.acquisitionTime;
		char acqDateTimeBuf[64];
		//snprintf(acqDateTimeBuf, sizeof acqDateTimeBuf, "%+08ld%+08f", acquisitionDate, acquisitionTime);
		snprintf(acqDateTimeBuf, sizeof acqDateTimeBuf, "%+08ld%+013.5f", acquisitionDate, acquisitionTime); //CR 20170404 add zero pad so 1:23am appears as +012300.00000 not +12300.00000
		//printMessage("acquisitionDateTime %s\n",acqDateTimeBuf);
		int ayear,amonth,aday,ahour,amin;
		double asec;
		int count = 0;
		sscanf(acqDateTimeBuf, "%5d%2d%2d%3d%2d%lf%n", &ayear, &amonth, &aday, &ahour, &amin, &asec, &count);  //CR 20170404 %lf not %f for double precision
		//printf("-%02d-%02dT%02d:%02d:%02.6f\",\n", amonth, aday, ahour, amin, asec);
		if (count) { // ISO 8601 specifies a sign must exist for distant years.
			//report time of the day only format, https://www.cs.tut.fi/~jkorpela/iso8601.html
			fprintf(fp, "\t\"AcquisitionTime\": \"%02d:%02d:%02.6f\",\n",ahour, amin, asec);
			//report date and time together
			if (!opts.isAnonymizeBIDS) {
				fprintf(fp, "\t\"AcquisitionDateTime\": ");
				fprintf(fp, (ayear >= 0 && ayear <= 9999) ? "\"%4d" : "\"%+4d", ayear);
				fprintf(fp, "-%02d-%02dT%02d:%02d:%02.6f\",\n", amonth, aday, ahour, amin, asec);

			}
		} //if (count)
	} //if acquisitionTime and acquisitionDate recorded
	// if (d.acquisitionTime > 0.0) fprintf(fp, "\t\"AcquisitionTime\": %f,\n", d.acquisitionTime );
	// if (d.acquisitionDate > 0.0) fprintf(fp, "\t\"AcquisitionDate\": %8.0f,\n", d.acquisitionDate );
	//if conditionals: the following values are required for DICOM MRI, but not available for CT
	if ((d.intenScalePhilips != 0) || (d.manufacturer == kMANUFACTURER_PHILIPS)) { //for details, see PhilipsPrecise()
		fprintf(fp, "\t\"PhilipsRescaleSlope\": %g,\n", d.intenScale );
		fprintf(fp, "\t\"PhilipsRescaleIntercept\": %g,\n", d.intenIntercept );
		fprintf(fp, "\t\"PhilipsScaleSlope\": %g,\n", d.intenScalePhilips );
		fprintf(fp, "\t\"UsePhilipsFloatNotDisplayScaling\": %d,\n", opts.isPhilipsFloatNotDisplayScaling);
	}
	//PET ISOTOPE MODULE ATTRIBUTES
	if (d.radionuclidePositronFraction > 0.0) fprintf(fp, "\t\"RadionuclidePositronFraction\": %g,\n", d.radionuclidePositronFraction );
	if (d.radionuclideTotalDose > 0.0) fprintf(fp, "\t\"RadionuclideTotalDose\": %g,\n", d.radionuclideTotalDose );
	if (d.radionuclideHalfLife > 0.0) fprintf(fp, "\t\"RadionuclideHalfLife\": %g,\n", d.radionuclideHalfLife );
	if (d.doseCalibrationFactor > 0.0) fprintf(fp, "\t\"DoseCalibrationFactor\": %g,\n", d.doseCalibrationFactor );
	//MRI parameters
	if (d.fieldStrength > 0.0) fprintf(fp, "\t\"MagneticFieldStrength\": %g,\n", d.fieldStrength );
	if (d.flipAngle > 0.0) fprintf(fp, "\t\"FlipAngle\": %g,\n", d.flipAngle );
	if ((d.TE > 0.0) && (!d.isXRay)) fprintf(fp, "\t\"EchoTime\": %g,\n", d.TE / 1000.0 );
    if ((d.TE > 0.0) && (d.isXRay)) fprintf(fp, "\t\"XRayExposure\": %g,\n", d.TE );
    if (d.TR > 0.0) fprintf(fp, "\t\"RepetitionTime\": %g,\n", d.TR / 1000.0 );
    if (d.TI > 0.0) fprintf(fp, "\t\"InversionTime\": %g,\n", d.TI / 1000.0 );
    if (d.ecat_isotope_halflife > 0.0) fprintf(fp, "\t\"IsotopeHalfLife\": %g,\n", d.ecat_isotope_halflife);
    if (d.ecat_dosage > 0.0) fprintf(fp, "\t\"Dosage\": %g,\n", d.ecat_dosage);
    double bandwidthPerPixelPhaseEncode = d.bandwidthPerPixelPhaseEncode;
    int phaseEncodingLines = d.phaseEncodingLines;
    if ((phaseEncodingLines == 0) &&  (h->dim[2] > 0) && (h->dim[1] > 0)) {
		if  (h->dim[2] == h->dim[2]) //phase encoding does not matter
			phaseEncodingLines = h->dim[2];
		else if (d.phaseEncodingRC =='R')
			phaseEncodingLines = h->dim[2];
		else if (d.phaseEncodingRC =='C')
			phaseEncodingLines = h->dim[1];
    }
    if (bandwidthPerPixelPhaseEncode == 0.0)
    	bandwidthPerPixelPhaseEncode = 	d.CSA.bandwidthPerPixelPhaseEncode;
    if (phaseEncodingLines > 0.0) fprintf(fp, "\t\"PhaseEncodingLines\": %d,\n", phaseEncodingLines );
    if (bandwidthPerPixelPhaseEncode > 0.0)
    	fprintf(fp, "\t\"BandwidthPerPixelPhaseEncode\": %g,\n", bandwidthPerPixelPhaseEncode );
    double effectiveEchoSpacing = 0.0;
    if ((phaseEncodingLines > 0) && (bandwidthPerPixelPhaseEncode > 0.0))
    	effectiveEchoSpacing = 1.0 / (bandwidthPerPixelPhaseEncode * phaseEncodingLines) ;
    if (d.effectiveEchoSpacingGE > 0.0)
    	effectiveEchoSpacing = d.effectiveEchoSpacingGE / 1000000.0;
    if (effectiveEchoSpacing > 0.0)
    		fprintf(fp, "\t\"EffectiveEchoSpacing\": %g,\n", effectiveEchoSpacing);
    //FSL definition is start of first line until start of last line, so n-1 unless accelerated in-plane acquisition
    // to check: partial Fourier, iPAT, etc.
	int fencePost = 1;
    if (d.accelFactPE > 1.0)
    	fencePost = (int)round(d.accelFactPE); //e.g. if 64 lines with iPAT=2, we want time from start of first until start of 62nd effective line
    if ((d.phaseEncodingSteps > 1) && (effectiveEchoSpacing > 0.0))
		fprintf(fp, "\t\"TotalReadoutTime\": %g,\n", effectiveEchoSpacing * ((float)d.phaseEncodingSteps - fencePost));
    if (d.accelFactPE > 1.0) {
    		fprintf(fp, "\t\"ParallelReductionFactorInPlane\": %g,\n", d.accelFactPE);
    		if (effectiveEchoSpacing > 0.0)
    			fprintf(fp, "\t\"TrueEchoSpacing\": %g,\n", effectiveEchoSpacing * d.accelFactPE);
	}
	if ((d.manufacturer == kMANUFACTURER_SIEMENS) && (d.dwellTime > 0))
		fprintf(fp, "\t\"DwellTime\": %g,\n", d.dwellTime * 1E-9);
	if (d.CSA.sliceTiming[0] >= 0.0) {
   		fprintf(fp, "\t\"SliceTiming\": [\n");
   		for (int i = 0; i < kMaxEPI3D; i++) {
   			if (d.CSA.sliceTiming[i] < 0.0) break;
			if (i != 0)
				fprintf(fp, ",\n");
			fprintf(fp, "\t\t%g", d.CSA.sliceTiming[i] / 1000.0 );
		}
		fprintf(fp, "\t],\n");
	}
	if (((d.phaseEncodingRC == 'R') || (d.phaseEncodingRC == 'C')) &&  (!d.is3DAcq) && ((d.CSA.phaseEncodingDirectionPositive == 1) || (d.CSA.phaseEncodingDirectionPositive == 0))) {
		if (d.phaseEncodingRC == 'C') //Values should be "R"ow, "C"olumn or "?"Unknown
			fprintf(fp, "\t\"PhaseEncodingDirection\": \"j");
		else if (d.phaseEncodingRC == 'R')
				fprintf(fp, "\t\"PhaseEncodingDirection\": \"i");
		else
			fprintf(fp, "\t\"PhaseEncodingDirection\": \"?");
		//phaseEncodingDirectionPositive has one of three values: UNKNOWN (-1), NEGATIVE (0), POSITIVE (1)
		//However, DICOM and NIfTI are reversed in the j (ROW) direction
		//Equivalent to dicm2nii's "if flp(iPhase), phPos = ~phPos; end"
		//for samples see https://github.com/rordenlab/dcm2niix/issues/125
		if (d.CSA.phaseEncodingDirectionPositive == -1)
			fprintf(fp, "?"); //unknown
		else if ((d.CSA.phaseEncodingDirectionPositive == 0) && (d.phaseEncodingRC != 'C'))
			fprintf(fp, "-");
		else if ((d.phaseEncodingRC == 'C') && (d.CSA.phaseEncodingDirectionPositive == 1) && (opts.isFlipY))
			fprintf(fp, "-");
		else if ((d.phaseEncodingRC == 'C') && (d.CSA.phaseEncodingDirectionPositive == 0) && (!opts.isFlipY))
			fprintf(fp, "-");
		fprintf(fp, "\",\n");
	} //only save PhaseEncodingDirection if BOTH direction and POLARITY are known
	fprintf(fp, "\t\"ConversionSoftware\": \"dcm2niix\",\n");
	fprintf(fp, "\t\"ConversionSoftwareVersion\": \"%s\"\n", kDCMvers );
	//fprintf(fp, "\t\"DicomConversion\": [\"dcm2niix\", \"%s\"]\n", kDCMvers );
    fprintf(fp, "}\n");
    fclose(fp);
}// nii_SaveBIDS()
