#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "jpg_0XC3.h"

unsigned char  readByte(unsigned char *lRawRA, long *lRawPos, long lRawSz) {
    unsigned char ret = 0x00;
    if (*lRawPos < lRawSz)
        ret = lRawRA[*lRawPos];
    (*lRawPos)++;
    return ret;
} //end readByte()

uint16_t  readWord(unsigned char *lRawRA, long *lRawPos, long lRawSz) {
    return ( (readByte(lRawRA, lRawPos, lRawSz) << 8) + readByte(lRawRA, lRawPos, lRawSz));
} //end readWord()

int readBit(unsigned char *lRawRA, long *lRawPos,  int *lCurrentBitPos) {//Read the next single bit
    int result = (lRawRA[*lRawPos] >> (7 - *lCurrentBitPos)) & 1;
    (*lCurrentBitPos)++;
    if (*lCurrentBitPos == 8) {
        (*lRawPos)++;
        *lCurrentBitPos = 0;
    }
    return result;
} //end readBit()

int bitMask(int bits) {
    return ( (2 << (bits - 1)) -1);
} //bitMask()

int readBits (unsigned char *lRawRA, long *lRawPos,  int *lCurrentBitPos, int  lNum) { //lNum: bits to read, not to exceed 16
    int result = lRawRA[*lRawPos];
    result = (result << 8) + lRawRA[(*lRawPos)+1];
    result = (result << 8) + lRawRA[(*lRawPos)+2];
    result = (result >> (24 - *lCurrentBitPos -lNum)) & bitMask(lNum); //lCurrentBitPos is incremented from 1, so -1
    *lCurrentBitPos = *lCurrentBitPos + lNum;
    if (*lCurrentBitPos > 7) {
            *lRawPos = *lRawPos + (*lCurrentBitPos >> 3); // div 8
            *lCurrentBitPos = *lCurrentBitPos & 7; //mod 8
    }
    return result;
} //end readBits()

struct HufTables {
    uint8_t SSSSszRA[18];
    uint8_t LookUpRA[256];
    int DHTliRA[32];
    int DHTstartRA[32];
    int HufSz[32];
    int HufCode[32];
    int HufVal[32];
    int MaxHufSi;
    int MaxHufVal;
}; //end HufTables()

int decodePixelDifference(unsigned char *lRawRA, long *lRawPos, int *lCurrentBitPos, struct HufTables l) {
    int lByte = (lRawRA[*lRawPos] << *lCurrentBitPos) + (lRawRA[*lRawPos+1] >> (8- *lCurrentBitPos));
    lByte = lByte & 255;
    int lHufValSSSS = l.LookUpRA[lByte];
    if (lHufValSSSS < 255) {
        *lCurrentBitPos = l.SSSSszRA[lHufValSSSS] + *lCurrentBitPos;
        *lRawPos = *lRawPos + (*lCurrentBitPos >> 3);
        *lCurrentBitPos = *lCurrentBitPos & 7;
    } else { //full SSSS is not in the first 8-bits
        int lInput = lByte;
        int lInputBits = 8;
        (*lRawPos)++; // forward 8 bits = precisely 1 byte
        do {
            lInputBits++;
            lInput = (lInput << 1) + readBit(lRawRA, lRawPos, lCurrentBitPos);
            if (l.DHTliRA[lInputBits] != 0) { //if any entires with this length
                for (int lI = l.DHTstartRA[lInputBits]; lI <= (l.DHTstartRA[lInputBits]+l.DHTliRA[lInputBits]-1); lI++) {
                    if (lInput == l.HufCode[lI])
                        lHufValSSSS = l.HufVal[lI];
                } //check each code
            } //if any entries with this length
            if ((lInputBits >= l.MaxHufSi) && (lHufValSSSS > 254)) {//exhausted options CR: added rev13
                lHufValSSSS = l.MaxHufVal;
            }
        } while (!(lHufValSSSS < 255)); // found;
    } //answer in first 8 bits
    //The HufVal is referred to as the SSSS in the Codec, so it is called 'lHufValSSSS'
    if (lHufValSSSS == 0) //NO CHANGE
      return 0;
    if (lHufValSSSS == 1) {
        if (readBit(lRawRA, lRawPos, lCurrentBitPos) == 0)
            return -1;
        else
            return 1;
     }
    if (lHufValSSSS == 16) { //ALL CHANGE 16 bit difference: Codec H.1.2.2 "No extra bits are appended after SSSS = 16 is encoded." Osiris fails here
        return 32768;
    }
    //to get here - there is a 2..15 bit difference
    int lDiff = readBits(lRawRA, lRawPos, lCurrentBitPos, lHufValSSSS);
    if (lDiff <= bitMask(lHufValSSSS-1))  //add
        lDiff = lDiff - bitMask(lHufValSSSS);
    return lDiff;
} //end decodePixelDifference()

unsigned char *  decode_JPEG_SOF_0XC3 (const char *fn, int skipBytes, bool verbose, int *dimX, int *dimY, int *bits, int *frames, int diskBytes) {
    //decompress JPEG image named "fn" where image data is located skipBytes into file. diskBytes is compressed size of image (set to 0 if unknown)
    #define abortGoto() free(lRawRA); return NULL;
    unsigned char *lImgRA8 = NULL;
    FILE *reader = fopen(fn, "rb");
    fseek(reader, 0, SEEK_END);
    long lRawSz = ftell(reader)- skipBytes;
    if ((diskBytes > 0) and (diskBytes < lRawSz)) //only if diskBytes is known and does not exceed length of file
        lRawSz = diskBytes;
    if (lRawSz <= 8) {
        printf("Error opening %s\n", fn);
        return NULL; //read failure
    }
    fseek(reader, skipBytes, SEEK_SET);
    unsigned char *lRawRA = (unsigned char*) malloc(lRawSz);
    fread(lRawRA, 1, lRawSz, reader);
    fclose(reader);
    if ((lRawRA[0] != 0xFF) || (lRawRA[1] != 0xD8) || (lRawRA[2] != 0xFF)) {
        printf("Error: JPEG signature 0xFFD8FF not found at offset %d of %s\n", skipBytes, fn);
        abortGoto();//goto abortGoto; //signature failure http://en.wikipedia.org/wiki/List_of_file_signatures
    }
    if (verbose)
        printf("JPEG signature 0xFFD8FF found at offset %d of %s\n", skipBytes, fn);
    //next: read header
    long lRawPos = 2; //Skip initial 0xFFD8, begin with third byte
    //long lRawPos = 0; //Skip initial 0xFFD8, begin with third byte
    unsigned char btS1, btS2, SOSss, SOSse, SOSahal, SOSpttrans, btMarkerType, SOSns = 0x00; //tag
    uint8_t SOFnf, SOFprecision;
    uint16_t SOFydim, SOFxdim; //, lRestartSegmentSz;
    // long SOSarrayPos; //SOFarrayPos
    int lnHufTables = 0;
    const int kmaxFrames = 4;
    struct HufTables l[kmaxFrames+1];
    do { //read each marker in the header
        do {
            btS1 = readByte(lRawRA, &lRawPos, lRawSz);
            if (btS1 != 0xFF) {
                printf("JPEG header tag must begin with 0xFF\n");
                abortGoto(); //goto abortGoto;
            }
            btMarkerType =  readByte(lRawRA, &lRawPos, lRawSz);
            if ((btMarkerType == 0x01) || (btMarkerType == 0xFF) || ((btMarkerType >= 0xD0) && (btMarkerType <= 0xD7) ) )
                btMarkerType = 0;//only process segments with length fields
            
        } while ((lRawPos < lRawSz) && (btMarkerType == 0));
        uint16_t lSegmentLength = readWord (lRawRA, &lRawPos, lRawSz); //read marker length
        long lSegmentEnd = lRawPos+(lSegmentLength - 2);
        if (lSegmentEnd > lRawSz)  {
            abortGoto(); //goto abortGoto;
        }
        if (verbose)
            printf("btMarkerType %#02X length %d@%ld\n", btMarkerType, lSegmentLength, lRawPos);
        if ( ((btMarkerType >= 0xC0) && (btMarkerType <= 0xC3)) || ((btMarkerType >= 0xC5) && (btMarkerType <= 0xCB)) || ((btMarkerType >= 0xCD) && (btMarkerType <= 0xCF)) )  {
            //if Start-Of-Frame (SOF) marker
            SOFprecision = readByte(lRawRA, &lRawPos, lRawSz);
            SOFydim = readWord(lRawRA, &lRawPos, lRawSz);
            SOFxdim = readWord(lRawRA, &lRawPos, lRawSz);
            SOFnf = readByte(lRawRA, &lRawPos, lRawSz);
            //SOFarrayPos = lRawPos;
            lRawPos = (lSegmentEnd);
            if (verbose) printf(" [Precision %d X*Y %d*%d Frames %d]\n", SOFprecision, SOFxdim, SOFydim, SOFnf);
            if (btMarkerType != 0xC3) { //lImgTypeC3 = true;
                printf("This JPEG decoder can only decompress lossless JPEG ITU-T81 images (SoF must be 0XC3, not %#02X)\n",btMarkerType );
                abortGoto(); //goto abortGoto;
            }
            if ( (SOFprecision < 1) || (SOFprecision > 16) || (SOFnf < 1) || (SOFnf == 2) || (SOFnf > 3)
                || ((SOFnf == 3) &&  (SOFprecision > 8))   ) {
                printf("Scalar data must be 1..16 bit, RGB data must be 8-bit (%d-bit, %d frames)\n", SOFprecision, SOFnf);
                abortGoto(); //goto abortGoto;
            }
        } else if (btMarkerType == 0xC4) {//if SOF marker else if define-Huffman-tables marker (DHT)
            if (verbose) printf(" [Huffman Length %d]\n", lSegmentLength);
            int lFrameCount = 1;
            do {
                uint8_t DHTnLi = readByte(lRawRA, &lRawPos, lRawSz ); //we read but ignore DHTtcth.
                #pragma unused(DHTnLi) //we need to increment the input file position, but we do not care what the value is
                DHTnLi = 0;
                for (int lInc = 1; lInc <= 16; lInc++) {
                    l[lFrameCount].DHTliRA[lInc] = readByte(lRawRA, &lRawPos, lRawSz);
                    DHTnLi = DHTnLi +  l[lFrameCount].DHTliRA[lInc];
                    if (l[lFrameCount].DHTliRA[lInc] != 0) l[lFrameCount].MaxHufSi = lInc;
                }
                if (DHTnLi > 17) {
                    printf("Huffman table corrupted.\n");
                    abortGoto(); //goto abortGoto;
                }
                int lIncY = 0; //frequency
                for (int lInc = 0; lInc <= 31; lInc++) {//lInc := 0 to 31 do begin
                    l[lFrameCount].HufVal[lInc] = -1;
                    l[lFrameCount].HufSz[lInc] = -1;
                    l[lFrameCount].HufCode[lInc] = -1;
                }
                for (int lInc = 1; lInc <= 16; lInc++) {//set the huffman size values
                    if (l[lFrameCount].DHTliRA[lInc] > 0) {
                        l[lFrameCount].DHTstartRA[lInc] = lIncY+1;
                        for (int lIncX = 1; lIncX <= l[lFrameCount].DHTliRA[lInc]; lIncX++) {
                            lIncY++;
                            btS1 = readByte(lRawRA, &lRawPos, lRawSz);
                            l[lFrameCount].HufVal[lIncY] = btS1;
                            l[lFrameCount].MaxHufVal = btS1;
                            if ((btS1 >= 0) && (btS1 <= 16))
                                l[lFrameCount].HufSz[lIncY] = lInc;
                            else {
                                printf("Huffman size array corrupted.\n");
                                abortGoto(); //goto abortGoto;
                            }
                        }
                    }
                } //set huffman size values
                int K = 1;
                int Code = 0;
                int Si = l[lFrameCount].HufSz[K];
                do {
                    while (Si == l[lFrameCount].HufSz[K]) {
                        l[lFrameCount].HufCode[K] = Code;
                        Code = Code + 1;
                        K++;
                    }
                    if (K <= DHTnLi) {
                        while (l[lFrameCount].HufSz[K] > Si) {
                            Code = Code << 1; //Shl!!!
                            Si = Si + 1;
                        }//while Si
                    }//K <= 17
                    
                } while (K <= DHTnLi);
                //if (verbose)
                //    for (int j = 1; j <= DHTnLi; j++)
                //        printf(" [%d Sz %d Code %d Value %d]\n", j, l[lFrameCount].HufSz[j], l[lFrameCount].HufCode[j], l[lFrameCount].HufVal[j]);
                lFrameCount++;
            } while ((lSegmentEnd-lRawPos) >= 18);
            lnHufTables = lFrameCount - 1;
            lRawPos = (lSegmentEnd);
            if (verbose) printf(" [FrameCount %d]\n", lnHufTables);
        } else if (btMarkerType == 0xDD) {  //if DHT marker else if Define restart interval (DRI) marker
            printf("This image uses Restart Segments - please contact Chris Rorden to add support for this format.\n");
            abortGoto(); //goto abortGoto;
            //lRestartSegmentSz = ReadWord(lRawRA, &lRawPos, lRawSz);
            //lRawPos = lSegmentEnd;
        } else if (btMarkerType == 0xDA) {  //if DRI marker else if read Start of Scan (SOS) marker
            SOSns = readByte(lRawRA, &lRawPos, lRawSz);
            //if Ns = 1 then NOT interleaved, else interleaved: see B.2.3
            // SOSarrayPos = lRawPos; //not required...
            if (SOSns > 0) {
                for (int lInc = 1; lInc <= SOSns; lInc++) {
                    btS1 = readByte(lRawRA, &lRawPos, lRawSz); //component identifier 1=Y,2=Cb,3=Cr,4=I,5=Q
                    #pragma unused(btS1) //dummy value used to increment file position
                    btS2 = readByte(lRawRA, &lRawPos, lRawSz); //horizontal and vertical sampling factors
                    #pragma unused(btS2) //dummy value used to increment file position
                }
            }
            SOSss = readByte(lRawRA, &lRawPos, lRawSz); //predictor selection B.3
            SOSse = readByte(lRawRA, &lRawPos, lRawSz);
            #pragma unused(SOSse) //dummy value used to increment file position
            SOSahal = readByte(lRawRA, &lRawPos, lRawSz); //lower 4bits= pointtransform
            SOSpttrans = SOSahal & 16;
            if (verbose)
                printf(" [Predictor: %d Transform %d]\n", SOSss, SOSahal);
            lRawPos = (lSegmentEnd);
        } else  //if SOS marker else skip marker
            lRawPos = (lSegmentEnd);
    } while ((lRawPos < lRawSz) && (btMarkerType != 0xDA)); //0xDA=Start of scan: loop for reading header
    //NEXT: Huffman decoding
    if (lnHufTables < 1) {
        printf("Decoding error: no Huffman tables.\n");
        abortGoto(); //goto abortGoto;
    }
    //NEXT: unpad data - delete byte that follows $FF
    long lIncI = lRawPos; //input position
    long lIncO = lRawPos; //output position
    do {
        lRawRA[lIncO] = lRawRA[lIncI];
        if (lRawRA[lIncI] == 255) {
            if (lRawRA[lIncI+1] == 0)
                lIncI = lIncI+1;
            else if (lRawRA[lIncI+1] == 0xD9)
                lIncO = -666; //end of padding
        }
        lIncI++;
        lIncO++;
    } while (lIncO > 0);
    //NEXT: some RGB images use only a single Huffman table for all 3 colour planes. In this case, replicate the correct values
    //NEXT: prepare lookup table

    for (int lFrameCount = 1; lFrameCount <= lnHufTables; lFrameCount ++) {
        for (int lInc = 0; lInc <= 17; lInc ++)
            l[lFrameCount].SSSSszRA[lInc] = 123; //Impossible value for SSSS, suggests 8-bits can not describe answer
        for (int lInc = 0; lInc <= 255; lInc ++)
            l[lFrameCount].LookUpRA[lInc] = 255; //Impossible value for SSSS, suggests 8-bits can not describe answer
    }
    //NEXT: fill lookuptable
    for (int lFrameCount = 1; lFrameCount <= lnHufTables; lFrameCount ++) {
        int lIncY = 0;
        for (int lSz = 1; lSz <= 8; lSz ++) { //set the huffman lookup table for keys with lengths <=8
            if (l[lFrameCount].DHTliRA[lSz]> 0) {
                for (int lIncX = 1; lIncX <= l[lFrameCount].DHTliRA[lSz]; lIncX ++) {
                    lIncY++;
                    int lHufVal = l[lFrameCount].HufVal[lIncY]; //SSSS
                    l[lFrameCount].SSSSszRA[lHufVal] = lSz;
                    int k = (l[lFrameCount].HufCode[lIncY] << (8-lSz )) & 255; //K= most sig bits for hufman table
                    if (lSz < 8) { //fill in all possible bits that exceed the huffman table
                        int lInc = bitMask(8-lSz);
                        for (int lCurrentBitPos = 0; lCurrentBitPos <= lInc; lCurrentBitPos++) {
                            l[lFrameCount].LookUpRA[k+lCurrentBitPos] = lHufVal;
                        }
                    } else
                        l[lFrameCount].LookUpRA[k] = lHufVal; //SSSS
                    //printf("Frame %d SSSS %d Size %d Code %d SHL %d EmptyBits %ld\n", lFrameCount, lHufRA[lFrameCount][lIncY].HufVal, lHufRA[lFrameCount][lIncY].HufSz,lHufRA[lFrameCount][lIncY].HufCode, k, lInc);
                } //Set SSSS
            } //Length of size lInc > 0
        } //for lInc := 1 to 8
    } //For each frame, e.g. once each for Red/Green/Blue
    //NEXT: some RGB images use only a single Huffman table for all 3 colour planes. In this case, replicate the correct values
    if (lnHufTables < SOFnf) { //use single Hufman table for each frame
        for (int lFrameCount = 2; lFrameCount <= SOFnf; lFrameCount++) {
            l[lFrameCount] = l[1];
        } //for each frame
    } // if lnHufTables < SOFnf
    //NEXT: uncompress data: different loops for different predictors
    int lItems =  SOFxdim*SOFydim*SOFnf;
    // lRawPos++;// <- only for Pascal where array is indexed from 1 not 0 first byte of data
    int lCurrentBitPos = 0; //read in a new byte
    
    //depending on SOSss, we see Table H.1
    int lPredA = 0;
    int lPredB = 0;
    int lPredC = 0;
    if (SOSss == 2) //predictor selection 2: above
        lPredA = SOFxdim-1;
    else if (SOSss == 3) //predictor selection 3: above+left
        lPredA = SOFxdim;
    else if ((SOSss == 4) || (SOSss == 5)) { //these use left, above and above+left WEIGHT LEFT
        lPredA = 0; //Ra left
        lPredB = SOFxdim-1; //Rb directly above
        lPredC = SOFxdim; //Rc UpperLeft:above and to the left
    } else if (SOSss == 6) { //also use left, above and above+left, WEIGHT ABOVE
        lPredB = 0;
        lPredA = SOFxdim-1; //Rb directly above
        lPredC = SOFxdim; //Rc UpperLeft:above and to the left
    }   else
        lPredA = 0; //Ra: directly to left)
    if (SOFprecision > 8) { //start - 16 bit data
        *bits = 16;
        int lPx = -1; //pixel position
        int lPredicted =  1 << (SOFprecision-1-SOSpttrans);
        lImgRA8 = (unsigned char*) malloc(lItems * 2);
        uint16_t *lImgRA16 = (uint16_t*) lImgRA8;
        for (int i = 0; i < lItems; i++)
            lImgRA16[i] = 0; //zero array
        int frame = 1;
        for (int lIncX = 1; lIncX <= SOFxdim; lIncX++) { //for first row - here we ALWAYS use LEFT as predictor
            lPx++; //writenext voxel
            if (lIncX > 1) lPredicted = lImgRA16[lPx-1];
            lImgRA16[lPx] = lPredicted+ decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[frame]);
        }
        for (int lIncY = 2; lIncY <= SOFydim; lIncY++) {//for all subsequent rows
            lPx++; //write next voxel
            lPredicted = lImgRA16[lPx-SOFxdim]; //use ABOVE
            lImgRA16[lPx] = lPredicted+decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[frame]);
            if (SOSss == 4) {
                for (int lIncX = 2; lIncX <= SOFxdim; lIncX++) {
                    lPredicted = lImgRA16[lPx-lPredA]+lImgRA16[lPx-lPredB]-lImgRA16[lPx-lPredC];
                    lPx++; //writenext voxel
                    lImgRA16[lPx] = lPredicted+decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[frame]);
                } //for lIncX
            } else if ((SOSss == 5) || (SOSss == 6)) {
                for (int lIncX = 2; lIncX <= SOFxdim; lIncX++) {
                    lPredicted = lImgRA16[lPx-lPredA]+ ((lImgRA16[lPx-lPredB]-lImgRA16[lPx-lPredC]) >> 1);
                    lPx++; //writenext voxel
                    lImgRA16[lPx] = lPredicted+decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[frame]);
                } //for lIncX
            } else if (SOSss == 7) {
                for (int lIncX = 2; lIncX <= SOFxdim; lIncX++) {
                    lPx++; //writenext voxel
                    lPredicted = (lImgRA16[lPx-1]+lImgRA16[lPx-SOFxdim]) >> 1;
                    lImgRA16[lPx] = lPredicted+decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[frame]);
                } //for lIncX
            } else { //SOSss 1,2,3 read single values
                for (int lIncX = 2; lIncX <= SOFxdim; lIncX++) {
                    lPredicted = lImgRA16[lPx-lPredA];
                    lPx++; //writenext voxel
                    lImgRA16[lPx] = lPredicted+decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[frame]);
                } //for lIncX
            } // if..else possible predictors
        }//for lIncY
    } else if (SOFnf == 3) { //if 16-bit data; else 8-bit 3 frames
        *bits = 8;
        lImgRA8 = (unsigned char*) malloc(lItems );
        int lPx[kmaxFrames+1], lPredicted[kmaxFrames+1]; //pixel position
        for (int f = 1; f <= SOFnf; f++) {
            lPx[f] = ((f-1) * (SOFxdim * SOFydim) ) -1;
            lPredicted[f] = 1 << (SOFprecision-1-SOSpttrans);
        }
        for (int i = 0; i < lItems; i++)
            lImgRA8[i] = 255; //zero array
        for (int lIncX = 1; lIncX <= SOFxdim; lIncX++) { //for first row - here we ALWAYS use LEFT as predictor
            for (int f = 1; f <= SOFnf; f++) {
                lPx[f]++; //writenext voxel
                if (lIncX > 1) lPredicted[f] = lImgRA8[lPx[f]-1];
                lImgRA8[lPx[f]] = lPredicted[f] + decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[f]);
            }
        } //first row always predicted by LEFT
        for (int lIncY = 2; lIncY <= SOFydim; lIncY++) {//for all subsequent rows
            for (int f = 1; f <= SOFnf; f++) {
                lPx[f]++; //write next voxel
                lPredicted[f] = lImgRA8[lPx[f]-SOFxdim]; //use ABOVE
                lImgRA8[lPx[f]] = lPredicted[f] + decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[f]);
            }//first column of row always predicted by ABOVE
            if (SOSss == 4) {
                for (int lIncX = 2; lIncX <= SOFxdim; lIncX++) {
                    for (int f = 1; f <= SOFnf; f++) {
                        lPredicted[f] = lImgRA8[lPx[f]-lPredA]+lImgRA8[lPx[f]-lPredB]-lImgRA8[lPx[f]-lPredC];
                        lPx[f]++; //writenext voxel
                        lImgRA8[lPx[f]] = lPredicted[f]+decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[f]);
                    }
                } //for lIncX
            } else if ((SOSss == 5) || (SOSss == 6)) {
                for (int lIncX = 2; lIncX <= SOFxdim; lIncX++) {
                    for (int f = 1; f <= SOFnf; f++) {
                        lPredicted[f] = lImgRA8[lPx[f]-lPredA]+ ((lImgRA8[lPx[f]-lPredB]-lImgRA8[lPx[f]-lPredC]) >> 1);
                        lPx[f]++; //writenext voxel
                        lImgRA8[lPx[f]] = lPredicted[f] + decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[f]);
                    }
                } //for lIncX
            } else if (SOSss == 7) {
                for (int lIncX = 2; lIncX <= SOFxdim; lIncX++) {
                    for (int f = 1; f <= SOFnf; f++) {
                        lPx[f]++; //writenext voxel
                        lPredicted[f] = (lImgRA8[lPx[f]-1]+lImgRA8[lPx[f]-SOFxdim]) >> 1;
                        lImgRA8[lPx[f]] = lPredicted[f] + decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[f]);
                    }
                } //for lIncX
            } else { //SOSss 1,2,3 read single values
                for (int lIncX = 2; lIncX <= SOFxdim; lIncX++) {
                    for (int f = 1; f <= SOFnf; f++) {
                        lPredicted[f] = lImgRA8[lPx[f]-lPredA];
                        lPx[f]++; //writenext voxel
                        lImgRA8[lPx[f]] = lPredicted[f] + decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[f]);
                    }
                } //for lIncX
            } // if..else possible predictors
        }//for lIncY
    }else { //if 8-bit data 3frames; else 8-bit 1 frames
        *bits = 8;
        lImgRA8 = (unsigned char*) malloc(lItems );
        int lPx = -1; //pixel position
        int lPredicted =  1 << (SOFprecision-1-SOSpttrans);
        for (int i = 0; i < lItems; i++)
            lImgRA8[i] = 0; //zero array
        for (int lIncX = 1; lIncX <= SOFxdim; lIncX++) { //for first row - here we ALWAYS use LEFT as predictor
            lPx++; //writenext voxel
            if (lIncX > 1) lPredicted = lImgRA8[lPx-1];
            int dx = decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[1]);
            lImgRA8[lPx] = lPredicted+dx;
        }
        for (int lIncY = 2; lIncY <= SOFydim; lIncY++) {//for all subsequent rows
            lPx++; //write next voxel
            lPredicted = lImgRA8[lPx-SOFxdim]; //use ABOVE
            lImgRA8[lPx] = lPredicted+decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[1]);
            if (SOSss == 4) {
                for (int lIncX = 2; lIncX <= SOFxdim; lIncX++) {
                    lPredicted = lImgRA8[lPx-lPredA]+lImgRA8[lPx-lPredB]-lImgRA8[lPx-lPredC];
                    lPx++; //writenext voxel
                    lImgRA8[lPx] = lPredicted+decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[1]);
                } //for lIncX
            } else if ((SOSss == 5) || (SOSss == 6)) {
                for (int lIncX = 2; lIncX <= SOFxdim; lIncX++) {
                    lPredicted = lImgRA8[lPx-lPredA]+ ((lImgRA8[lPx-lPredB]-lImgRA8[lPx-lPredC]) >> 1);
                    lPx++; //writenext voxel
                    lImgRA8[lPx] = lPredicted+decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[1]);
                } //for lIncX
            } else if (SOSss == 7) {
                for (int lIncX = 2; lIncX <= SOFxdim; lIncX++) {
                    lPx++; //writenext voxel
                    lPredicted = (lImgRA8[lPx-1]+lImgRA8[lPx-SOFxdim]) >> 1;
                    lImgRA8[lPx] = lPredicted+decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[1]);
                } //for lIncX
            } else { //SOSss 1,2,3 read single values
                for (int lIncX = 2; lIncX <= SOFxdim; lIncX++) {
                    lPredicted = lImgRA8[lPx-lPredA];
                    lPx++; //writenext voxel
                    lImgRA8[lPx] = lPredicted+decodePixelDifference(lRawRA, &lRawPos, &lCurrentBitPos, l[1]);
                } //for lIncX
            } // if..else possible predictors
        }//for lIncY
    } //if 16bit else 8bit
    free(lRawRA);
    *dimX = SOFxdim;
    *dimY = SOFydim;
    *frames = SOFnf;
    if (verbose)
        printf("JPEG ends %ld@%ld\n", lRawPos, lRawPos+skipBytes);
    return lImgRA8;
} //end decode_JPEG_SOF_0XC3()

